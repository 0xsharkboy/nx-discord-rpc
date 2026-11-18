#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "drpc/sha256.hpp"

using drpc::Sha256;

namespace {

std::span<const uint8_t> bytes(std::string_view s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }

}  // namespace

TEST_CASE("sha256_vectors: FIPS 180-4 examples") {
    CHECK(drpc::to_hex(Sha256::of(bytes(""))) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(drpc::to_hex(Sha256::of(bytes("abc"))) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(drpc::to_hex(Sha256::of(bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    const std::string million(1'000'000, 'a');
    CHECK(drpc::to_hex(Sha256::of(bytes(million))) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256_incremental: any split gives the same digest") {
    std::string data;
    for (int i = 0; i < 1000; ++i) data += char('a' + i % 26);
    const auto whole = Sha256::of(bytes(data));
    for (size_t step : {1u, 3u, 55u, 56u, 63u, 64u, 65u, 127u, 999u}) {
        Sha256 s;
        for (size_t pos = 0; pos < data.size(); pos += step)
            s.update(bytes(std::string_view(data).substr(pos, step)));
        CHECK(s.finish() == whole);
    }
}
