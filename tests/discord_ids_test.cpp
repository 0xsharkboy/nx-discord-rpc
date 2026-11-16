#include <doctest/doctest.h>

#include "drpc/discord_ids.hpp"

using namespace drpc;

TEST_CASE("discord ids") {
    CHECK(valid_discord_id("80351110224678912"));
    CHECK_FALSE(valid_discord_id(""));
    CHECK_FALSE(valid_discord_id("12a"));
    CHECK_FALSE(valid_discord_id("99999999999999999999"));  // over uint64
    CHECK(valid_avatar_hash("a_0123456789abcdef0123456789abcdef"));
    CHECK_FALSE(valid_avatar_hash("0123456789ABCDEF0123456789ABCDEF"));
    CHECK_FALSE(valid_avatar_hash("0123456789abcdef0123456789abcde"));
    CHECK(avatar_cdn_path("80351110224678912", "0123456789abcdef0123456789abcdef") ==
          "/avatars/80351110224678912/0123456789abcdef0123456789abcdef.png?size=128");
    CHECK(avatar_cdn_path("80351110224678912", "") == "/embed/avatars/5.png");
    CHECK(avatar_cdn_path("1", "") == "/embed/avatars/0.png");
    CHECK_FALSE(avatar_cdn_path("x", ""));
    CHECK_FALSE(avatar_cdn_path("1", "../../x"));
}
