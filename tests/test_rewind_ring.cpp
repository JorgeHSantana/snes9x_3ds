#include "doctest.h"

#include <cstring>
#include <vector>

#include "../source/3dsrewindring.h"

static RewindRing makeRing(std::vector<uint8_t>& pool, std::vector<uint32_t>& lens,
                           int slots, uint32_t slotSize)
{
    pool.assign((size_t)slots * slotSize, 0);
    lens.assign(slots, 0);
    RewindRing r;
    r.init(pool.data(), lens.data(), slots, slotSize);
    return r;
}

static void pushByte(RewindRing& r, uint8_t value, uint32_t length)
{
    memset(r.push_ptr(), value, length);
    r.push_commit(length);
}

TEST_CASE("rewind ring: starts empty, pops nothing")
{
    std::vector<uint8_t> pool; std::vector<uint32_t> lens;
    RewindRing r = makeRing(pool, lens, 4, 16);

    CHECK(r.valid());
    CHECK(r.empty());
    const uint8_t* data; uint32_t len;
    CHECK(!r.pop_peek(&data, &len));
}

TEST_CASE("rewind ring: pops newest first (LIFO)")
{
    std::vector<uint8_t> pool; std::vector<uint32_t> lens;
    RewindRing r = makeRing(pool, lens, 4, 16);

    pushByte(r, 0xAA, 10);
    pushByte(r, 0xBB, 12);
    pushByte(r, 0xCC, 14);

    const uint8_t* data; uint32_t len;
    REQUIRE(r.pop_peek(&data, &len));
    CHECK(len == 14); CHECK(data[0] == 0xCC);
    r.pop_commit();

    REQUIRE(r.pop_peek(&data, &len));
    CHECK(len == 12); CHECK(data[0] == 0xBB);
    r.pop_commit();

    REQUIRE(r.pop_peek(&data, &len));
    CHECK(len == 10); CHECK(data[0] == 0xAA);
    r.pop_commit();

    CHECK(r.empty());
}

TEST_CASE("rewind ring: wraps by overwriting the oldest")
{
    std::vector<uint8_t> pool; std::vector<uint32_t> lens;
    RewindRing r = makeRing(pool, lens, 3, 8);

    pushByte(r, 1, 8);
    pushByte(r, 2, 8);
    pushByte(r, 3, 8);
    CHECK(r.full());
    pushByte(r, 4, 8);   // evicts snapshot 1
    CHECK(r.full());

    // popping everything yields 4, 3, 2 - snapshot 1 is gone
    const uint8_t* data; uint32_t len;
    uint8_t expected[3] = { 4, 3, 2 };
    for (int i = 0; i < 3; i++) {
        REQUIRE(r.pop_peek(&data, &len));
        CHECK(data[0] == expected[i]);
        r.pop_commit();
    }
    CHECK(r.empty());
}

TEST_CASE("rewind ring: play-after-rewind overwrites the popped branch")
{
    std::vector<uint8_t> pool; std::vector<uint32_t> lens;
    RewindRing r = makeRing(pool, lens, 4, 8);

    pushByte(r, 1, 8);
    pushByte(r, 2, 8);
    pushByte(r, 3, 8);

    const uint8_t* data; uint32_t len;
    REQUIRE(r.pop_peek(&data, &len));
    r.pop_commit();       // rewound past snapshot 3

    pushByte(r, 9, 8);   // new timeline

    REQUIRE(r.pop_peek(&data, &len));
    CHECK(data[0] == 9);
    r.pop_commit();
    REQUIRE(r.pop_peek(&data, &len));
    CHECK(data[0] == 2);
}

TEST_CASE("rewind ring: clear drops everything, slots stay reusable")
{
    std::vector<uint8_t> pool; std::vector<uint32_t> lens;
    RewindRing r = makeRing(pool, lens, 2, 8);

    pushByte(r, 5, 8);
    r.clear();
    CHECK(r.empty());

    pushByte(r, 6, 4);
    const uint8_t* data; uint32_t len;
    REQUIRE(r.pop_peek(&data, &len));
    CHECK(len == 4);
    CHECK(data[0] == 6);
}
