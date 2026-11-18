#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "update_http.hpp"

// The updater's pure parts: version compare, release and SHA256SUMS parsing, and the install itself over an
// injected fetcher and file system root, so host tests run it against fixtures in a temp dir.
namespace app::update {

struct Version {
    uint32_t major = 0, minor = 0, patch = 0;
    auto operator<=>(const Version&) const = default;
};

// "vX.Y.Z" (tags) or "X.Y.Z" (the app's own version) with plain decimal numbers. Prereleases ("-rc1"),
// build metadata and anything else give nothing.
std::optional<Version> parse_version(std::string_view text, bool tag);

// True only when tag is a well-formed vX.Y.Z above the app's version.
bool is_newer(std::string_view tag, std::string_view app_version);

// The release asset names the app installs from, besides SHA256SUMS; CI publishes exactly these.
inline constexpr std::array<std::string_view, 4> kFiles = {
    "nx-discord-rpc.ovl", "toolbox.json", "exefs.nsp", "nx-discord-rpc.nro"};
inline constexpr std::string_view kSums = "SHA256SUMS";

// Caps (amendment): the API answer, SHA256SUMS, then each file.
inline constexpr size_t kMaxApiBytes = 256 * 1024;
inline constexpr size_t kMaxSumsBytes = 64 * 1024;
size_t max_file_bytes(std::string_view name);

struct Asset {
    std::string url;
    uint64_t size = 0;
};

struct Release {
    std::string tag;
    bool prerelease = false;
    bool draft = false;
    std::map<std::string, Asset, std::less<>> assets;

    // Every file in kFiles and SHA256SUMS is attached with an allowed https URL.
    bool installable() const;
    // Sum of the kFiles sizes the API reported, for progress.
    uint64_t total_size() const;
};

// Reads GET /repos/<repo>/releases/latest incrementally; feed it the body, then call finish().
class ReleaseReader {
public:
    ReleaseReader();
    void feed(std::string_view chunk);
    std::optional<Release> finish();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

std::optional<Release> parse_release(std::string_view json);

// "<64 hex>  name" or "<64 hex> *name" lines (sha256sum output). Unknown or malformed lines are skipped;
// a name listed twice with different hashes makes the whole file invalid.
std::optional<std::map<std::string, std::string, std::less<>>> parse_sha256sums(std::string_view text);

struct Config {
    std::string api_base = "https://api.github.com";
    std::string repo = "0xsharkboy/nx-discord-rpc";
    std::string user_agent;
    // Prefix for the SD card paths: "sdmc:" on the console, a temp dir in tests.
    std::string root = "sdmc:";

    std::string latest_url() const { return api_base + "/repos/" + repo + "/releases/latest"; }
};

// Where a release file goes. The app's own .nro is only staged: the sysmodule swaps it in once the app
// has closed (see core/nro_swap.hpp).
struct Target {
    std::string_view name;
    std::string path;     // the installed file
    std::string staged;   // the downloaded and verified copy
};
Target target_for(const Config& config, std::string_view name);

// GET url with these options (user agent, accept, cap), streaming into sink. Real builds use https over TLS;
// tests use fixtures.
using Fetch = std::function<http::Result(const std::string& url, const http::Options& options, const http::Sink& sink)>;

enum class CheckOutcome { UpToDate, Available, ManualOnly, Failed };

struct CheckResult {
    CheckOutcome outcome = CheckOutcome::Failed;
    Release release;  // set for Available and ManualOnly
};

// cancelled, when set, is checked as the answer comes in: true stops the check (as a failure).
CheckResult check(const Fetch& fetch, const Config& config, std::string_view app_version,
                  const std::function<bool()>& cancelled = {});

// A verified update of the app is staged (nx-discord-rpc.nro.new, never written unverified), waiting for the
// sysmodule to swap it in after the app closes. That takes a few seconds: an app reopened before then is the
// old one, and must not offer the same update again.
bool staged_ready(const Config& config);

enum class StartAction {
    None,   // Check for updates is off
    Check,  // the quiet check
    Ready,  // an update is staged: say to close the app and wait, check nothing
};
StartAction start_action(bool staged, bool check_updates);

enum class InstallOutcome {
    Done,       // every file replaced (see InstallResult::sysmodule_started)
    Failed,     // a download or a hash failed: nothing on the SD card changed
    Partial,    // a replace failed after others succeeded
};

struct InstallHooks {
    // done bytes, total bytes (0 when unknown); called from the worker.
    std::function<void(uint64_t, uint64_t)> progress;
    // Around exefs.nsp: stop the sysmodule before it is replaced (not running counts as stopped), start it
    // again after. start_sysmodule returns whether it started.
    std::function<void()> stop_sysmodule;
    std::function<bool()> start_sysmodule;
    // Checked between reads: true stops the download (as a failure, nothing changed).
    std::function<bool()> cancelled;
};

struct InstallResult {
    InstallOutcome outcome = InstallOutcome::Failed;
    std::string detail;  // English, for the log only
    // False when start_sysmodule failed: Done then asks the user to start it (Start now) or reboot.
    bool sysmodule_started = true;
};

// Downloads SHA256SUMS then every file to its .new name (.part for the nro), hashing as it goes. Only when all
// match does it replace the targets (nro_swap::swap: FAT has no atomic replace), the sysmodule's exefs.nsp between
// stop_sysmodule and start_sysmodule, and stage the .nro for the sysmodule to swap in.
InstallResult install(const Fetch& fetch, const Config& config, const Release& release, const InstallHooks& hooks);

// Removes .new and .part leftovers of an interrupted install (not the staged .nro, which is verified).
void clean_leftovers(const Config& config);

}  // namespace app::update
