#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "core/nro_swap.hpp"

using namespace drpc::nro_swap;

namespace {

namespace fs = std::filesystem;

struct TempDir {
    TempDir() {
        static std::atomic<int> counter{0};
        path = fs::temp_directory_path() / ("nxdrpc-swap-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
    fs::path path;
};

void write(const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::binary) << s; }

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

TEST_CASE("heartbeat_roundtrip") {
    CHECK(parse_heartbeat(format_heartbeat(123456)) == 123456);
    CHECK(parse_heartbeat("42\n") == 42);
    CHECK_FALSE(parse_heartbeat(""));
    CHECK_FALSE(parse_heartbeat("abc"));
    CHECK_FALSE(parse_heartbeat("-5"));
    CHECK_FALSE(parse_heartbeat("99999999999999999999999"));
    CHECK_FALSE(parse_heartbeat("9999999999999999999"));
    CHECK(format_heartbeat(5).size() == format_heartbeat(9000000000).size());
}

TEST_CASE("swap_decision: only while a staged nro exists and the app is closed") {
    const int64_t now = 100'000;
    CHECK(decide(false, std::nullopt, now) == Action::None);
    CHECK(decide(false, now, now) == Action::None);
    // No heartbeat: closed.
    CHECK(decide(true, std::nullopt, now) == Action::Swap);
    // Fresh heartbeat: open.
    CHECK(decide(true, now - 1'000, now) == Action::Wait);
    CHECK(decide(true, now - kHeartbeatStaleMs + 1, now) == Action::Wait);
    // Stale: the app crashed or the console lost power.
    CHECK(decide(true, now - kHeartbeatStaleMs, now) == Action::Swap);
    // Written before a reboot: the clock restarted below it.
    CHECK(decide(true, now + 5'000, now) == Action::Swap);
}

TEST_CASE("swap_replaces_the_nro") {
    TempDir dir;
    const std::string nro = (dir.path / "app.nro").string();
    const std::string staged = nro + ".new";
    write(nro, "old");
    write(staged, "new");
    CHECK(swap(nro, staged) == SwapResult::Swapped);
    CHECK(read(nro) == "new");
    CHECK_FALSE(fs::exists(staged));

    // The old file already gone (an earlier try removed it, then the rename failed): still swapped.
    write(staged, "newer");
    fs::remove(nro);
    CHECK(swap(nro, staged) == SwapResult::Swapped);
    CHECK(read(nro) == "newer");
}

TEST_CASE("swap_keeps_staged_when_old_cannot_go") {
    if (::geteuid() == 0) return;  // root ignores the permission that stands for a lock
    TempDir dir;
    // A read-only directory stands for a file Horizon will not let go of: nothing can be moved.
    const fs::path sub = dir.path / "app";
    fs::create_directories(sub);
    const std::string nro = (sub / "app.nro").string();
    const std::string staged = (dir.path / "app.nro.new").string();
    write(nro, "old");
    write(staged, "new");
    fs::permissions(sub, fs::perms::owner_read | fs::perms::owner_exec);
    CHECK(swap(nro, staged) == SwapResult::Locked);
    fs::permissions(sub, fs::perms::owner_all);
    CHECK(read(nro) == "old");
    CHECK(read(staged) == "new");
}

TEST_CASE("swap_restores_the_old_file_when_the_rename_fails") {
    TempDir dir;
    const std::string nro = (dir.path / "app.nro").string();
    write(nro, "old");
    // No staged file: the second rename fails.
    CHECK(swap(nro, nro + ".new") == SwapResult::Failed);
    CHECK(read(nro) == "old");
    CHECK_FALSE(fs::exists(nro + ".old"));
}

TEST_CASE("read_heartbeat_file") {
    TempDir dir;
    const fs::path hb = dir.path / "app.running";
    CHECK_FALSE(read_heartbeat(hb.string()));
    write(hb, format_heartbeat(777));
    CHECK(read_heartbeat(hb.string()) == 777);
}
