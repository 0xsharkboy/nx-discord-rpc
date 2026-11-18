#include <borealis.hpp>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "app_window.hpp"
#include "drpc/client.hpp"
#include "title_icon.hpp"

using namespace brls::literals;

namespace {

#ifdef __SWITCH__
std::unique_ptr<drpc::Client> make_client(int, char*[]) { return drpc::make_switch_client(); }
#else
std::unique_ptr<drpc::Client> make_client(int argc, char* argv[]) {
    std::string_view scenario = "linked-game";
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) != "--fake") continue;
        if (i + 1 >= argc) {
            std::fprintf(stderr, "usage: %s [--fake <scenario>]\n", argv[0]);
            return nullptr;
        }
        scenario = argv[++i];
    }
    auto client = drpc::make_fake_client(scenario);
    if (!client)
        std::fprintf(stderr, "unknown fake scenario: %.*s\n", static_cast<int>(scenario.size()), scenario.data());
    return client;
}
#endif

#ifdef __SWITCH__
// Borealis brings up nifm and sockets only; the avatar download needs the ssl service too.
struct SslSession {
    SslSession() : ok(R_SUCCEEDED(sslInitialize(1))) {}
    ~SslSession() {
        if (ok) sslExit();
    }
    bool ok;
};

// Borealis makes the Simplified Chinese shared font the default on the Switch. Its accented Latin letters
// (é, à, è...) are wide pinyin glyphs, which space French words out ("R é glages"). Make the Standard shared
// font the default, as the system UI does, and keep every font Borealis loaded as a fallback. Runs after
// createWindow (fonts loaded) and before the first Label, which caches the default font.
void use_standard_font() {
    PlFontData standard;
    if (R_FAILED(plGetSharedFontByType(&standard, PlSharedFontType_Standard))) return;
    const int chinese = brls::Application::getFont(brls::FONT_CHINESE_SIMPLIFIED);
    if (chinese == brls::FONT_INVALID) return;
    if (!brls::Application::loadFontFromMemory(brls::FONT_CHINESE_SIMPLIFIED, standard.address, standard.size, false))
        return;
    NVGcontext* vg = brls::Application::getNVGContext();
    const int base = brls::Application::getFont(brls::FONT_CHINESE_SIMPLIFIED);
    // nanovg looks fallbacks up one level only: add each one to the new default directly.
    nvgAddFallbackFontId(vg, base, chinese);
    for (const std::string& name : {brls::FONT_CHINESE_SIMPLIFIED_EXT, brls::FONT_CHINESE_TRADITIONAL,
                                    brls::FONT_KOREAN_REGULAR, brls::FONT_SWITCH_ICONS, brls::FONT_MATERIAL_ICONS,
                                    std::string("emoji")}) {
        const int id = brls::Application::getFont(name);
        if (id != brls::FONT_INVALID) nvgAddFallbackFontId(vg, base, id);
    }
    if (brls::Application::getDefaultFont() != base)
        brls::Logger::warning("Default font was cached before the Standard font swap");
}
#endif

}  // namespace

int main(int argc, char* argv[]) {
    auto client = make_client(argc, argv);
    if (!client) return EXIT_FAILURE;
#ifdef __SWITCH__
    const SslSession ssl;
#endif

    // The Switch follows the console language; the desktop build reads BOREALIS_LANG (e.g. fr).
    // Languages without a translation fall back to en-US.
    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_AUTO;
    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }
    brls::Application::createWindow("app/title"_i18n);
#ifdef __SWITCH__
    use_standard_font();
#endif

    app::title_icon::init();
    app::AppWindow window(std::move(client));
    window.open();

    while (brls::Application::mainLoop()) {
    }
    app::title_icon::exit();
    return EXIT_SUCCESS;
}
