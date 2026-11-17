#include <doctest/doctest.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unistd.h>

#include "boot_flag.hpp"

using namespace app;

namespace {

namespace fs = std::filesystem;

// A fresh directory under the system temp dir, removed when the test ends. It stands for the
// program's contents directory: the flag lives at <dir>/flags/boot2.flag.
struct TempDir {
    TempDir() {
        static std::atomic<int> counter{0};
        path = fs::temp_directory_path() / ("nxdrpc-bootflag-test-" + std::to_string(::getpid()) + "-" +
                                            std::to_string(counter++));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
    std::string flag() const { return (path / "flags" / "boot2.flag").string(); }
    fs::path path;
};

}  // namespace

TEST_CASE("boot flag: missing file reads as off") {
    TempDir dir;
    CHECK_FALSE(boot_flag_enabled(dir.flag()));
}

TEST_CASE("boot flag: turning it on creates the flags dir and an empty file") {
    TempDir dir;
    REQUIRE_FALSE(fs::exists(dir.path / "flags"));
    CHECK(set_boot_flag(dir.flag(), true));
    CHECK(fs::is_directory(dir.path / "flags"));
    CHECK(fs::is_regular_file(dir.flag()));
    CHECK(fs::file_size(dir.flag()) == 0);
    CHECK(boot_flag_enabled(dir.flag()));
}

TEST_CASE("boot flag: turning it on works when the flags dir already exists") {
    TempDir dir;
    fs::create_directory(dir.path / "flags");
    CHECK(set_boot_flag(dir.flag(), true));
    CHECK(boot_flag_enabled(dir.flag()));
}

TEST_CASE("boot flag: turning it off removes the file") {
    TempDir dir;
    REQUIRE(set_boot_flag(dir.flag(), true));
    CHECK(set_boot_flag(dir.flag(), false));
    CHECK_FALSE(fs::exists(dir.flag()));
    CHECK_FALSE(boot_flag_enabled(dir.flag()));
}

TEST_CASE("boot flag: setting the current state again succeeds") {
    TempDir dir;
    CHECK(set_boot_flag(dir.flag(), false));
    CHECK_FALSE(boot_flag_enabled(dir.flag()));
    REQUIRE(set_boot_flag(dir.flag(), true));
    CHECK(set_boot_flag(dir.flag(), true));
    CHECK(boot_flag_enabled(dir.flag()));
    REQUIRE(set_boot_flag(dir.flag(), false));
    CHECK(set_boot_flag(dir.flag(), false));
    CHECK_FALSE(boot_flag_enabled(dir.flag()));
}

TEST_CASE("boot flag: turning it off when the flags dir is missing succeeds") {
    TempDir dir;
    CHECK(set_boot_flag(dir.flag(), false));
    CHECK_FALSE(fs::exists(dir.path / "flags"));
}

TEST_CASE("boot flag: a regular file in place of the flags dir makes turning it on fail") {
    TempDir dir;
    std::FILE* f = std::fopen((dir.path / "flags").string().c_str(), "wb");
    REQUIRE(f);
    std::fclose(f);
    CHECK_FALSE(set_boot_flag(dir.flag(), true));
    CHECK_FALSE(boot_flag_enabled(dir.flag()));
    CHECK(fs::is_regular_file(dir.path / "flags"));
}

TEST_CASE("boot flag: a missing contents dir makes turning it on fail") {
    TempDir dir;
    const std::string flag = (dir.path / "missing" / "flags" / "boot2.flag").string();
    CHECK_FALSE(set_boot_flag(flag, true));
    CHECK_FALSE(boot_flag_enabled(flag));
}

TEST_CASE("boot flag: a directory in place of the file reads as off and cannot be removed as the flag") {
    TempDir dir;
    fs::create_directories(dir.flag());
    CHECK_FALSE(boot_flag_enabled(dir.flag()));
    CHECK_FALSE(set_boot_flag(dir.flag(), true));
}

TEST_CASE("boot flag: the desktop path stays under the system temp dir") {
    const std::string path = boot_flag_path();
    REQUIRE_FALSE(path.empty());
    CHECK(fs::path(path).filename() == "boot2.flag");
    const std::string temp = fs::temp_directory_path().string();
    CHECK(path.compare(0, temp.size(), temp) == 0);
}
