#include "title_icon.hpp"

#ifdef __SWITCH__
#include <switch.h>

#include <cstdlib>
#include <map>
#include <memory>
#endif

namespace app::title_icon {

#ifdef __SWITCH__

namespace {

bool g_ready = false;
std::map<std::string, std::vector<uint8_t>> g_cache;

std::vector<uint8_t> load(const std::string& title_id) {
    char* end = nullptr;
    const u64 id = std::strtoull(title_id.c_str(), &end, 16);
    if (!g_ready || id == 0 || end == title_id.c_str() || *end != '\0') return {};
    auto data = std::make_unique<NsApplicationControlData>();
    u64 size = 0;
    const Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, id, data.get(),
                                                  sizeof(NsApplicationControlData), &size);
    if (R_FAILED(rc) || size <= sizeof(NacpStruct)) return {};
    const size_t icon_size = size - sizeof(NacpStruct);
    if (icon_size > sizeof(data->icon)) return {};
    return std::vector<uint8_t>(data->icon, data->icon + icon_size);
}

}  // namespace

void init() { g_ready = R_SUCCEEDED(nsInitialize()); }

void exit() {
    g_cache.clear();
    if (g_ready) nsExit();
    g_ready = false;
}

const std::vector<uint8_t>& get(const std::string& title_id) {
    auto it = g_cache.find(title_id);
    if (it == g_cache.end()) it = g_cache.emplace(title_id, load(title_id)).first;
    return it->second;
}

#else

void init() {}

void exit() {}

const std::vector<uint8_t>& get(const std::string&) {
    static const std::vector<uint8_t> kNone;
    return kNone;
}

#endif

}  // namespace app::title_icon
