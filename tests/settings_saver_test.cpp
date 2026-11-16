#include <doctest/doctest.h>

#include "core/settings_saver.hpp"

using drpc::SettingsSaver;

TEST_CASE("SettingsSaver: due 2000 ms after the last change") {
    SettingsSaver saver;
    CHECK_FALSE(saver.due(0));
    saver.changed(0);
    saver.changed(500);
    saver.changed(1500);
    CHECK_FALSE(saver.due(3499));
    CHECK(saver.due(3500));
}

TEST_CASE("SettingsSaver: not due after saved() until the next change") {
    SettingsSaver saver;
    saver.changed(0);
    REQUIRE(saver.due(2000));
    saver.saved();
    CHECK_FALSE(saver.due(2000));
    CHECK_FALSE(saver.due(100000));
    saver.changed(100000);
    CHECK_FALSE(saver.due(101999));
    CHECK(saver.due(102000));
}
