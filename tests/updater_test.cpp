#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "drpc/sha256.hpp"
#include "fake_https.hpp"
#include "updater.hpp"

using namespace app::update;

namespace {

namespace fs = std::filesystem;

struct TempDir {
    TempDir() {
        static std::atomic<int> counter{0};
        path = fs::temp_directory_path() / ("nxdrpc-update-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
    fs::path path;
};

void write(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::string hex(const std::string& s) {
    return drpc::to_hex(drpc::Sha256::of({reinterpret_cast<const uint8_t*>(s.data()), s.size()}));
}

const std::string kDl = "https://github.com/0xsharkboy/nx-discord-rpc/releases/download/v0.6.0/";

std::string asset_json(const std::string& name, size_t size) {
    return R"({"url":"https://api.github.com/repos/0xsharkboy/nx-discord-rpc/releases/assets/1","id":1,"name":")" + name +
           R"(","label":"","uploader":{"login":"github-actions[bot]","id":4},"content_type":"application/octet-stream","state":"uploaded","size":)" +
           std::to_string(size) + R"(,"download_count":3,"browser_download_url":")" + kDl + name + R"("})";
}

std::string release_json(const std::string& tag, const std::vector<std::pair<std::string, size_t>>& assets,
                         bool prerelease = false) {
    std::string j = R"({"url":"https://api.github.com/repos/0xsharkboy/nx-discord-rpc/releases/1","html_url":"x","id":1,
"author":{"login":"0xsharkboy","id":2,"type":"User"},"tag_name":")" + tag +
                    R"(","target_commitish":"main","name":")" + tag + R"(","draft":false,"prerelease":)" +
                    (prerelease ? "true" : "false") + R"(,"assets":[)";
    for (size_t i = 0; i < assets.size(); ++i) {
        if (i) j += ",";
        j += asset_json(assets[i].first, assets[i].second);
    }
    j += R"(],"body":"Notes with \"quotes\" and a é."})";
    return j;
}

// The files of a fake v0.6.0 release, served through FakeHttps with GitHub's redirect to the blob host.
struct FakeRelease {
    std::map<std::string, std::string> files = {
        {"nx-discord-rpc.ovl", "overlay v0.6.0"},
        {"toolbox.json", "{\"name\":\"nx-discord-rpc\"}"},
        {"exefs.nsp", std::string(5000, 'S')},
        {"nx-discord-rpc.nro", std::string(20000, 'N')},
    };
    std::string zip = "zipbytes";
    test::FakeHttps t;
    Config config;

    explicit FakeRelease(const fs::path& root) {
        config.root = root.string();
        config.user_agent = "nx-discord-rpc/0.5.0";
    }

    std::string sums() const {
        std::string s = hex(zip) + "  nx-discord-rpc-0.6.0.zip\n";
        for (const auto& [name, data] : files) s += hex(data) + "  " + name + "\n";
        return s;
    }

    void serve(const std::string& sums_text) {
        std::vector<std::pair<std::string, size_t>> assets = {{"nx-discord-rpc-0.6.0.zip", zip.size()},
                                                              {"SHA256SUMS", sums_text.size()}};
        for (const auto& [name, data] : files) assets.emplace_back(name, data.size());
        t.responses["api.github.com/repos/0xsharkboy/nx-discord-rpc/releases/latest"] =
            test::ok(release_json("v0.6.0", assets));
        int n = 0;
        auto blob = [&](const std::string& name, const std::string& data) {
            const std::string target = "/github-production-release-asset/" + std::to_string(n++) + "?X-Amz-Signature=ab";
            t.responses["github.com/0xsharkboy/nx-discord-rpc/releases/download/v0.6.0/" + name] =
                test::redirect("https://objects.githubusercontent.com" + target);
            t.responses["objects.githubusercontent.com" + target] = test::ok(data);
        };
        blob("SHA256SUMS", sums_text);
        for (const auto& [name, data] : files) blob(name, data);
    }

    Fetch fetch() {
        return [this](const std::string& url, const app::http::Options& o, const app::http::Sink& sink) {
            return app::http::get(t, url, o, sink);
        };
    }
};

// Files already on the SD card before the update.
void install_old(const fs::path& root) {
    write(root / "switch/.overlays/nx-discord-rpc.ovl", "overlay old");
    write(root / "atmosphere/contents/4200000000D15C0D/toolbox.json", "toolbox old");
    write(root / "atmosphere/contents/4200000000D15C0D/exefs.nsp", "exefs old");
    write(root / "switch/nx-discord-rpc/nx-discord-rpc.nro", "nro old");
}

}  // namespace

TEST_CASE("version_compare_numeric") {
    CHECK(is_newer("v0.5.0", "0.4.0"));
    CHECK(is_newer("v0.10.0", "0.9.9"));
    CHECK(is_newer("v0.10.0", "0.9.1"));
    CHECK(is_newer("v1.0.0", "0.99.99"));
    CHECK(is_newer("v0.4.1", "0.4.0"));
    CHECK_FALSE(is_newer("v0.4.0", "0.4.0"));
    CHECK_FALSE(is_newer("v0.3.9", "0.4.0"));
    CHECK_FALSE(is_newer("v0.9.0", "0.10.0"));
    // Prereleases and malformed tags never count.
    CHECK_FALSE(is_newer("v0.6.0-rc1", "0.4.0"));
    CHECK_FALSE(is_newer("v0.6.0+build", "0.4.0"));
    CHECK_FALSE(is_newer("0.6.0", "0.4.0"));
    CHECK_FALSE(is_newer("v0.6", "0.4.0"));
    CHECK_FALSE(is_newer("v0.6.0.1", "0.4.0"));
    CHECK_FALSE(is_newer("v.6.0", "0.4.0"));
    CHECK_FALSE(is_newer("v0..0", "0.4.0"));
    CHECK_FALSE(is_newer("V0.6.0", "0.4.0"));
    CHECK_FALSE(is_newer("v0.6.0 ", "0.4.0"));
    CHECK_FALSE(is_newer("v-1.6.0", "0.4.0"));
    CHECK_FALSE(is_newer("v99999999999.0.0", "0.4.0"));
    CHECK_FALSE(is_newer("", "0.4.0"));
    // A broken app version never offers anything.
    CHECK_FALSE(is_newer("v0.6.0", "dev"));
    CHECK(parse_version("v1.2.3", true) == Version{1, 2, 3});
    CHECK(parse_version("1.2.3", false) == Version{1, 2, 3});
    CHECK_FALSE(parse_version("1.2.3", true));
    CHECK_FALSE(parse_version("v1.2.3", false));
    CHECK(parse_version("v01.2.3", true) == Version{1, 2, 3});
}

TEST_CASE("release_json_assets") {
    const std::string json = release_json("v0.6.0", {{"nx-discord-rpc-0.6.0.zip", 10},
                                                     {"SHA256SUMS", 400},
                                                     {"exefs.nsp", 251797},
                                                     {"nx-discord-rpc.nro", 2713430},
                                                     {"nx-discord-rpc.ovl", 294968},
                                                     {"toolbox.json", 94}});
    const auto r = parse_release(json);
    REQUIRE(r);
    CHECK(r->tag == "v0.6.0");
    CHECK_FALSE(r->prerelease);
    CHECK_FALSE(r->draft);
    REQUIRE(r->assets.size() == 6);
    CHECK(r->assets.at("exefs.nsp").url == kDl + "exefs.nsp");
    CHECK(r->assets.at("nx-discord-rpc.nro").size == 2713430);
    CHECK(r->installable());
    CHECK(r->total_size() == 251797 + 2713430 + 294968 + 94);

    // Byte-at-a-time feeding gives the same result.
    ReleaseReader reader;
    for (char c : json) reader.feed(std::string_view(&c, 1));
    const auto r2 = reader.finish();
    REQUIRE(r2);
    CHECK(r2->assets.size() == 6);

    // v0.4.0 and older: only the zip and SHA256SUMS.
    const auto old = parse_release(release_json("v0.4.0", {{"nx-discord-rpc-0.4.0.zip", 10}, {"SHA256SUMS", 90}}));
    REQUIRE(old);
    CHECK_FALSE(old->installable());

    // A download URL outside GitHub makes the release not installable.
    std::string evil = json;
    const std::string from = kDl + "exefs.nsp";
    evil.replace(evil.find(from), from.size(), "https://evil.example/exefs.nsp");
    CHECK_FALSE(parse_release(evil)->installable());

    CHECK(parse_release(release_json("v0.6.0-rc1", {}, true))->prerelease);
    CHECK_FALSE(parse_release("{\"message\":\"Not Found\"}"));
    CHECK_FALSE(parse_release("not json"));
    CHECK_FALSE(parse_release("{\"tag_name\":\"v1.0.0\""));
}

TEST_CASE("sha256sums_parse") {
    const std::string a(64, 'a'), b(64, 'b');
    const std::string upper = "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789";
    auto sums = parse_sha256sums(a + "  exefs.nsp\n" + b + " *nx-discord-rpc.nro\r\n" + upper +
                                 "  toolbox.json\n\nnot a line\n" + std::string(63, 'c') + "  short.bin\n" +
                                 std::string(64, 'g') + "  nothex.bin\n" + a + "  exefs.nsp\n");
    REQUIRE(sums);
    CHECK(sums->size() == 3);
    CHECK(sums->at("exefs.nsp") == a);
    CHECK(sums->at("nx-discord-rpc.nro") == b);
    CHECK(sums->at("toolbox.json") == "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
    CHECK_FALSE(parse_sha256sums(a + "  exefs.nsp\n" + b + "  exefs.nsp\n"));
    CHECK(parse_sha256sums("")->empty());
}

TEST_CASE("check_reports_each_outcome") {
    TempDir dir;
    FakeRelease rel(dir.path);
    rel.serve(rel.sums());
    CheckResult r = check(rel.fetch(), rel.config, "0.5.0");
    CHECK(r.outcome == CheckOutcome::Available);
    CHECK(r.release.tag == "v0.6.0");
    REQUIRE(rel.t.heads.size() == 1);
    CHECK(rel.t.heads[0].find("GET /repos/0xsharkboy/nx-discord-rpc/releases/latest HTTP/1.1\r\n") == 0);
    CHECK(rel.t.heads[0].find("Accept: application/vnd.github+json\r\n") != std::string::npos);

    CHECK(check(rel.fetch(), rel.config, "0.6.0").outcome == CheckOutcome::UpToDate);
    CHECK(check(rel.fetch(), rel.config, "0.7.0").outcome == CheckOutcome::UpToDate);

    // The base URL and repo are injectable.
    Config other = rel.config;
    other.repo = "someone/fork";
    CHECK(check(rel.fetch(), other, "0.5.0").outcome == CheckOutcome::Failed);  // 404 from the fixture
    CHECK(rel.t.requests.back() == "api.github.com /repos/someone/fork/releases/latest");

    // An older-style release (zip only): newer, but manual.
    rel.t.responses["api.github.com/repos/0xsharkboy/nx-discord-rpc/releases/latest"] =
        test::ok(release_json("v0.6.0", {{"nx-discord-rpc-0.6.0.zip", 10}, {"SHA256SUMS", 90}}));
    r = check(rel.fetch(), rel.config, "0.5.0");
    CHECK(r.outcome == CheckOutcome::ManualOnly);
    CHECK(r.release.tag == "v0.6.0");

    // A prerelease served as latest is ignored.
    rel.t.responses["api.github.com/repos/0xsharkboy/nx-discord-rpc/releases/latest"] =
        test::ok(release_json("v0.6.0-rc1", {}, true));
    CHECK(check(rel.fetch(), rel.config, "0.5.0").outcome == CheckOutcome::UpToDate);

    rel.t.responses["api.github.com/repos/0xsharkboy/nx-discord-rpc/releases/latest"] =
        "HTTP/1.1 403 Forbidden\r\nContent-Length: 2\r\n\r\n{}";
    CHECK(check(rel.fetch(), rel.config, "0.5.0").outcome == CheckOutcome::Failed);
    rel.t.responses["api.github.com/repos/0xsharkboy/nx-discord-rpc/releases/latest"] =
        test::ok(std::string(kMaxApiBytes + 1, ' '));
    CHECK(check(rel.fetch(), rel.config, "0.5.0").outcome == CheckOutcome::Failed);
}

TEST_CASE("install_replaces_every_file") {
    TempDir dir;
    install_old(dir.path);
    FakeRelease rel(dir.path);
    rel.serve(rel.sums());
    const Release release = check(rel.fetch(), rel.config, "0.5.0").release;

    std::vector<std::string> events;
    uint64_t last_done = 0, last_total = 0;
    InstallHooks hooks;
    hooks.progress = [&](uint64_t done, uint64_t total) {
        CHECK(done >= last_done);
        last_done = done;
        last_total = total;
    };
    hooks.stop_sysmodule = [&] {
        events.push_back("stop:" + read(dir.path / "atmosphere/contents/4200000000D15C0D/exefs.nsp"));
    };
    hooks.start_sysmodule = [&] {
        events.push_back("start:" + read(dir.path / "atmosphere/contents/4200000000D15C0D/exefs.nsp").substr(0, 1));
        return true;
    };
    const InstallResult r = install(rel.fetch(), rel.config, release, hooks);
    CHECK(r.outcome == InstallOutcome::Done);
    CHECK(r.sysmodule_started);
    CHECK(read(dir.path / "switch/.overlays/nx-discord-rpc.ovl") == "overlay v0.6.0");
    CHECK(read(dir.path / "atmosphere/contents/4200000000D15C0D/toolbox.json") == rel.files["toolbox.json"]);
    CHECK(read(dir.path / "atmosphere/contents/4200000000D15C0D/exefs.nsp") == rel.files["exefs.nsp"]);
    // The running app's nro is only staged, for the sysmodule to swap in.
    CHECK(read(dir.path / "switch/nx-discord-rpc/nx-discord-rpc.nro") == "nro old");
    CHECK(read(dir.path / "switch/nx-discord-rpc/nx-discord-rpc.nro.new") == rel.files["nx-discord-rpc.nro"]);
    CHECK(events == std::vector<std::string>{"stop:exefs old", "start:S"});
    CHECK(last_total == release.total_size());
    CHECK(last_done == last_total);
    for (const char* leftover : {"switch/.overlays/nx-discord-rpc.ovl.new",
                                 "atmosphere/contents/4200000000D15C0D/toolbox.json.new",
                                 "atmosphere/contents/4200000000D15C0D/exefs.nsp.new",
                                 "switch/nx-discord-rpc/nx-discord-rpc.nro.part"})
        CHECK_FALSE(fs::exists(dir.path / leftover));
}

TEST_CASE("install_creates_missing_overlay_dir") {
    TempDir dir;
    install_old(dir.path);
    fs::remove_all(dir.path / "switch/.overlays");
    FakeRelease rel(dir.path);
    rel.serve(rel.sums());
    const Release release = check(rel.fetch(), rel.config, "0.5.0").release;
    CHECK(install(rel.fetch(), rel.config, release, {}).outcome == InstallOutcome::Done);
    CHECK(read(dir.path / "switch/.overlays/nx-discord-rpc.ovl") == "overlay v0.6.0");
}

TEST_CASE("failed_checksum_replaces_nothing") {
    TempDir dir;
    install_old(dir.path);
    FakeRelease rel(dir.path);
    // The last file downloaded (the nro) does not match its line in SHA256SUMS.
    std::string sums = rel.sums();
    const std::string good = hex(rel.files["nx-discord-rpc.nro"]);
    sums.replace(sums.find(good), good.size(), std::string(64, '0'));
    rel.serve(sums);
    const Release release = check(rel.fetch(), rel.config, "0.5.0").release;
    bool stopped = false;
    InstallHooks hooks;
    hooks.stop_sysmodule = [&] { stopped = true; };
    const InstallResult r = install(rel.fetch(), rel.config, release, hooks);
    CHECK(r.outcome == InstallOutcome::Failed);
    CHECK_FALSE(stopped);
    CHECK(read(dir.path / "switch/.overlays/nx-discord-rpc.ovl") == "overlay old");
    CHECK(read(dir.path / "atmosphere/contents/4200000000D15C0D/toolbox.json") == "toolbox old");
    CHECK(read(dir.path / "atmosphere/contents/4200000000D15C0D/exefs.nsp") == "exefs old");
    CHECK(read(dir.path / "switch/nx-discord-rpc/nx-discord-rpc.nro") == "nro old");
    CHECK_FALSE(fs::exists(dir.path / "switch/nx-discord-rpc/nx-discord-rpc.nro.new"));
    CHECK_FALSE(fs::exists(dir.path / "switch/nx-discord-rpc/nx-discord-rpc.nro.part"));
    CHECK_FALSE(fs::exists(dir.path / "switch/.overlays/nx-discord-rpc.ovl.new"));
    CHECK_FALSE(fs::exists(dir.path / "atmosphere/contents/4200000000D15C0D/exefs.nsp.new"));
}

TEST_CASE("install_fails_cleanly") {
    TempDir dir;
    install_old(dir.path);
    FakeRelease rel(dir.path);
    rel.serve(rel.sums());
    const Release release = check(rel.fetch(), rel.config, "0.5.0").release;

    SUBCASE("a file missing from SHA256SUMS") {
        std::string sums = rel.sums();
        const size_t at = sums.find("  toolbox.json\n");
        sums.erase(sums.rfind('\n', at) + 1, at + 15 - (sums.rfind('\n', at) + 1));
        rel.serve(sums);
    }
    SUBCASE("a download fails") {
        rel.t.responses.erase("github.com/0xsharkboy/nx-discord-rpc/releases/download/v0.6.0/exefs.nsp");
    }
    SUBCASE("a file over its cap") {
        rel.files["toolbox.json"] = std::string(max_file_bytes("toolbox.json") + 1, 't');
        rel.serve(rel.sums());
    }
    const InstallResult r = install(rel.fetch(), rel.config, release, {});
    CHECK(r.outcome == InstallOutcome::Failed);
    CHECK(read(dir.path / "atmosphere/contents/4200000000D15C0D/exefs.nsp") == "exefs old");
    CHECK(read(dir.path / "atmosphere/contents/4200000000D15C0D/toolbox.json") == "toolbox old");
    CHECK_FALSE(fs::exists(dir.path / "switch/nx-discord-rpc/nx-discord-rpc.nro.new"));
}

TEST_CASE("install_cancel_changes_nothing") {
    TempDir dir;
    install_old(dir.path);
    FakeRelease rel(dir.path);
    rel.serve(rel.sums());
    const Release release = check(rel.fetch(), rel.config, "0.5.0").release;
    InstallHooks hooks;
    int calls = 0;
    hooks.cancelled = [&] { return ++calls > 5; };
    CHECK(install(rel.fetch(), rel.config, release, hooks).outcome == InstallOutcome::Failed);
    CHECK(read(dir.path / "atmosphere/contents/4200000000D15C0D/exefs.nsp") == "exefs old");
    CHECK_FALSE(fs::exists(dir.path / "atmosphere/contents/4200000000D15C0D/exefs.nsp.new"));
}

TEST_CASE("install_partial_when_a_replace_fails") {
    TempDir dir;
    install_old(dir.path);
    if (::geteuid() == 0) return;  // root ignores the permission that stands for a lock
    FakeRelease rel(dir.path);
    rel.serve(rel.sums());
    const Release release = check(rel.fetch(), rel.config, "0.5.0").release;
    std::vector<std::string> events;
    InstallHooks hooks;
    // exefs.nsp cannot be moved (a read-only directory stands for a locked file); the overlay and toolbox
    // before it were already replaced.
    const fs::path contents = dir.path / "atmosphere/contents/4200000000D15C0D";
    hooks.stop_sysmodule = [&] {
        events.push_back("stop");
        fs::permissions(contents, fs::perms::owner_read | fs::perms::owner_exec);
    };
    hooks.start_sysmodule = [&] {
        events.push_back("start");
        fs::permissions(contents, fs::perms::owner_all);
        return true;
    };
    const InstallResult r = install(rel.fetch(), rel.config, release, hooks);
    CHECK(r.outcome == InstallOutcome::Partial);
    // The sysmodule is started again even though its file could not be replaced.
    CHECK(events == std::vector<std::string>{"stop", "start"});
    CHECK(read(contents / "exefs.nsp") == "exefs old");
    CHECK_FALSE(fs::exists(contents / "exefs.nsp.new"));
    CHECK(read(dir.path / "switch/.overlays/nx-discord-rpc.ovl") == "overlay v0.6.0");
    CHECK_FALSE(fs::exists(dir.path / "switch/nx-discord-rpc/nx-discord-rpc.nro.new"));
}

TEST_CASE("clean_leftovers_keeps_the_staged_nro") {
    TempDir dir;
    Config config;
    config.root = dir.path.string();
    write(dir.path / "switch/.overlays/nx-discord-rpc.ovl.new", "x");
    write(dir.path / "atmosphere/contents/4200000000D15C0D/exefs.nsp.new", "x");
    write(dir.path / "switch/nx-discord-rpc/nx-discord-rpc.nro.part", "x");
    write(dir.path / "switch/nx-discord-rpc/nx-discord-rpc.nro.new", "verified");
    clean_leftovers(config);
    CHECK_FALSE(fs::exists(dir.path / "switch/.overlays/nx-discord-rpc.ovl.new"));
    CHECK_FALSE(fs::exists(dir.path / "atmosphere/contents/4200000000D15C0D/exefs.nsp.new"));
    CHECK_FALSE(fs::exists(dir.path / "switch/nx-discord-rpc/nx-discord-rpc.nro.part"));
    CHECK(fs::exists(dir.path / "switch/nx-discord-rpc/nx-discord-rpc.nro.new"));
}

TEST_CASE("install_reports_a_sysmodule_that_did_not_start") {
    TempDir dir;
    install_old(dir.path);
    FakeRelease rel(dir.path);
    rel.serve(rel.sums());
    const Release release = check(rel.fetch(), rel.config, "0.5.0").release;
    InstallHooks hooks;
    hooks.start_sysmodule = [] { return false; };
    const InstallResult r = install(rel.fetch(), rel.config, release, hooks);
    // Every file is in place; only the restart failed, which the app reports (Start now, or reboot).
    CHECK(r.outcome == InstallOutcome::Done);
    CHECK_FALSE(r.sysmodule_started);
    CHECK(read(dir.path / "atmosphere/contents/4200000000D15C0D/exefs.nsp") == rel.files["exefs.nsp"]);
    CHECK(read(dir.path / "switch/nx-discord-rpc/nx-discord-rpc.nro.new") == rel.files["nx-discord-rpc.nro"]);
}

TEST_CASE("install_with_the_sysmodule_not_running") {
    TempDir dir;
    install_old(dir.path);
    FakeRelease rel(dir.path);
    rel.serve(rel.sums());
    const Release release = check(rel.fetch(), rel.config, "0.5.0").release;
    // From the not-running screen: stopping finds nothing (a no-op), starting launches it.
    bool launched = false;
    InstallHooks hooks;
    hooks.stop_sysmodule = [] {};
    hooks.start_sysmodule = [&] {
        launched = true;
        return true;
    };
    const InstallResult r = install(rel.fetch(), rel.config, release, hooks);
    CHECK(r.outcome == InstallOutcome::Done);
    CHECK(r.sysmodule_started);
    CHECK(launched);
}

TEST_CASE("check_stops_when_cancelled") {
    TempDir dir;
    FakeRelease rel(dir.path);
    rel.serve(rel.sums());
    CHECK(check(rel.fetch(), rel.config, "0.5.0", [] { return true; }).outcome == CheckOutcome::Failed);
    CHECK(check(rel.fetch(), rel.config, "0.5.0", [] { return false; }).outcome == CheckOutcome::Available);
}

TEST_CASE("start_with_a_staged_update_offers_nothing") {
    TempDir dir;
    Config config;
    config.root = dir.path.string();
    CHECK_FALSE(staged_ready(config));
    CHECK(start_action(staged_ready(config), true) == StartAction::Check);
    CHECK(start_action(staged_ready(config), false) == StartAction::None);

    // After an install, until the sysmodule swaps the .nro in: the reopened app is the old one, so it must not
    // check (and offer the same update again), even with Check for updates off it says to wait.
    write(dir.path / "switch/nx-discord-rpc/nx-discord-rpc.nro.new", "verified");
    CHECK(staged_ready(config));
    CHECK(start_action(staged_ready(config), true) == StartAction::Ready);
    CHECK(start_action(staged_ready(config), false) == StartAction::Ready);

    // A download in progress (.part) is not a staged update.
    fs::remove(dir.path / "switch/nx-discord-rpc/nx-discord-rpc.nro.new");
    write(dir.path / "switch/nx-discord-rpc/nx-discord-rpc.nro.part", "partial");
    CHECK_FALSE(staged_ready(config));
}
