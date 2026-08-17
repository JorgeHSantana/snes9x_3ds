// Host-side tests for the keyframe-disciplined delta ring (issue #37, step 2).

#include "doctest.h"

#include <vector>
#include <cstring>

#include "3dsrewinddeltaring.h"

namespace {

struct RingFixture
{
    static constexpr uint32_t STATE = 256;   // synthetic state size
    static constexpr uint32_t PAGE = 64;
    static constexpr int KF_SLOTS = 3;
    static constexpr int DELTA_SLOTS = 6;
    static constexpr uint32_t DELTA_SLOT = 8 + 2 * (4 + PAGE);   // fits 2 dirty pages
    static constexpr int ENTRIES = 16;
    static constexpr int INTERVAL = 4;       // a keyframe at least every 4

    std::vector<uint8_t> kfPool, deltaPool, state, out;
    RewindDeltaRing::Entry entries[ENTRIES];
    RewindDeltaRing ring;
    uint32_t nextTag = 0;

    RingFixture() : kfPool(KF_SLOTS * 512), deltaPool(DELTA_SLOTS * DELTA_SLOT),
                    state(STATE), out(STATE)
    {
        ring.init(kfPool.data(), KF_SLOTS, 512,
                  deltaPool.data(), DELTA_SLOTS, DELTA_SLOT,
                  entries, ENTRIES, PAGE, INTERVAL);
        for (uint32_t i = 0; i < STATE; i++)
            state[i] = (uint8_t)i;
    }

    // mutate a few bytes and push; dirtyPages controls the delta size
    void play(int dirtyPages = 1)
    {
        for (int p = 0; p < dirtyPages; p++)
            state[p * PAGE] += 1;
        uint8_t *dst = ring.push_ptr();
        REQUIRE(dst != nullptr);
        memcpy(dst, state.data(), STATE);
        ring.push_commit(STATE, ++nextTag);
    }
};

} // namespace

TEST_CASE("deltaring: first capture is a keyframe, then deltas until the interval")
{
    RingFixture f;
    for (int i = 0; i < 5; i++) f.play();

    CHECK(f.ring.count == 5);
    CHECK(f.ring.at(4).kind == RewindDeltaRing::KIND_KEYFRAME);   // capture 1
    CHECK(f.ring.at(3).kind == RewindDeltaRing::KIND_DELTA);      // 2..4
    CHECK(f.ring.at(1).kind == RewindDeltaRing::KIND_DELTA);
    CHECK(f.ring.at(0).kind == RewindDeltaRing::KIND_KEYFRAME);   // capture 5 = interval
}

TEST_CASE("deltaring: every entry reads back the exact state it stored")
{
    RingFixture f;
    std::vector<std::vector<uint8_t>> history;
    for (int i = 0; i < 7; i++) {
        f.play(1 + (i % 2));
        history.push_back(f.state);
    }
    for (int back = 0; back < f.ring.count; back++) {
        uint32_t n = f.ring.read_at(back, f.out.data(), RingFixture::STATE);
        REQUIRE(n == RingFixture::STATE);
        CHECK(memcmp(f.out.data(), history[history.size() - 1 - back].data(), n) == 0);
        uint32_t tag = 0;
        CHECK(f.ring.tag_at(back, &tag));
        CHECK(tag == history.size() - back);
    }
}

TEST_CASE("deltaring: a delta too big for its slot becomes a keyframe")
{
    RingFixture f;
    f.play();          // keyframe
    f.play(3);         // 3 dirty pages > slot capacity of 2
    CHECK(f.ring.at(0).kind == RewindDeltaRing::KIND_KEYFRAME);
    uint32_t n = f.ring.read_at(0, f.out.data(), RingFixture::STATE);
    REQUIRE(n == RingFixture::STATE);
    CHECK(memcmp(f.out.data(), f.state.data(), n) == 0);
}

TEST_CASE("deltaring: group eviction never strands a delta without its keyframe")
{
    RingFixture f;
    for (int i = 0; i < 40; i++) f.play();   // far past every capacity

    for (int back = 0; back < f.ring.count; back++) {
        uint32_t n = f.ring.read_at(back, f.out.data(), RingFixture::STATE);
        CHECK(n == RingFixture::STATE);      // every survivor decodes
    }
    CHECK(f.ring.at(f.ring.count - 1).kind == RewindDeltaRing::KIND_KEYFRAME);
}

TEST_CASE("deltaring: rollback keeps the target and forces a fresh keyframe")
{
    RingFixture f;
    for (int i = 0; i < 6; i++) f.play();
    uint32_t targetTag = 0;
    f.ring.tag_at(3, &targetTag);

    f.ring.rollback_to(3);
    CHECK(f.ring.count == 3);
    uint32_t tag = 0;
    CHECK(f.ring.tag_at(0, &tag));
    CHECK(tag == targetTag);

    f.play();
    CHECK(f.ring.at(0).kind == RewindDeltaRing::KIND_KEYFRAME);
}
