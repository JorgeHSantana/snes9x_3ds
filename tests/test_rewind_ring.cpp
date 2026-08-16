#include "doctest.h"

#include <cstring>
#include <vector>

#include "../source/3dsrewindring.h"

static std::vector<uint32_t> g_tags;

static RewindRing makeRing(std::vector<uint8_t>& pool, std::vector<uint32_t>& lens,
                           int slots, uint32_t slotSize)
{
    pool.assign((size_t)slots * slotSize, 0);
    lens.assign(slots, 0);
    g_tags.assign(slots, 0);
    RewindRing r;
    r.init(pool.data(), lens.data(), g_tags.data(), slots, slotSize);
    return r;
}

static void pushByte(RewindRing& r, uint8_t value, uint32_t length)
{
    memset(r.push_ptr(), value, length);
    r.push_commit(length, value * 100u);   // tag derived from the value
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

TEST_CASE("rewind ring: indexed peek walks from newest to oldest with tags")
{
    std::vector<uint8_t> pool; std::vector<uint32_t> lens;
    RewindRing r = makeRing(pool, lens, 4, 8);

    pushByte(r, 1, 8);
    pushByte(r, 2, 8);
    pushByte(r, 3, 8);

    const uint8_t* data; uint32_t len, tag;
    REQUIRE(r.peek_at(0, &data, &len, &tag));
    CHECK(data[0] == 3); CHECK(tag == 300);
    REQUIRE(r.peek_at(2, &data, &len, &tag));
    CHECK(data[0] == 1); CHECK(tag == 100);
    CHECK(!r.peek_at(3, &data, &len, &tag));
    CHECK(!r.peek_at(-1, &data, &len, &tag));
}

TEST_CASE("rewind ring: indexed peek stays correct after wrap")
{
    std::vector<uint8_t> pool; std::vector<uint32_t> lens;
    RewindRing r = makeRing(pool, lens, 3, 8);

    for (uint8_t v = 1; v <= 5; v++) pushByte(r, v, 8);   // survivors: 5,4,3

    const uint8_t* data; uint32_t len, tag;
    REQUIRE(r.peek_at(0, &data, &len, &tag)); CHECK(data[0] == 5);
    REQUIRE(r.peek_at(1, &data, &len, &tag)); CHECK(data[0] == 4);
    REQUIRE(r.peek_at(2, &data, &len, &tag)); CHECK(data[0] == 3);
    CHECK(!r.peek_at(3, &data, &len, &tag));
}

TEST_CASE("rewind ring: rollback_to drops the abandoned future, keeps the target")
{
    std::vector<uint8_t> pool; std::vector<uint32_t> lens;
    RewindRing r = makeRing(pool, lens, 5, 8);

    for (uint8_t v = 1; v <= 5; v++) pushByte(r, v, 8);

    r.rollback_to(2);   // resume from snapshot '3'
    CHECK(r.count == 3);

    const uint8_t* data; uint32_t len, tag;
    REQUIRE(r.peek_at(0, &data, &len, &tag));
    CHECK(data[0] == 3);   // target survives as newest

    pushByte(r, 9, 8);     // play on: new branch
    REQUIRE(r.peek_at(0, &data, &len, &tag)); CHECK(data[0] == 9);
    REQUIRE(r.peek_at(1, &data, &len, &tag)); CHECK(data[0] == 3);
    REQUIRE(r.peek_at(3, &data, &len, &tag)); CHECK(data[0] == 1);
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
