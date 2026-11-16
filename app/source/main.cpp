#include <borealis.hpp>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>

#include "app_window.hpp"
#include "drpc/client.hpp"

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

}  // namespace

int main(int argc, char* argv[]) {
    auto client = make_client(argc, argv);
    if (!client) return EXIT_FAILURE;

    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_EN_US;
    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }
    brls::Application::createWindow("app/title"_i18n);

    app::AppWindow window(std::move(client));
    window.open();

    while (brls::Application::mainLoop()) {
    }
    return EXIT_SUCCESS;
}
