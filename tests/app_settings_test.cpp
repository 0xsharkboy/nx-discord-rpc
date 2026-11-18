#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "app_settings.hpp"

using namespace app;

TEST_CASE("app_settings_parse: defaults, values and junk") {
    CHECK(parse_app_settings("").check_updates);
    CHECK_FALSE(parse_app_settings("check_updates=false\n").check_updates);
    CHECK_FALSE(parse_app_settings("# comment\r\n  check_updates = 0 \r\n").check_updates);
    CHECK(parse_app_settings("check_updates=false\ncheck_updates=true\n").check_updates);
    CHECK(parse_app_settings("check_updates=maybe\n").check_updates);
    CHECK(parse_app_settings("other=false\n[section]\n=x\n").check_updates);
    CHECK_FALSE(parse_app_settings("; note\ncheck_updates=off").check_updates);
}

TEST_CASE("app_settings_roundtrip: save then load, parent dir created") {
    static std::atomic<int> counter{0};
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() /
                         ("nxdrpc-appini-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
    fs::remove_all(dir);
    const std::string path = (dir / "app.ini").string();
    CHECK(load_app_settings(path).check_updates);  // missing file: defaults

    fs::create_directories(dir);
    CHECK(save_app_settings(path, AppSettings{false}));
    CHECK_FALSE(load_app_settings(path).check_updates);
    CHECK(save_app_settings(path, AppSettings{true}));
    CHECK(load_app_settings(path).check_updates);
    CHECK_FALSE(fs::exists(path + ".tmp"));
    fs::remove_all(dir);

    CHECK(save_app_settings(path, AppSettings{false}));  // dir missing: created
    CHECK_FALSE(load_app_settings(path).check_updates);
    fs::remove_all(dir);
}
