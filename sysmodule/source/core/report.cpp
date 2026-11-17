#include "core/report.hpp"

#include <algorithm>

#include "core/json_writer.hpp"

namespace drpc {

namespace {

constexpr size_t kMaxFieldBytes = 128;
constexpr uint64_t kMinReportableTitleId = 0x0100000000010000ULL;
constexpr int64_t kMaxElapsedSeconds = 31536000;
constexpr const char* kFallbackName = "Nintendo Switch";

std::string paused_details(Screen screen) {
    switch (screen) {
        case Screen::HomeMenu: return "Paused \xC2\xB7 HOME Menu";
        case Screen::Shop: return "Paused \xC2\xB7 Nintendo eShop";
        case Screen::Album: return "Paused \xC2\xB7 Album";
        case Screen::UserPage: return "Paused \xC2\xB7 User page";
        case Screen::Unknown: break;
    }
    return "Paused";
}

void append_part(std::string& details, const std::string& part) {
    if (part.empty()) return;
    details += " \xC2\xB7 ";
    details += part;
}

}  // namespace

std::string format_playtime(uint64_t seconds) {
    if (seconds < 60) return "";
    if (seconds < 3600) return std::to_string(seconds / 60) + " min total";
    return std::to_string(seconds / 3600) + " h total";
}

std::string truncate_utf8(std::string_view s, size_t max_bytes) {
    if (s.size() <= max_bytes) return std::string{s};
    size_t end = max_bytes;
    while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) --end;
    return std::string{s.substr(0, end)};
}

std::optional<Report> render_report(const SceneInput& in, const Settings& settings) {
    if (!settings.enabled) return std::nullopt;

    Report r;
    const bool paused = in.scene == Scene::Paused && settings.show_paused;
    const bool game_like = in.scene != Scene::HomeMenu;

    if (in.scene == Scene::HomeMenu) {
        if (!settings.show_home_menu) return std::nullopt;
        r.name = kFallbackName;
        r.details = "In the HOME Menu";
    } else if (settings.private_mode) {
        r.name = kFallbackName;
        r.details = paused ? "Paused" : "Playing a game";
    } else {
        r.name = in.game_name.empty() ? kFallbackName : in.game_name;
        r.details = paused ? paused_details(in.paused_screen) : "on Nintendo Switch";
        if (in.title_id >= kMinReportableTitleId) r.title_id = format_title_id(in.title_id);
    }

    if (game_like && settings.show_docked && in.docked) append_part(r.details, *in.docked ? "Docked" : "Handheld");
    if (game_like && !settings.private_mode && settings.show_playtime && in.playtime_s)
        append_part(r.details, format_playtime(*in.playtime_s));
    if (settings.show_firmware) append_part(r.details, in.firmware);

    r.name = truncate_utf8(r.name, kMaxFieldBytes);
    r.details = truncate_utf8(r.details, kMaxFieldBytes);
    return r;
}

std::string report_json(const Report& report, std::optional<int64_t> elapsed_s) {
    json::Writer w;
    w.begin_object();
    w.key("name").value(report.name);
    w.key("details").value(report.details);
    if (elapsed_s) {
        w.key("elapsed_s").value(std::clamp<int64_t>(*elapsed_s, 0, kMaxElapsedSeconds));
    }
    if (report.title_id) w.key("title_id").value(*report.title_id);
    if (report.icon) w.key("icon").value(*report.icon);
    w.end_object();
    return w.str();
}

}  // namespace drpc
