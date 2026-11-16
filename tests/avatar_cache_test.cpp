#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <unistd.h>
#include <string>
#include <vector>

#include "avatar_cache.hpp"

using namespace app;

namespace {

namespace fs = std::filesystem;

const std::vector<uint8_t> kPng = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R'};

// A fresh directory under the system temp dir, removed when the test ends.
struct TempDir {
    TempDir() {
        static std::atomic<int> counter{0};
        path = fs::temp_directory_path() / ("nxdrpc-avatar-test-" + std::to_string(::getpid()) + "-" +
                                            std::to_string(counter++));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
    fs::path path;
};

}  // namespace

TEST_CASE("avatar cache key") {
    CHECK(avatar_cache_key("1", "") == "1:default");
    CHECK(avatar_cache_key("1", "0123456789abcdef0123456789abcdef") == "1:0123456789abcdef0123456789abcdef");
}

TEST_CASE("is_png checks the 8-byte signature") {
    CHECK(is_png(kPng));
    CHECK(is_png(std::span<const uint8_t>(kPng.data(), 8)));
    const std::array<uint8_t, 8> jpeg = {0xFF, 0xD8, 0xFF, 0xE0, 0, 0x10, 'J', 'F'};
    CHECK_FALSE(is_png(jpeg));
    CHECK_FALSE(is_png(std::span<const uint8_t>(kPng.data(), 7)));
}

TEST_CASE("avatar cache stores and loads by key") {
    TempDir dir;
    AvatarCache cache(dir.path.string());
    CHECK_FALSE(cache.load("1:default"));
    REQUIRE(cache.store("1:default", kPng));
    CHECK(fs::exists(dir.path / "avatar.png"));
    CHECK(fs::exists(dir.path / "avatar.key"));
    auto hit = cache.load("1:default");
    REQUIRE(hit);
    CHECK(*hit == kPng);
    CHECK_FALSE(cache.load("2:default"));
    CHECK_FALSE(cache.load("1:"));
}

TEST_CASE("avatar cache replaces the previous picture") {
    TempDir dir;
    AvatarCache cache(dir.path.string());
    REQUIRE(cache.store("1:default", kPng));
    std::vector<uint8_t> other = kPng;
    other.push_back(42);
    REQUIRE(cache.store("2:default", other));
    CHECK_FALSE(cache.load("1:default"));
    auto hit = cache.load("2:default");
    REQUIRE(hit);
    CHECK(*hit == other);
    CHECK_FALSE(fs::exists(dir.path / "avatar.png.tmp"));
    CHECK_FALSE(fs::exists(dir.path / "avatar.key.tmp"));
}

TEST_CASE("avatar cache never returns bytes that are not a PNG") {
    TempDir dir;
    AvatarCache cache(dir.path.string());
    const std::vector<uint8_t> jpeg = {0xFF, 0xD8, 0xFF, 0xE0, 0, 0x10, 'J', 'F', 'I', 'F'};
    cache.store("1:default", jpeg);
    CHECK_FALSE(cache.load("1:default"));
}

TEST_CASE("avatar cache clear removes both files") {
    TempDir dir;
    AvatarCache cache(dir.path.string());
    REQUIRE(cache.store("1:default", kPng));
    cache.clear();
    CHECK_FALSE(fs::exists(dir.path / "avatar.png"));
    CHECK_FALSE(fs::exists(dir.path / "avatar.key"));
    CHECK_FALSE(cache.load("1:default"));
}

TEST_CASE("avatar cache creates its directory") {
    TempDir dir;
    AvatarCache cache((dir.path / "sub").string());
    REQUIRE(cache.store("1:default", kPng));
    CHECK(cache.load("1:default"));
}
