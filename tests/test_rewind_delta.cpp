// Host-side tests for the rewind page-delta codec (issue #37).

#include "doctest.h"

#include <vector>

#include "3dsrewinddelta.h"

namespace {

struct DeltaFixture
{
    static constexpr uint32_t LEN = 2500;   // not a page multiple on purpose
    static constexpr uint32_t PAGE = 1024;  // 3 pages: 1024+1024+452
    std::vector<uint8_t> key, state, delta, out;

    DeltaFixture() : key(LEN), state(LEN), delta(LEN + 64), out(LEN)
    {
        for (uint32_t i = 0; i < LEN; i++)
            key[i] = (uint8_t)(i * 7);
        state = key;
    }

    uint32_t enc() {
        return RewindDelta::encode(key.data(), LEN, state.data(), LEN,
            PAGE, delta.data(), (uint32_t)delta.size());
    }
    uint32_t dec(uint32_t deltaLen) {
        return RewindDelta::decode(key.data(), LEN, delta.data(), deltaLen,
            PAGE, out.data(), (uint32_t)out.size());
    }
};

} // namespace

TEST_CASE("delta: identical state costs the 8-byte header")
{
    DeltaFixture f;
    CHECK(f.enc() == 8);
    CHECK(f.dec(8) == DeltaFixture::LEN);
    CHECK(f.out == f.key);
}

TEST_CASE("delta: one dirty byte stores exactly its page")
{
    DeltaFixture f;
    f.state[100] ^= 0xFF;
    uint32_t n = f.enc();
    CHECK(n == 8 + 4 + DeltaFixture::PAGE);
    REQUIRE(f.dec(n) == DeltaFixture::LEN);
    CHECK(f.out == f.state);
}

TEST_CASE("delta: dirty short tail page round-trips")
{
    DeltaFixture f;
    f.state[DeltaFixture::LEN - 1] ^= 0x55;   // page 2 = 452 bytes
    uint32_t n = f.enc();
    CHECK(n == 8 + 4 + (DeltaFixture::LEN - 2 * DeltaFixture::PAGE));
    REQUIRE(f.dec(n) == DeltaFixture::LEN);
    CHECK(f.out == f.state);
}

TEST_CASE("delta: every page dirty still round-trips")
{
    DeltaFixture f;
    for (uint32_t i = 0; i < DeltaFixture::LEN; i += 97)
        f.state[i] ^= 0xA5;
    uint32_t n = f.enc();
    REQUIRE(n > 0);
    REQUIRE(f.dec(n) == DeltaFixture::LEN);
    CHECK(f.out == f.state);
}

TEST_CASE("delta: refuses when it will not fit (caller stores full)")
{
    DeltaFixture f;
    for (uint32_t i = 0; i < DeltaFixture::LEN; i += 97)
        f.state[i] ^= 0xA5;
    CHECK(RewindDelta::encode(f.key.data(), DeltaFixture::LEN,
        f.state.data(), DeltaFixture::LEN, DeltaFixture::PAGE,
        f.delta.data(), 512) == 0);
}

TEST_CASE("delta: refuses length mismatch and corrupt input")
{
    DeltaFixture f;
    CHECK(RewindDelta::encode(f.key.data(), DeltaFixture::LEN - 4,
        f.state.data(), DeltaFixture::LEN, DeltaFixture::PAGE,
        f.delta.data(), (uint32_t)f.delta.size()) == 0);

    f.state[0] ^= 1;
    uint32_t n = f.enc();
    REQUIRE(n > 0);
    CHECK(f.dec(n - 5) == 0);                      // truncated payload
    uint32_t bad = 999999;
    memcpy(f.delta.data() + 8, &bad, 4);           // out-of-range page
    CHECK(f.dec(n) == 0);
}
