#include "platform/title_monitor.hpp"

#include <switch.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <unordered_map>

#include "core/nacp_titles.hpp"
#include "core/title_name.hpp"

namespace drpc {
namespace {

constexpr size_t kMaxCachedNames = 32;

std::string_view entry_name(const NacpLanguageEntry& e) {
    return {e.name, strnlen(e.name, sizeof e.name)};
}

std::string lookup_name(uint64_t title_id) {
    // About 144 KB (NACP + icon): heap-allocated only while a new title is looked up.
    auto* data = static_cast<NsApplicationControlData*>(std::malloc(sizeof(NsApplicationControlData)));
    if (!data) return std::string{kUnknownGameName};
    u64 size = 0;
    std::string name{kUnknownGameName};
    if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, title_id, data, sizeof *data, &size)) &&
        size >= sizeof data->nacp) {
        // Decompresses [21.0.0+] compressed titles and leaves plain entries in data->nacp for ns.
        const auto titles = unpack_nacp_titles({reinterpret_cast<uint8_t*>(&data->nacp), sizeof data->nacp});
        NacpLanguageEntry* preferred = nullptr;
        std::string_view preferred_name;
        if (R_SUCCEEDED(nsGetApplicationDesiredLanguage(&data->nacp, &preferred)) && preferred)
            preferred_name = entry_name(*preferred);
        std::array<std::string_view, kNacpTitleCount> entries;
        for (size_t i = 0; i < entries.size(); ++i) entries[i] = nacp_title_name(titles, i);
        name = choose_title_name(preferred_name, entries);
    }
    std::free(data);
    return name;
}

}  // namespace

std::optional<RunningTitle> current_title() {
    static std::unordered_map<uint64_t, std::string> names;

    u64 pid = 0, title_id = 0;
    if (R_FAILED(pmdmntGetApplicationProcessId(&pid))) return std::nullopt;
    if (R_FAILED(pminfoGetProgramId(&title_id, pid))) return std::nullopt;

    auto it = names.find(title_id);
    if (it == names.end()) {
        if (names.size() >= kMaxCachedNames) names.clear();
        it = names.emplace(title_id, lookup_name(title_id)).first;
    }
    return RunningTitle{title_id, it->second};
}

}  // namespace drpc
