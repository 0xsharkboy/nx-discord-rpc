#include "updater.hpp"

#include <sys/stat.h>

#include <charconv>
#include <cstdio>
#include <utility>

#include "core/json_extract.hpp"
#include "core/nro_swap.hpp"
#include "drpc/sha256.hpp"

namespace app::update {

namespace {

constexpr const char* kContents = "/atmosphere/contents/4200000000D15C0D";
constexpr size_t kMaxUrl = 2048;

bool is_hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }

std::optional<uint32_t> parse_part(std::string_view s) {
    if (s.empty() || s.size() > 9) return std::nullopt;
    uint32_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        v = v * 10 + uint32_t(c - '0');
    }
    return v;
}

bool exists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

// Creates every directory of path's parent below root (each may already exist).
void make_parent_dirs(const std::string& root, const std::string& path) {
    const size_t last = path.find_last_of('/');
    if (last == std::string::npos || last <= root.size()) return;
    for (size_t pos = path.find('/', root.size() + 1); pos != std::string::npos && pos <= last; pos = path.find('/', pos + 1))
        mkdir(path.substr(0, pos).c_str(), 0777);
}

// Removes a file; absent counts as removed.
bool remove_file(const std::string& path) {
    if (std::remove(path.c_str()) == 0) return true;
    return !exists(path);
}

bool replace_file(const std::string& target, const std::string& staged) {
    return drpc::nro_swap::swap(target, staged) == drpc::nro_swap::SwapResult::Swapped;
}

std::string staged_nro(const Config& config) { return config.root + drpc::nro_swap::kStaged; }

}  // namespace

std::optional<Version> parse_version(std::string_view text, bool tag) {
    if (tag) {
        if (!text.starts_with('v')) return std::nullopt;
        text.remove_prefix(1);
    }
    Version v;
    uint32_t* parts[] = {&v.major, &v.minor, &v.patch};
    for (int i = 0; i < 3; ++i) {
        const size_t dot = i < 2 ? text.find('.') : text.size();
        if (dot == std::string_view::npos) return std::nullopt;
        const auto n = parse_part(text.substr(0, dot));
        if (!n) return std::nullopt;
        *parts[i] = *n;
        text.remove_prefix(i < 2 ? dot + 1 : dot);
    }
    return v;
}

bool is_newer(std::string_view tag, std::string_view app_version) {
    const auto latest = parse_version(tag, true);
    const auto current = parse_version(app_version, false);
    return latest && current && *latest > *current;
}

size_t max_file_bytes(std::string_view name) {
    return name == "nx-discord-rpc.nro" ? size_t(8) * 1024 * 1024 : size_t(1024) * 1024;
}

bool Release::installable() const {
    const auto usable = [this](std::string_view name) {
        const auto it = assets.find(name);
        if (it == assets.end()) return false;
        const auto url = http::parse_url(it->second.url);
        return url && http::allowed_host(url->host);
    };
    for (std::string_view name : kFiles)
        if (!usable(name)) return false;
    return usable(kSums);
}

uint64_t Release::total_size() const {
    uint64_t total = 0;
    for (std::string_view name : kFiles)
        if (const auto it = assets.find(name); it != assets.end()) total += it->second.size;
    return total;
}

struct ReleaseReader::Impl {
    drpc::json::Extractor extractor{
        {"tag_name", "prerelease", "draft", "assets[].name", "assets[].browser_download_url", "assets[].size"},
        kMaxUrl};
};

ReleaseReader::ReleaseReader() : impl_(std::make_shared<Impl>()) {}

void ReleaseReader::feed(std::string_view chunk) { impl_->extractor.feed(chunk); }

std::optional<Release> ReleaseReader::finish() {
    drpc::json::Extractor& ex = impl_->extractor;
    if (!ex.finish()) return std::nullopt;
    const auto* tag = ex.find("tag_name");
    if (!tag || !tag->is_string) return std::nullopt;
    Release r;
    r.tag = tag->value;
    if (const auto* p = ex.find("prerelease")) r.prerelease = !p->is_string && p->value == "true";
    if (const auto* d = ex.find("draft")) r.draft = !d->is_string && d->value == "true";

    // Captures come in document order: "assets[3].name", "assets[3].size", ...
    std::map<std::string, std::pair<std::string, Asset>> by_index;
    for (const auto& c : ex.captures()) {
        if (!c.path.starts_with("assets[")) continue;
        const size_t close = c.path.find("].");
        if (close == std::string::npos) continue;
        const std::string index = c.path.substr(7, close - 7);
        const std::string_view field = std::string_view(c.path).substr(close + 2);
        auto& [name, asset] = by_index[index];
        if (field == "name" && c.is_string) {
            name = c.value;
        } else if (field == "browser_download_url" && c.is_string) {
            asset.url = c.value;
        } else if (field == "size" && !c.is_string) {
            uint64_t n = 0;
            const auto [end, ec] = std::from_chars(c.value.data(), c.value.data() + c.value.size(), n);
            if (ec == std::errc{} && end == c.value.data() + c.value.size()) asset.size = n;
        }
    }
    for (auto& [index, entry] : by_index)
        if (!entry.first.empty() && !entry.second.url.empty()) r.assets.emplace(entry.first, std::move(entry.second));
    return r;
}

std::optional<Release> parse_release(std::string_view json) {
    ReleaseReader reader;
    reader.feed(json);
    return reader.finish();
}

std::optional<std::map<std::string, std::string, std::less<>>> parse_sha256sums(std::string_view text) {
    std::map<std::string, std::string, std::less<>> out;
    while (!text.empty()) {
        const size_t eol = text.find('\n');
        std::string_view line = text.substr(0, eol);
        text.remove_prefix(eol == std::string_view::npos ? text.size() : eol + 1);
        if (line.ends_with('\r')) line.remove_suffix(1);
        if (line.size() < 67 || line[64] != ' ' || (line[65] != ' ' && line[65] != '*')) continue;
        std::string hash;
        bool ok = true;
        for (char c : line.substr(0, 64)) {
            if (!is_hex(c)) ok = false;
            hash.push_back(lower(c));
        }
        if (!ok) continue;
        const std::string name(line.substr(66));
        const auto [it, inserted] = out.emplace(name, hash);
        if (!inserted && it->second != hash) return std::nullopt;
    }
    return out;
}

Target target_for(const Config& config, std::string_view name) {
    Target t{name, {}, {}};
    if (name == "nx-discord-rpc.ovl") {
        t.path = config.root + "/switch/.overlays/nx-discord-rpc.ovl";
    } else if (name == "toolbox.json" || name == "exefs.nsp") {
        t.path = config.root + kContents + "/" + std::string(name);
    } else if (name == "nx-discord-rpc.nro") {
        t.path = config.root + drpc::nro_swap::kNro;
        // Never downloaded straight to .nro.new: the sysmodule swaps that name in without checking it.
        t.staged = t.path + ".part";
        return t;
    } else {
        return t;
    }
    t.staged = t.path + ".new";
    return t;
}

CheckResult check(const Fetch& fetch, const Config& config, std::string_view app_version,
                  const std::function<bool()>& cancelled) {
    CheckResult result;
    ReleaseReader reader;
    http::Options options;
    options.user_agent = config.user_agent;
    options.accept = "application/vnd.github+json";
    options.max_body = kMaxApiBytes;
    const http::Result got = fetch(config.latest_url(), options, [&reader, &cancelled](std::span<const uint8_t> d) {
        if (cancelled && cancelled()) return false;
        reader.feed({reinterpret_cast<const char*>(d.data()), d.size()});
        return true;
    });
    if (got.error != http::Error::None) return result;
    auto release = reader.finish();
    if (!release) return result;
    if (release->draft || release->prerelease || !is_newer(release->tag, app_version)) {
        result.outcome = CheckOutcome::UpToDate;
        return result;
    }
    result.outcome = release->installable() ? CheckOutcome::Available : CheckOutcome::ManualOnly;
    result.release = std::move(*release);
    return result;
}

bool staged_ready(const Config& config) { return exists(staged_nro(config)); }

StartAction start_action(bool staged, bool check_updates) {
    if (staged) return StartAction::Ready;
    return check_updates ? StartAction::Check : StartAction::None;
}

void clean_leftovers(const Config& config) {
    for (std::string_view name : kFiles) remove_file(target_for(config, name).staged);
}

InstallResult install(const Fetch& fetch, const Config& config, const Release& release, const InstallHooks& hooks) {
    const auto failed = [&config](std::string detail) {
        clean_leftovers(config);
        return InstallResult{InstallOutcome::Failed, std::move(detail)};
    };
    if (!release.installable()) return failed("release files missing");
    clean_leftovers(config);

    http::Options options;
    options.user_agent = config.user_agent;
    options.accept = "application/octet-stream";

    std::string sums_text;
    options.max_body = kMaxSumsBytes;
    http::Result got = fetch(release.assets.find(kSums)->second.url, options, [&sums_text](std::span<const uint8_t> d) {
        sums_text.append(reinterpret_cast<const char*>(d.data()), d.size());
        return true;
    });
    if (got.error != http::Error::None) return failed(std::string("SHA256SUMS: ") + http::error_name(got.error));
    const auto sums = parse_sha256sums(sums_text);
    if (!sums) return failed("SHA256SUMS is invalid");
    for (std::string_view name : kFiles)
        if (!sums->contains(name)) return failed("SHA256SUMS does not list " + std::string(name));

    // Download and verify everything before touching an installed file.
    const uint64_t total = release.total_size();
    uint64_t done = 0;
    for (std::string_view name : kFiles) {
        const Target target = target_for(config, name);
        make_parent_dirs(config.root, target.staged);
        std::FILE* f = std::fopen(target.staged.c_str(), "wb");
        if (!f) return failed("cannot write " + target.staged);
        drpc::Sha256 hash;
        bool write_failed = false;
        bool stopped = false;
        options.max_body = max_file_bytes(name);
        got = fetch(release.assets.find(name)->second.url, options, [&](std::span<const uint8_t> d) {
            if (hooks.cancelled && hooks.cancelled()) {
                stopped = true;
                return false;
            }
            if (std::fwrite(d.data(), 1, d.size(), f) != d.size()) {
                write_failed = true;
                return false;
            }
            hash.update(d);
            done += d.size();
            if (hooks.progress) hooks.progress(done, total);
            return true;
        });
        const bool closed = std::fclose(f) == 0;
        if (stopped) return failed("cancelled");
        if (write_failed || !closed) return failed("cannot write " + target.staged);
        if (got.error != http::Error::None) return failed(std::string(name) + ": " + http::error_name(got.error));
        if (drpc::to_hex(hash.finish()) != sums->find(name)->second)
            return failed(std::string(name) + " does not match SHA256SUMS");
    }

    // Replace, one file at a time (see nro_swap::swap), so a failure from here on can leave a mix of versions.
    bool replaced_any = false;
    bool started = true;
    for (std::string_view name : kFiles) {
        const Target target = target_for(config, name);
        bool ok;
        if (name == "nx-discord-rpc.nro") {
            const std::string staged = staged_nro(config);
            ok = remove_file(staged) && std::rename(target.staged.c_str(), staged.c_str()) == 0;
        } else if (name == "exefs.nsp") {
            if (hooks.stop_sysmodule) hooks.stop_sysmodule();
            ok = replace_file(target.path, target.staged);
            if (hooks.start_sysmodule) started = hooks.start_sysmodule();
        } else {
            make_parent_dirs(config.root, target.path);
            ok = replace_file(target.path, target.staged);
        }
        if (!ok) {
            clean_leftovers(config);
            std::string detail = "could not replace " + target.path;
            if (!started) detail += "; the sysmodule did not start";
            return {replaced_any ? InstallOutcome::Partial : InstallOutcome::Failed, detail, started};
        }
        replaced_any = true;
    }
    if (hooks.progress) hooks.progress(total, total);
    if (!started) return {InstallOutcome::Done, "installed " + release.tag + "; the sysmodule did not start", false};
    return {InstallOutcome::Done, "installed " + release.tag};
}

}  // namespace app::update
