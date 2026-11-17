#include <doctest/doctest.h>

#include "core/elapsed.hpp"

using namespace drpc;

namespace {
struct Clocks {
    int64_t wall = 1'000'000;
    int64_t mono = 5'000;
};
Elapsed make(Clocks& c) {
    return Elapsed([&c] { return c.wall; }, [&c] { return c.mono; });
}
void advance(Clocks& c, int64_t ms) {
    c.wall += ms;
    c.mono += ms;
}
}  // namespace

TEST_CASE("elapsed counts from restart") {
    Clocks c;
    Elapsed e = make(c);
    e.restart();
    advance(c, 12'500);
    CHECK(e.seconds() == 12);
}

TEST_CASE("pause_excludes_paused_time") {
    Clocks c;
    Elapsed e = make(c);
    e.restart();
    advance(c, 10'000);
    e.pause();
    CHECK(e.paused());
    advance(c, 60'000);
    CHECK(e.seconds() == 10);
}

TEST_CASE("resume_continues") {
    Clocks c;
    Elapsed e = make(c);
    e.restart();
    advance(c, 10'000);
    e.pause();
    advance(c, 60'000);
    e.resume();
    CHECK_FALSE(e.paused());
    CHECK(e.seconds() == 10);
    advance(c, 5'000);
    CHECK(e.seconds() == 15);
}

TEST_CASE("elapsed_resets_on_title_change_while_paused") {
    Clocks c;
    Elapsed e = make(c);
    e.restart();
    advance(c, 10'000);
    e.pause();
    advance(c, 30'000);
    e.restart();
    CHECK(e.seconds() == 0);
    CHECK_FALSE(e.paused());
    advance(c, 4'000);
    CHECK(e.seconds() == 4);
}

TEST_CASE("pause and resume are idempotent") {
    Clocks c;
    Elapsed e = make(c);
    e.restart();
    e.resume();
    advance(c, 3'000);
    e.pause();
    advance(c, 1'000);
    e.pause();
    advance(c, 1'000);
    e.resume();
    e.resume();
    CHECK(e.seconds() == 3);
}

TEST_CASE("falls back to monotonic when the wall clock is unavailable") {
    Clocks c;
    c.wall = 0;
    Elapsed e = make(c);
    e.restart();
    advance(c, 7'000);
    c.wall = 0;
    CHECK(e.seconds() == 7);
    e.pause();
    advance(c, 9'000);
    c.wall = 0;
    e.resume();
    CHECK(e.seconds() == 7);
}
