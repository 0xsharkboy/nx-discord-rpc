#include <doctest/doctest.h>

#include <cstring>
#include <string>

#include "core/ipc_convert.hpp"
#include "drpc/protocol.hpp"

TEST_CASE("ipc settings is 8 bytes") {
    CHECK(sizeof(drpc::ipc::Settings) == 8);
}

TEST_CASE("ipc settings layout and protocol 4") {
    using drpc::ipc::Settings;
    CHECK(drpc::ipc::kProtocolVersion == 4);
    CHECK(offsetof(Settings, show_paused) == 2);
    CHECK(offsetof(Settings, show_docked) == 6);
    CHECK(offsetof(Settings, show_playtime) == 7);
}

TEST_CASE("ipc status layout carries the Discord id and avatar (protocol 2)") {
    using drpc::ipc::Status;
    CHECK(offsetof(Status, discord_id) == 56);
    CHECK(offsetof(Status, avatar) == 80);
    CHECK(offsetof(Status, elapsed_s) == 472);
}

TEST_CASE("to_ipc and apply_ipc round trip the booleans") {
    drpc::Settings s;
    s.enabled = false;
    s.private_mode = true;
    s.show_firmware = true;
    s.show_home_menu = true;
    s.debug_log = true;
    s.show_paused = false;
    s.show_docked = true;
    s.show_playtime = true;
    const drpc::ipc::Settings wire = drpc::to_ipc(s);
    drpc::Settings back;
    drpc::apply_ipc(wire, back);
    CHECK(back == s);
}

TEST_CASE("apply_ipc leaves relay_url, keys and username alone") {
    drpc::Settings s;
    s.relay_url = "https://example.test";
    s.api_key = "k";
    s.admin_key = "a";
    s.username = "u";
    drpc::ipc::Settings wire{};
    drpc::apply_ipc(wire, s);
    CHECK(s.relay_url == "https://example.test");
    CHECK(s.api_key == "k");
    CHECK(s.admin_key == "a");
    CHECK(s.username == "u");
    CHECK_FALSE(s.enabled);
}

TEST_CASE("copy_str truncates on a UTF-8 boundary and terminates") {
    std::string src;
    while (src.size() < 200) src += "\xC3\xA9";
    drpc::ipc::Status st{};
    std::memset(st.name, 'x', sizeof st.name);
    drpc::copy_str(st.name, sizeof st.name, src);
    const size_t n = std::strlen(st.name);
    CHECK(n <= 131);
    CHECK(n % 2 == 0);
    CHECK(st.name[n] == '\0');
    CHECK(std::string(st.name) == src.substr(0, n));
}

TEST_CASE("copy_str fits short strings and empty") {
    char buf[8];
    drpc::copy_str(buf, sizeof buf, "abc");
    CHECK(std::string(buf) == "abc");
    drpc::copy_str(buf, sizeof buf, "");
    CHECK(buf[0] == '\0');
}

TEST_CASE("terminate_strings ends every string field with a NUL") {
    drpc::ipc::Status st;
    std::memset(&st, 'x', sizeof st);
    drpc::ipc::terminate_strings(st);
    CHECK(std::strlen(st.name) == sizeof st.name - 1);
    CHECK(std::strlen(st.relay_host) == sizeof st.relay_host - 1);
    CHECK(std::strlen(st.title_id) == sizeof st.title_id - 1);

    drpc::ipc::Link l;
    std::memset(&l, 'x', sizeof l);
    drpc::ipc::terminate_strings(l);
    CHECK(std::strlen(l.verification_uri) == sizeof l.verification_uri - 1);
    CHECK(std::strlen(l.user_code) == sizeof l.user_code - 1);
    CHECK(std::strlen(l.username) == sizeof l.username - 1);
}

TEST_CASE("ipc status protocol 4: last relay status and running title id at the end") {
    using drpc::ipc::Status;
    CHECK(offsetof(Status, unlink) == 488);
    CHECK(offsetof(Status, last_relay_status) == 492);
    CHECK(offsetof(Status, running_title_id) == 496);
    CHECK(sizeof(Status) == 504);
}

TEST_CASE("ipc hidden list layout and command numbers") {
    using namespace drpc::ipc;
    CHECK(sizeof(HiddenTitle) == 16);
    CHECK(offsetof(HiddenTitle, visibility) == 8);
    CHECK(sizeof(HiddenList) == 8 + 64 * 16);
    CHECK(offsetof(HiddenList, items) == 8);
    CHECK(kMaxHiddenTitles == 64);
    CHECK(static_cast<uint32_t>(Cmd::GetHidden) == 9);
    CHECK(static_cast<uint32_t>(Cmd::SetHidden) == 10);
    CHECK(static_cast<uint8_t>(TitleVisibility::Shown) == 0);
    CHECK(static_cast<uint8_t>(TitleVisibility::Private) == 1);
    CHECK(static_cast<uint8_t>(TitleVisibility::Hidden) == 2);
}

namespace {
drpc::ipc::HiddenList two_entries() {
    drpc::ipc::HiddenList l{};
    l.count = 2;
    l.items[0].title_id = 0x0100A0D02127E000ULL;
    l.items[0].visibility = 1;
    l.items[1].title_id = 0x0100000000010000ULL;
    l.items[1].visibility = 2;
    return l;
}
}  // namespace

TEST_CASE("valid_hidden accepts up to 64 Private or Hidden entries of real titles") {
    using namespace drpc::ipc;
    CHECK(valid_hidden(HiddenList{}));
    CHECK(valid_hidden(two_entries()));
    HiddenList full{};
    full.count = 64;
    for (uint32_t i = 0; i < 64; ++i) {
        full.items[i].title_id = 0x0100000000010000ULL + (uint64_t{i} << 16);
        full.items[i].visibility = 2;
    }
    CHECK(valid_hidden(full));
    full.count = 65;
    CHECK_FALSE(valid_hidden(full));
}

TEST_CASE("valid_hidden rejects a bad visibility, a system title and a duplicate") {
    using namespace drpc::ipc;
    HiddenList l = two_entries();
    l.items[1].visibility = 0;
    CHECK_FALSE(valid_hidden(l));
    l.items[1].visibility = 3;
    CHECK_FALSE(valid_hidden(l));
    l = two_entries();
    l.items[1].title_id = 0x010000000000FFFFULL;
    CHECK_FALSE(valid_hidden(l));
    l = two_entries();
    l.items[1].title_id = l.items[0].title_id;
    CHECK_FALSE(valid_hidden(l));
    // Garbage past count is ignored.
    l = two_entries();
    l.items[5].visibility = 9;
    CHECK(valid_hidden(l));
}

TEST_CASE("hidden list conversions round trip and zero the unused items") {
    drpc::Settings s;
    s.hidden_titles = {{0x0100A0D02127E000ULL, drpc::TitleVisibility::Private},
                       {0x0100000000010000ULL, drpc::TitleVisibility::Hidden}};
    drpc::ipc::HiddenList wire;
    std::memset(&wire, 0xAB, sizeof wire);
    drpc::to_ipc(s, wire);
    CHECK(wire.count == 2);
    CHECK(wire.pad == 0);
    CHECK(wire.items[0].title_id == 0x0100A0D02127E000ULL);
    CHECK(wire.items[0].visibility == 1);
    CHECK(wire.items[1].visibility == 2);
    CHECK(wire.items[0].pad[0] == 0);
    CHECK(wire.items[2].title_id == 0);
    CHECK(wire.items[63].visibility == 0);
    drpc::Settings back;
    back.private_mode = true;
    drpc::apply_ipc(wire, back);
    CHECK(back.hidden_titles == s.hidden_titles);
    CHECK(back.private_mode);
}
