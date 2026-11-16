#include "avatar_cache.hpp"

#include <sys/stat.h>

#include <cstdio>
#include <utility>

namespace app {

namespace {

constexpr uint8_t kPngSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

// The whole file, or nullopt when it is missing, unreadable or over max_bytes.
std::optional<std::vector<uint8_t>> read_file(const std::string& path, size_t max_bytes) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::nullopt;
    std::vector<uint8_t> data;
    uint8_t buf[4096];
    size_t n;
    bool too_big = false;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        data.insert(data.end(), buf, buf + n);
        if (data.size() > max_bytes) {
            too_big = true;
            break;
        }
    }
    const bool failed = std::ferror(f) != 0;
    std::fclose(f);
    if (too_big || failed) return std::nullopt;
    return data;
}

bool write_file(const std::string& path, std::span<const uint8_t> data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool written = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    if (std::fclose(f) != 0 || !written) {
        std::remove(path.c_str());
        return false;
    }
    return true;
}

std::span<const uint8_t> bytes(std::string_view s) {
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

}  // namespace

std::string avatar_cache_key(std::string_view discord_id, std::string_view avatar) {
    std::string key(discord_id);
    key += ':';
    key += avatar.empty() ? std::string_view("default") : avatar;
    return key;
}

bool is_png(std::span<const uint8_t> data) {
    if (data.size() < sizeof kPngSignature) return false;
    for (size_t i = 0; i < sizeof kPngSignature; ++i)
        if (data[i] != kPngSignature[i]) return false;
    return true;
}

AvatarCache::AvatarCache(std::string dir) : dir_(std::move(dir)) {}

std::optional<std::vector<uint8_t>> AvatarCache::load(std::string_view key) const {
    auto stored_key = read_file(dir_ + "/avatar.key", 128);
    if (!stored_key || std::string_view(reinterpret_cast<const char*>(stored_key->data()), stored_key->size()) != key)
        return std::nullopt;
    auto png = read_file(dir_ + "/avatar.png", kAvatarMaxBytes);
    if (!png || !is_png(*png)) return std::nullopt;
    return png;
}

bool AvatarCache::store(std::string_view key, std::span<const uint8_t> png) {
    mkdir(dir_.c_str(), 0777);
    const std::string png_path = dir_ + "/avatar.png";
    const std::string key_path = dir_ + "/avatar.key";
    if (!write_file(png_path + ".tmp", png)) return false;
    if (!write_file(key_path + ".tmp", bytes(key))) {
        std::remove((png_path + ".tmp").c_str());
        return false;
    }
    // The key goes first and comes back last, so a crash never pairs a key with another picture.
    // fsdev cannot rename over an existing file.
    std::remove(key_path.c_str());
    std::remove(png_path.c_str());
    return std::rename((png_path + ".tmp").c_str(), png_path.c_str()) == 0 &&
           std::rename((key_path + ".tmp").c_str(), key_path.c_str()) == 0;
}

void AvatarCache::clear() {
    std::remove((dir_ + "/avatar.key").c_str());
    std::remove((dir_ + "/avatar.png").c_str());
}

}  // namespace app
