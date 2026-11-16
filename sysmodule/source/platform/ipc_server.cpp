#include "platform/ipc_server.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <span>

#include "core/ipc_dispatch.hpp"
#include "drpc/protocol.hpp"

namespace drpc {

namespace {

constexpr size_t kStackSize = 0x4000;
constexpr int kPriority = 0x2C;
constexpr int kCore = 3;
constexpr s32 kMaxSessions = 4;
constexpr size_t kMessageSize = 0x100;
constexpr u64 kFailureBackoffNs = 100'000'000;
constexpr Result kRcBadRequestType = MAKERESULT(11, 403);
constexpr u32 kControlQueryPointerBufferSize = 3;

struct Server {
    Thread thread{};
    SharedState* state = nullptr;
    Handle port = INVALID_HANDLE;
    std::array<Handle, kMaxSessions> sessions{};
    s32 session_count = 0;
    bool running = false;
    std::atomic<bool> stop{false};
    std::atomic<Result> error{0};
};

Server g_server;

void report(Result rc) { g_server.error.store(rc); }

void close_session(s32 index) {
    svcCloseHandle(g_server.sessions[index]);
    g_server.sessions[index] = g_server.sessions[g_server.session_count - 1];
    g_server.session_count--;
}

void close_all() {
    while (g_server.session_count > 0) close_session(g_server.session_count - 1);
    if (g_server.port != INVALID_HANDLE) {
        svcCloseHandle(g_server.port);
        g_server.port = INVALID_HANDLE;
    }
    smUnregisterService(smEncodeName(ipc::kServiceName));
}

void accept_session() {
    Handle session;
    const Result rc = svcAcceptSession(&session, g_server.port);
    if (R_FAILED(rc)) {
        report(rc);
        svcSleepThread(kFailureBackoffNs);
        return;
    }
    if (g_server.session_count >= kMaxSessions) {
        svcCloseHandle(session);
        return;
    }
    g_server.sessions[g_server.session_count++] = session;
}

void write_reply(Result rc, std::span<const uint8_t> raw) {
    void* tls = armGetTls();
    const size_t raw_size = R_SUCCEEDED(rc) ? raw.size() : 0;
    // 0x10 extra for the alignment of the CMIF header, as libnx does on the request side.
    const u32 words = static_cast<u32>((sizeof(CmifOutHeader) + raw_size + 0x10 + 3) / 4);
    HipcMetadata meta{};
    meta.type = CmifCommandType_Request;
    meta.num_data_words = words;
    const HipcRequest hipc = hipcMakeRequest(tls, meta);
    auto* hdr = static_cast<CmifOutHeader*>(cmifGetAlignedDataStart(hipc.data_words, tls));
    hdr->magic = CMIF_OUT_HEADER_MAGIC;
    hdr->version = 0;
    hdr->result = rc;
    hdr->token = 0;
    if (raw_size > 0) std::memcpy(hdr + 1, raw.data(), raw_size);
}

// The CMIF in-header of a received message, or null when it does not fit the message.
// raw receives the bytes after the header up to the end of the data words.
const CmifInHeader* parse_header(const HipcParsedRequest& req, std::span<const uint8_t>& raw) {
    auto* tls = static_cast<uint8_t*>(armGetTls());
    if (req.meta.num_data_words == 0 || req.data.data_words == nullptr) return nullptr;
    const auto* data_begin = reinterpret_cast<const uint8_t*>(req.data.data_words);
    const uint8_t* data_end = data_begin + req.meta.num_data_words * 4;
    if (data_end > tls + kMessageSize) return nullptr;
    auto* hdr = static_cast<const CmifInHeader*>(cmifGetAlignedDataStart(req.data.data_words, tls));
    const auto* raw_begin = reinterpret_cast<const uint8_t*>(hdr + 1);
    if (raw_begin > data_end || hdr->magic != CMIF_IN_HEADER_MAGIC) return nullptr;
    raw = {raw_begin, static_cast<size_t>(data_end - raw_begin)};
    return hdr;
}

// Builds the reply in TLS for a received CMIF request.
void handle_request(const HipcParsedRequest& req) {
    std::span<const uint8_t> raw;
    const CmifInHeader* hdr = parse_header(req, raw);
    if (hdr == nullptr) {
        write_reply(kRcBadInput, {});
        return;
    }

    std::span<uint8_t> out_buf;
    if (req.meta.num_recv_buffers > 0) {
        const HipcBufferDescriptor* desc = &req.data.recv_buffers[0];
        auto* addr = static_cast<uint8_t*>(hipcGetBufferAddress(desc));
        if (addr != nullptr) out_buf = {addr, hipcGetBufferSize(desc)};
    }

    const DispatchResult result = dispatch(hdr->command_id, raw, out_buf, *g_server.state);
    write_reply(result.rc, result.raw_out);
}

// libnx clients send QueryPointerBufferSize on connect; we have no pointer buffer.
void handle_control(const HipcParsedRequest& req) {
    std::span<const uint8_t> raw;
    const CmifInHeader* hdr = parse_header(req, raw);
    if (hdr == nullptr || hdr->command_id != kControlQueryPointerBufferSize) {
        write_reply(kRcBadRequestType, {});
        return;
    }
    const uint16_t size = 0;
    write_reply(0, {reinterpret_cast<const uint8_t*>(&size), sizeof(size)});
}

// False when the session is gone or closed by the client and must be dropped.
bool serve(Handle session) {
    void* tls = armGetTls();
    std::memset(tls, 0, 8);  // no receive list: a stale header must not describe one
    s32 index;
    // Timeout 0: the kernel answers a request it rejects itself and keeps waiting, which
    // must not park the thread on this session.
    Result rc = svcReplyAndReceive(&index, &session, 1, INVALID_HANDLE, 0);
    if (rc == KERNELRESULT(TimedOut)) return true;
    if (R_FAILED(rc)) {
        if (rc != KERNELRESULT(ConnectionClosed) && rc != KERNELRESULT(Cancelled)) report(rc);
        return false;
    }

    const HipcParsedRequest req = hipcParseRequest(tls);
    // Handles live in TLS, which the reply overwrites: copy them out first.
    std::array<Handle, 32> handles{};
    size_t handle_count = 0;
    for (u32 i = 0; i < req.meta.num_copy_handles; i++) handles[handle_count++] = req.data.copy_handles[i];
    for (u32 i = 0; i < req.meta.num_move_handles; i++) handles[handle_count++] = req.data.move_handles[i];

    bool keep = true;
    switch (req.meta.type) {
        case CmifCommandType_Close:
            keep = false;
            break;
        case CmifCommandType_Request:
            handle_request(req);
            break;
        case CmifCommandType_Control:
            handle_control(req);
            break;
        default:
            write_reply(kRcBadRequestType, {});
            break;
    }
    for (size_t i = 0; i < handle_count; i++) svcCloseHandle(handles[i]);
    if (!keep) return false;

    rc = svcReplyAndReceive(&index, &session, 0, session, 0);
    if (rc == KERNELRESULT(TimedOut)) return true;
    if (R_FAILED(rc) && rc != KERNELRESULT(ConnectionClosed) && rc != KERNELRESULT(Cancelled)) report(rc);
    return R_SUCCEEDED(rc);
}

void server_thread(void*) {
    std::array<Handle, kMaxSessions + 1> handles{};
    while (!g_server.stop.load()) {
        // While full, the port stays out of the wait set so a pending connection cannot spin the loop.
        const bool with_port = g_server.session_count < kMaxSessions;
        s32 count = 0;
        if (with_port) handles[count++] = g_server.port;
        for (s32 i = 0; i < g_server.session_count; i++) handles[count++] = g_server.sessions[i];

        s32 index = -1;
        const Result rc = svcWaitSynchronization(&index, handles.data(), count, UINT64_MAX);
        if (g_server.stop.load()) break;
        if (R_FAILED(rc)) {
            if (rc != KERNELRESULT(Cancelled)) {
                report(rc);
                svcSleepThread(kFailureBackoffNs);
            }
            continue;
        }
        if (index < 0 || index >= count) continue;

        if (with_port && index == 0) {
            accept_session();
            continue;
        }
        const s32 session = index - (with_port ? 1 : 0);
        if (!serve(g_server.sessions[session])) close_session(session);
    }
}

}  // namespace

bool ipc_server_start(SharedState& state) {
    if (g_server.running) return true;
    g_server.state = &state;
    g_server.stop.store(false);
    g_server.session_count = 0;

    Result rc = smRegisterService(&g_server.port, smEncodeName(ipc::kServiceName), false, kMaxSessions);
    if (R_FAILED(rc)) {
        g_server.port = INVALID_HANDLE;
        report(rc);
        return false;
    }
    rc = threadCreate(&g_server.thread, server_thread, nullptr, nullptr, kStackSize, kPriority, kCore);
    if (R_SUCCEEDED(rc)) {
        rc = threadStart(&g_server.thread);
        if (R_FAILED(rc)) threadClose(&g_server.thread);
    }
    if (R_FAILED(rc)) {
        close_all();
        report(rc);
        return false;
    }
    g_server.running = true;
    return true;
}

void ipc_server_stop() {
    if (!g_server.running) return;
    g_server.stop.store(true);
    svcCancelSynchronization(g_server.thread.handle);
    threadWaitForExit(&g_server.thread);
    threadClose(&g_server.thread);
    close_all();
    g_server.running = false;
}

Result ipc_server_take_error() { return g_server.error.exchange(0); }

}  // namespace drpc
