#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "core/ipc_dispatch.hpp"
#include "core/shared_state.hpp"
#include "drpc/protocol.hpp"

namespace {

using drpc::DispatchResult;
using drpc::SharedState;
using drpc::ipc::Cmd;

DispatchResult call(SharedState& state, Cmd cmd, std::span<const uint8_t> in = {}, std::span<uint8_t> out = {}) {
    return drpc::dispatch(static_cast<uint32_t>(cmd), in, out, state);
}

template <typename T>
std::vector<uint8_t> bytes_of(const T& value) {
    std::vector<uint8_t> v(sizeof(T));
    std::memcpy(v.data(), &value, sizeof(T));
    return v;
}

drpc::ipc::Settings valid_settings() {
    drpc::ipc::Settings s{};
    s.enabled = 1;
    s.private_mode = 0;
    s.show_elapsed = 1;
    s.show_firmware = 0;
    s.show_home_menu = 1;
    s.debug_log = 1;
    return s;
}

void check_protocol_version(SharedState& state) {
    const DispatchResult r = call(state, Cmd::GetProtocolVersion);
    REQUIRE(r.rc == 0);
    REQUIRE(r.raw_out.size() == sizeof(uint32_t));
    uint32_t version = 0;
    std::memcpy(&version, r.raw_out.data(), sizeof(version));
    CHECK(version == 1);
}

}  // namespace

TEST_CASE("GetProtocolVersion returns 1") {
    SharedState state;
    check_protocol_version(state);
}

TEST_CASE("GetStatus without an out buffer is a bad buffer") {
    SharedState state;
    const DispatchResult r = call(state, Cmd::GetStatus);
    CHECK(r.rc == drpc::kRcBadBuffer);
    CHECK(r.raw_out.empty());
    check_protocol_version(state);
}

TEST_CASE("GetStatus with an undersized out buffer is a bad buffer and writes nothing") {
    SharedState state;
    std::vector<uint8_t> out(sizeof(drpc::ipc::Status) - 1, 0xAB);
    const DispatchResult r = call(state, Cmd::GetStatus, {}, out);
    CHECK(r.rc == drpc::kRcBadBuffer);
    for (uint8_t b : out) CHECK(b == 0xAB);
    check_protocol_version(state);
}

TEST_CASE("GetStatus writes the published status") {
    SharedState state;
    drpc::ipc::Status status{};
    std::strcpy(status.username, "alice");
    std::strcpy(status.title_id, "0100000000010000");
    status.elapsed_s = 1234;
    status.link = drpc::ipc::LinkState::Linked;
    drpc::ipc::Link link{};
    state.publish(status, link);

    std::vector<uint8_t> out(sizeof(drpc::ipc::Status), 0xFF);
    const DispatchResult r = call(state, Cmd::GetStatus, {}, out);
    REQUIRE(r.rc == 0);
    CHECK(r.raw_out.empty());
    CHECK(std::memcmp(out.data(), &status, sizeof(status)) == 0);
}

TEST_CASE("GetLink writes the published link") {
    SharedState state;
    drpc::ipc::Link link{};
    link.phase = drpc::ipc::LinkPhase::Pending;
    link.seconds_left = 300;
    std::strcpy(link.user_code, "ABCD-1234");
    state.publish(drpc::ipc::Status{}, link);

    std::vector<uint8_t> out(sizeof(drpc::ipc::Link) + 16, 0xFF);
    const DispatchResult r = call(state, Cmd::GetLink, {}, out);
    REQUIRE(r.rc == 0);
    CHECK(std::memcmp(out.data(), &link, sizeof(link)) == 0);
}

TEST_CASE("GetLink with an undersized out buffer is a bad buffer") {
    SharedState state;
    std::vector<uint8_t> out(sizeof(drpc::ipc::Link) - 1);
    CHECK(call(state, Cmd::GetLink, {}, out).rc == drpc::kRcBadBuffer);
    check_protocol_version(state);
}

TEST_CASE("SetSettings with 7 bytes is bad input") {
    SharedState state;
    const std::vector<uint8_t> in = bytes_of(valid_settings());
    const DispatchResult r = call(state, Cmd::SetSettings, std::span<const uint8_t>(in.data(), 7));
    CHECK(r.rc == drpc::kRcBadInput);
    CHECK_FALSE(state.take_settings().has_value());
    check_protocol_version(state);
}

TEST_CASE("SetSettings with a boolean other than 0 or 1 is bad input") {
    SharedState state;
    drpc::ipc::Settings s = valid_settings();
    s.enabled = 2;
    const std::vector<uint8_t> in = bytes_of(s);
    CHECK(call(state, Cmd::SetSettings, in).rc == drpc::kRcBadInput);
    CHECK_FALSE(state.take_settings().has_value());

    s = valid_settings();
    s.debug_log = 0xFF;
    const std::vector<uint8_t> in2 = bytes_of(s);
    CHECK(call(state, Cmd::SetSettings, in2).rc == drpc::kRcBadInput);
    CHECK_FALSE(state.take_settings().has_value());
    check_protocol_version(state);
}

TEST_CASE("a valid SetSettings is taken once") {
    SharedState state;
    const drpc::ipc::Settings s = valid_settings();
    const std::vector<uint8_t> in = bytes_of(s);
    REQUIRE(call(state, Cmd::SetSettings, in).rc == 0);

    const auto taken = state.take_settings();
    REQUIRE(taken.has_value());
    CHECK(std::memcmp(&*taken, &s, sizeof(s)) == 0);
    CHECK_FALSE(state.take_settings().has_value());
}

TEST_CASE("SetSettings accepts trailing padding after the struct") {
    SharedState state;
    std::vector<uint8_t> in = bytes_of(valid_settings());
    in.resize(in.size() + 8, 0xEE);
    CHECK(call(state, Cmd::SetSettings, in).rc == 0);
    CHECK(state.take_settings().has_value());
}

TEST_CASE("GetSettings returns the latest set value") {
    SharedState state;
    const drpc::ipc::Settings s = valid_settings();
    const std::vector<uint8_t> in = bytes_of(s);
    REQUIRE(call(state, Cmd::SetSettings, in).rc == 0);

    const DispatchResult r = call(state, Cmd::GetSettings);
    REQUIRE(r.rc == 0);
    REQUIRE(r.raw_out.size() == sizeof(drpc::ipc::Settings));
    CHECK(std::memcmp(r.raw_out.data(), &s, sizeof(s)) == 0);
}

TEST_CASE("an unknown command is rejected") {
    SharedState state;
    const DispatchResult r = drpc::dispatch(99, {}, {}, state);
    CHECK(r.rc == drpc::kRcUnknownCommand);
    CHECK(r.raw_out.empty());
    check_protocol_version(state);
}

TEST_CASE("StartLink is taken once") {
    SharedState state;
    CHECK(call(state, Cmd::StartLink).rc == 0);
    CHECK(state.take_request(Cmd::StartLink));
    CHECK_FALSE(state.take_request(Cmd::StartLink));
}

TEST_CASE("repeated requests coalesce and stay separate per command") {
    SharedState state;
    CHECK(call(state, Cmd::StartLink).rc == 0);
    CHECK(call(state, Cmd::StartLink).rc == 0);
    CHECK(call(state, Cmd::Unlink).rc == 0);
    CHECK_FALSE(state.take_request(Cmd::CancelLink));
    CHECK_FALSE(state.take_request(Cmd::ForgetKey));
    CHECK(state.take_request(Cmd::StartLink));
    CHECK_FALSE(state.take_request(Cmd::StartLink));
    CHECK(state.take_request(Cmd::Unlink));

    CHECK(call(state, Cmd::CancelLink).rc == 0);
    CHECK(call(state, Cmd::ForgetKey).rc == 0);
    CHECK(state.take_request(Cmd::CancelLink));
    CHECK(state.take_request(Cmd::ForgetKey));
}

TEST_CASE("request ignores commands that are not requests") {
    SharedState state;
    state.request(Cmd::GetStatus);
    CHECK_FALSE(state.take_request(Cmd::GetStatus));
}
