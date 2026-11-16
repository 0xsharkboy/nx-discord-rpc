#include "platform/init.hpp"

#include <cstddef>
#include <iterator>

namespace {

// Socket transfer memory is carved out of this heap too (global budget: 4 MB hard ceiling).
constexpr size_t kInnerHeapSize = 1 * 1024 * 1024;

const SocketInitConfig kSocketConfig = {
    .tcp_tx_buf_size = 0x4000,
    .tcp_rx_buf_size = 0x8000,
    .tcp_tx_buf_max_size = 0,
    .tcp_rx_buf_max_size = 0,
    .udp_tx_buf_size = 0x2400,
    .udp_rx_buf_size = 0xA500,
    .sb_efficiency = 2,
    .num_bsd_sessions = 1,
    .bsd_service_type = BsdServiceType_User,
};

struct ServiceInit {
    const char* name;
    Result (*init)();
    void (*exit)();
    bool up;
};

Result init_socket() { return socketInitialize(&kSocketConfig); }
Result init_ssl() { return sslInitialize(1); }
Result init_nifm() { return nifmInitialize(NifmServiceType_User); }

ServiceInit g_services[] = {
    {"time", timeInitialize, timeExit, false},
    {"pm:dmnt", pmdmntInitialize, pmdmntExit, false},
    {"pm:info", pminfoInitialize, pminfoExit, false},
    {"ns", nsInitialize, nsExit, false},
    {"bsd", init_socket, socketExit, false},
    {"ssl", init_ssl, sslExit, false},
    {"nifm", init_nifm, nifmExit, false},
};

std::optional<drpc::InitFailure> g_failure;

}  // namespace

std::optional<drpc::InitFailure> drpc::init_failure() {
    return g_failure;
}

extern "C" {

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

void __libnx_initheap(void) {
    static u8 inner_heap[kInnerHeapSize];
    extern void* fake_heap_start;
    extern void* fake_heap_end;
    fake_heap_start = inner_heap;
    fake_heap_end = inner_heap + sizeof(inner_heap);
}

void __appInit(void) {
    // Without sm or fs nothing (not even the log) can work; anything later degrades to a clean exit instead.
    // sm stays open: the resolver opens sfdnsres through it on every lookup.
    Result rc = smInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(rc);

    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }

    rc = fsInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(rc);
    fsdevMountSdmc();

    for (ServiceInit& s : g_services) {
        rc = s.init();
        if (R_FAILED(rc)) {
            g_failure = drpc::InitFailure{s.name, rc};
            return;
        }
        s.up = true;
    }
}

void __appExit(void) {
    for (size_t i = std::size(g_services); i-- > 0;) {
        if (g_services[i].up) g_services[i].exit();
        g_services[i].up = false;
    }
    fsdevUnmountAll();
    fsExit();
    smExit();
}

}  // extern "C"
