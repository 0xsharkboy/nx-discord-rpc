#include "platform/config_store.hpp"

#include <cstdio>
#include <optional>
#include <string>
#include <sys/stat.h>

#include "platform/log.hpp"
#include "platform/paths.hpp"

namespace drpc {
namespace {

constexpr long kMaxFileBytes = 64 * 1024;

std::optional<std::string> read_file(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return std::nullopt;
    std::string data;
    char buf[1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        data.append(buf, n);
        if (data.size() > size_t(kMaxFileBytes)) break;
    }
    std::fclose(f);
    return data;
}

void ensure_dirs() {
    mkdir(paths::kConfigRoot, 0777);
    mkdir(paths::kConfigDir, 0777);
}

}  // namespace

Settings ConfigStore::load() {
    ensure_dirs();
    auto data = read_file(paths::kConfig);
    // A crash between remove and rename in save() leaves only the tmp file.
    if (!data && (data = read_file(paths::kConfigTmp))) std::rename(paths::kConfigTmp, paths::kConfig);
    return data ? parse_settings(*data) : Settings{};
}

bool ConfigStore::save(const Settings& s) {
    ensure_dirs();
    const std::string text = serialize_settings(s);
    FILE* f = std::fopen(paths::kConfigTmp, "wb");
    if (!f) return false;
    const bool written = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (std::fclose(f) != 0 || !written) {
        std::remove(paths::kConfigTmp);
        return false;
    }
    // fsdev cannot rename over an existing file.
    std::remove(paths::kConfig);
    return std::rename(paths::kConfigTmp, paths::kConfig) == 0;
}

std::optional<std::string> ConfigStore::import_api_key_file() {
    auto raw = read_file(paths::kApiKey);
    if (!raw) return std::nullopt;
    auto key = normalize_api_key(*raw);
    if (!key) {
        delete_api_key_file();
        log::warn("api_key.txt does not contain a valid API key; ignored and deleted");
    }
    return key;
}

void ConfigStore::delete_api_key_file() {
    std::remove(paths::kApiKey);
}

}  // namespace drpc
