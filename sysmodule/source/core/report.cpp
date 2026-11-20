#include "core/report.hpp"

#include <algorithm>
#include <utility>

#include "core/json_writer.hpp"

namespace drpc {

namespace {

constexpr size_t kMaxFieldBytes = 128;
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

std::string repair_utf8(std::string s) {
    const auto byte = [&s](size_t i) { return static_cast<unsigned char>(s[i]); };
    const auto in = [](unsigned char c, unsigned char lo, unsigned char hi) { return c >= lo && c <= hi; };
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = byte(i);
        size_t len = 0;
        unsigned char lo = 0x80, hi = 0xBF;  // the allowed range of the second byte
        if (c < 0x80) len = 1;
        else if (in(c, 0xC2, 0xDF)) len = 2;
        else if (c == 0xE0) len = 3, lo = 0xA0;
        else if (in(c, 0xE1, 0xEC) || in(c, 0xEE, 0xEF)) len = 3;
        else if (c == 0xED) len = 3, hi = 0x9F;
        else if (c == 0xF0) len = 4, lo = 0x90;
        else if (in(c, 0xF1, 0xF3)) len = 4;
        else if (c == 0xF4) len = 4, hi = 0x8F;
        bool ok = len > 0 && i + len <= s.size();
        for (size_t k = 1; ok && k < len; ++k) ok = k == 1 ? in(byte(i + 1), lo, hi) : in(byte(i + k), 0x80, 0xBF);
        if (ok) {
            i += len;
        } else {
            s[i] = '?';
            ++i;
        }
    }
    return s;
}

std::optional<Report> render_report(const SceneInput& in, const Settings& settings) {
    if (!settings.enabled) return std::nullopt;
    // A per-title choice: Hidden sends nothing at all, Private renders like private mode.
    const TitleVisibility visibility =
        in.scene == Scene::HomeMenu ? TitleVisibility::Shown : visibility_of(settings, in.title_id);
    if (visibility == TitleVisibility::Hidden) return std::nullopt;
    const bool masked = settings.private_mode || visibility == TitleVisibility::Private;

    Report r;
    const bool paused = in.scene == Scene::Paused && settings.show_paused;
    const bool game_like = in.scene != Scene::HomeMenu;

    if (in.scene == Scene::HomeMenu) {
        if (!settings.show_home_menu) return std::nullopt;
        r.name = kFallbackName;
        r.details = "In the HOME Menu";
    } else if (masked) {
        r.name = kFallbackName;
        r.details = paused ? "Paused" : "Playing a game";
    } else {
        const bool english = settings.english_game_names && !in.english_game_name.empty();
        const std::string& game_name = english ? in.english_game_name : in.game_name;
        r.name = game_name.empty() ? kFallbackName : game_name;
        r.details = paused ? paused_details(in.paused_screen) : "on Nintendo Switch";
        if (in.title_id >= ipc::kMinReportableTitleId) r.title_id = format_title_id(in.title_id);
    }

    if (game_like && settings.show_docked && in.docked) append_part(r.details, *in.docked ? "Docked" : "Handheld");
    if (game_like && !masked && settings.show_playtime && in.playtime_s)
        append_part(r.details, format_playtime(*in.playtime_s));
    if (settings.show_firmware) append_part(r.details, in.firmware);

    r.name = truncate_utf8(repair_utf8(std::move(r.name)), kMaxFieldBytes);
    r.details = truncate_utf8(repair_utf8(std::move(r.details)), kMaxFieldBytes);
    r.badge = settings.show_badge;
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
    if (report.badge) w.key("badge").value(true);
    w.end_object();
    return w.str();
}

}  // namespace drpc
