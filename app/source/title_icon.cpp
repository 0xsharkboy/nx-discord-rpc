#include "title_icon.hpp"

#ifndef __SWITCH__
#include <cstdlib>
#include <fstream>
#include <iterator>
#endif

#ifdef __SWITCH__
#include <switch.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>

#include "core/nacp_titles.hpp"
#include "core/title_name.hpp"
#endif

namespace app::title_icon {

#ifdef __SWITCH__

namespace {

// Guards g_ready against exit() while the hidden games loader still reads control data on its thread.
std::mutex g_mutex;
bool g_ready = false;
std::map<std::string, std::vector<uint8_t>> g_cache;  // UI thread only

// NACP names are not always NUL-terminated (strnlen is not declared in strict C++ mode).
std::string_view entry_name(const NacpLanguageEntry& e) {
    const char* end = static_cast<const char*>(std::memchr(e.name, 0, sizeof e.name));
    return {e.name, end ? static_cast<size_t>(end - e.name) : sizeof e.name};
}

// About 144 KB: heap-allocated, never on the stack.
bool read_locked(uint64_t id, NsApplicationControlData& data, u64& size) {
    if (!g_ready || id == 0) return false;
    size = 0;
    const Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, id, &data,
                                                  sizeof(NsApplicationControlData), &size);
    return R_SUCCEEDED(rc) && size >= sizeof(NacpStruct);
}

std::vector<uint8_t> icon_of(const NsApplicationControlData& data, u64 size) {
    if (size <= sizeof(NacpStruct)) return {};
    const size_t icon_size = size - sizeof(NacpStruct);
    if (icon_size > sizeof(data.icon)) return {};
    return std::vector<uint8_t>(data.icon, data.icon + icon_size);
}

std::vector<uint8_t> load(const std::string& title_id) {
    char* end = nullptr;
    const u64 id = std::strtoull(title_id.c_str(), &end, 16);
    if (end == title_id.c_str() || *end != '\0') return {};
    auto data = std::make_unique<NsApplicationControlData>();
    u64 size = 0;
    const std::lock_guard lock(g_mutex);
    if (!read_locked(id, *data, size)) return {};
    return icon_of(*data, size);
}

}  // namespace

void init() {
    const std::lock_guard lock(g_mutex);
    g_ready = R_SUCCEEDED(nsInitialize());
}

void exit() {
    const std::lock_guard lock(g_mutex);
    g_cache.clear();
    if (g_ready) nsExit();
    g_ready = false;
}

const std::vector<uint8_t>& get(const std::string& title_id) {
    auto it = g_cache.find(title_id);
    if (it == g_cache.end()) it = g_cache.emplace(title_id, load(title_id)).first;
    return it->second;
}

bool read_control(uint64_t title_id, Control& out) {
    auto data = std::make_unique<NsApplicationControlData>();
    u64 size = 0;
    {
        const std::lock_guard lock(g_mutex);
        if (!read_locked(title_id, *data, size)) return false;
        // Decompresses [21.0.0+] compressed titles and leaves plain entries in data->nacp for ns.
        const auto titles = drpc::unpack_nacp_titles({reinterpret_cast<uint8_t*>(&data->nacp), sizeof data->nacp});
        NacpLanguageEntry* preferred = nullptr;
        std::string_view preferred_name;
        if (R_SUCCEEDED(nsGetApplicationDesiredLanguage(&data->nacp, &preferred)) && preferred)
            preferred_name = entry_name(*preferred);
        std::array<std::string_view, drpc::kNacpTitleCount> entries;
        for (size_t i = 0; i < entries.size(); ++i) entries[i] = drpc::nacp_title_name(titles, i);
        // choose_title_name falls back to "Unknown game"; the screen prefers the title id then.
        bool any = !preferred_name.empty();
        for (std::string_view e : entries) any = any || !e.empty();
        out.name = any ? drpc::choose_title_name(preferred_name, entries) : std::string();
    }
    out.jpeg = icon_of(*data, size);
    return true;
}

bool list_applications(std::vector<uint64_t>& out) {
    out.clear();
    constexpr s32 kPage = 64;
    auto page = std::make_unique<NsApplicationRecord[]>(kPage);
    const std::lock_guard lock(g_mutex);
    if (!g_ready) return false;
    for (s32 offset = 0;;) {
        s32 n = 0;
        if (R_FAILED(nsListApplicationRecord(page.get(), kPage, offset, &n))) return offset > 0;
        for (s32 i = 0; i < n; ++i) out.push_back(page[i].application_id);
        if (n < kPage) return true;
        offset += n;
    }
}

#else

void init() {}

void exit() {}

// Desktop builds have no title data: NXDRPC_TITLE_ICON_FILE names a JPEG shown for every title (screenshots).
const std::vector<uint8_t>& get(const std::string&) {
    static const std::vector<uint8_t> kIcon = [] {
        std::vector<uint8_t> jpeg;
        const char* file = std::getenv("NXDRPC_TITLE_ICON_FILE");
        if (!file || !*file) return jpeg;
        std::ifstream in(file, std::ios::binary);
        jpeg.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return jpeg;
    }();
    return kIcon;
}

bool read_control(uint64_t, Control&) { return false; }

bool list_applications(std::vector<uint64_t>& out) {
    out.clear();
    return false;
}

#endif

}  // namespace app::title_icon
