#include "doctest.h"
#include "scratch_ring.h"

namespace {
struct Entry { short v[10]; };   // two tile vertices' worth
Entry entryOf(int n) { Entry e; for (int i = 0; i < 10; i++) e.v[i] = (short)(n * 16 + i); return e; }
struct Ring {
    ScratchRing r;
    Entry mem[SCRATCH_RING_ENTRIES];
    Ring() { scratch_ring_reset(&r); memset(mem, 0xAB, sizeof(mem)); }
    int put(int n, unsigned int frame, bool *wrote) { Entry e = entryOf(n); return scratch_ring_put(&r, mem, sizeof(Entry), &e, frame, wrote); }
};
}

TEST_CASE("scratch ring: an entry already in the ring is reused without a write") {
    Ring g; bool wrote = false;
    const int a = g.put(1, 10, &wrote);
    CHECK(wrote);
    CHECK(memcmp(&g.mem[a], &(const Entry &)entryOf(1), sizeof(Entry)) == 0);
    CHECK(g.put(1, 10, &wrote) == a);
    CHECK_FALSE(wrote);
    CHECK(g.put(1, 11, &wrote) == a);
    CHECK_FALSE(wrote);
}

TEST_CASE("scratch ring: different entries take different slots") {
    Ring g; bool wrote = false;
    const int a = g.put(1, 10, &wrote);
    const int b = g.put(2, 10, &wrote);
    CHECK(wrote);
    CHECK(a != b);
    CHECK(g.put(1, 10, &wrote) == a);
    CHECK_FALSE(wrote);
}

TEST_CASE("scratch ring: what the allocator left is never taken for an entry") {
    ScratchRing r; scratch_ring_reset(&r);
    Entry mem[SCRATCH_RING_ENTRIES];
    const Entry e = entryOf(3);
    for (int i = 0; i < SCRATCH_RING_ENTRIES; i++) mem[i] = e;   // same bytes, never written by the ring
    bool wrote = false;
    scratch_ring_put(&r, mem, sizeof(Entry), &e, 1, &wrote);
    CHECK(wrote);
}

TEST_CASE("scratch ring: a steady frame writes nothing from the second frame on") {
    Ring g; bool wrote = false; int writes = 0;
    for (unsigned int frame = 1; frame <= 200; frame++)
        for (int n = 0; n < 9; n++) { g.put(n, frame, &wrote); if (wrote) writes++; }
    CHECK(writes == 9);
}

TEST_CASE("scratch ring: an entry used in this frame or the one before is not written over") {
    Ring g; bool wrote = false;
    // frame 1 fills the ring but for two entries; frame 2 uses 5 of them again
    for (int n = 0; n < SCRATCH_RING_ENTRIES - 2; n++) g.put(n, 1, &wrote);
    int kept[5];
    for (int n = 0; n < 5; n++) { kept[n] = g.put(40 + n, 2, &wrote); CHECK_FALSE(wrote); }
    // frame 3: 20 new entries. The two never written go first, then entries
    // of frame 1 that frame 2 did not use
    for (int n = 0; n < 20; n++) {
        const int at = g.put(1000 + n, 3, &wrote);
        CHECK(wrote);
        for (int k = 0; k < 5; k++) CHECK(at != kept[k]);
    }
    for (int k = 0; k < 5; k++)
        CHECK(memcmp(&g.mem[kept[k]], &(const Entry &)entryOf(40 + k), sizeof(Entry)) == 0);
}

TEST_CASE("scratch ring: with every entry in use the cursor's entry is written") {
    Ring g; bool wrote = false;
    for (int n = 0; n < SCRATCH_RING_ENTRIES; n++) g.put(n, 1, &wrote);
    const int at = g.put(999, 1, &wrote);
    CHECK(wrote);
    CHECK(at == 0);
    CHECK(memcmp(&g.mem[0], &(const Entry &)entryOf(999), sizeof(Entry)) == 0);
}

TEST_CASE("scratch ring: an entry outside the search window is written again") {
    Ring g; bool wrote = false;
    const int a = g.put(7, 1, &wrote);
    for (int n = 0; n < SCRATCH_RING_WINDOW; n++) g.put(100 + n, 1, &wrote);
    const int b = g.put(7, 5, &wrote);
    CHECK(wrote);
    CHECK(b != a);
    CHECK(memcmp(&g.mem[b], &(const Entry &)entryOf(7), sizeof(Entry)) == 0);
}

TEST_CASE("scratch ring: the frame counter may wrap") {
    Ring g; bool wrote = false;
    const int a = g.put(1, 0xFFFFFFFFu, &wrote);
    for (int n = 0; n < SCRATCH_RING_ENTRIES - 1; n++) g.put(10 + n, 0xFFFFFFFFu, &wrote);
    // frame 0 follows 0xFFFFFFFF: every entry was used in the frame before
    CHECK(g.put(500, 0u, &wrote) == a);   // all in use: the cursor's entry
    // frame 1: the entries of 0xFFFFFFFF are two frames old and free
    const int c = g.put(501, 1u, &wrote);
    CHECK(wrote);
    CHECK(c != a);
}
