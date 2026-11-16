#include "core/report.hpp"

#include <algorithm>

#include "core/json_writer.hpp"

namespace drpc {

namespace {

constexpr size_t kMaxFieldBytes = 128;
constexpr uint64_t kMinReportableTitleId = 0x0100000000010000ULL;
constexpr int64_t kMaxElapsedSeconds = 31536000;
constexpr const char* kFallbackName = "Nintendo Switch";

}  // namespace

std::string truncate_utf8(std::string_view s, size_t max_bytes) {
    if (s.size() <= max_bytes) return std::string{s};
    size_t end = max_bytes;
    while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) --end;
    return std::string{s.substr(0, end)};
}

std::optional<Report> render_report(const SceneInput& in, const Settings& settings) {
    if (!settings.enabled) return std::nullopt;

    Report r;
    r.show_elapsed = settings.show_elapsed;

    if (in.scene == Scene::HomeMenu) {
        if (!settings.show_home_menu) return std::nullopt;
        r.name = kFallbackName;
        r.details = "In the HOME Menu";
    } else if (settings.private_mode) {
        r.name = kFallbackName;
        r.details = "Playing a game";
    } else {
        r.name = in.game_name.empty() ? kFallbackName : in.game_name;
        r.details = "on Nintendo Switch";
        if (in.title_id >= kMinReportableTitleId) r.title_id = format_title_id(in.title_id);
    }

    if (settings.show_firmware && !in.firmware.empty()) {
        r.details += " \xC2\xB7 ";
        r.details += in.firmware;
    }

    r.name = truncate_utf8(r.name, kMaxFieldBytes);
    r.details = truncate_utf8(r.details, kMaxFieldBytes);
    return r;
}

std::string report_json(const Report& report, std::optional<int64_t> elapsed_s) {
    json::Writer w;
    w.begin_object();
    w.key("name").value(report.name);
    w.key("details").value(report.details);
    if (report.show_elapsed && elapsed_s) {
        w.key("elapsed_s").value(std::clamp<int64_t>(*elapsed_s, 0, kMaxElapsedSeconds));
    }
    if (report.title_id) w.key("title_id").value(*report.title_id);
    w.end_object();
    return w.str();
}

}  // namespace drpc
