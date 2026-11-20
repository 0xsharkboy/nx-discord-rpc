#include "platform/title_monitor.hpp"

#include <switch.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "core/nacp_titles.hpp"
#include "core/title_name.hpp"

namespace drpc {
namespace {

constexpr size_t kMaxCachedNames = 32;

std::string_view entry_name(const NacpLanguageEntry& e) {
    return {e.name, strnlen(e.name, sizeof e.name)};
}

struct Names {
    std::string name, english_name;
};

// The title id the names were read from (the parent game for a sub-program) and its names.
struct Lookup {
    uint64_t title_id;
    Names names;
};

// nullopt when the title has no control data (for example a sub-program of a game).
std::optional<Names> lookup_names(uint64_t title_id) {
    // About 144 KB (NACP + icon): heap-allocated only while a new title is looked up.
    auto* data = static_cast<NsApplicationControlData*>(std::malloc(sizeof(NsApplicationControlData)));
    if (!data) return std::nullopt;
    std::optional<Names> names;
    u64 size = 0;
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
        names = Names{choose_title_name(preferred_name, entries), english_title_name(entries)};
    }
    std::free(data);
    return names;
}

Lookup lookup(uint64_t program_id) {
    if (auto names = lookup_names(program_id)) return {program_id, std::move(*names)};
    if (const auto parent = parent_application_id(program_id)) {
        if (auto names = lookup_names(*parent)) return {*parent, std::move(*names)};
    }
    return {program_id, Names{std::string{kUnknownGameName}, {}}};
}

}  // namespace

std::optional<RunningTitle> current_title() {
    static std::unordered_map<uint64_t, Lookup> cache;  // by program id

    u64 pid = 0, program_id = 0;
    if (R_FAILED(pmdmntGetApplicationProcessId(&pid))) return std::nullopt;
    if (R_FAILED(pminfoGetProgramId(&program_id, pid))) return std::nullopt;

    auto it = cache.find(program_id);
    if (it == cache.end()) {
        if (cache.size() >= kMaxCachedNames) cache.clear();
        it = cache.emplace(program_id, lookup(program_id)).first;
    }
    const Lookup& l = it->second;
    return RunningTitle{l.title_id, l.names.name, l.names.english_name};
}

}  // namespace drpc
