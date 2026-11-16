#include <doctest/doctest.h>

#include <cstring>
#include <string>

#include "core/ipc_convert.hpp"
#include "drpc/protocol.hpp"

TEST_CASE("ipc settings is 8 bytes") {
    CHECK(sizeof(drpc::ipc::Settings) == 8);
}

TEST_CASE("to_ipc and apply_ipc round trip the booleans") {
    drpc::Settings s;
    s.enabled = false;
    s.private_mode = true;
    s.show_elapsed = false;
    s.show_firmware = true;
    s.show_home_menu = true;
    s.debug_log = true;
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
