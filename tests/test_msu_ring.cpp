#include "doctest.h"
#include "3dsmsuring.h"

#include <vector>

// Byte value derived from a logical position - lets every test assert that
// served bytes came from the right stream offset, wraps included.
static uint8_t pat(uint32_t pos) { return (uint8_t)(pos * 7u + 3u); }

// Producer-side helper: decode-and-append 'want' bytes of the pattern
// stream, honoring producer_chunk's seam bounding like the real producer.
static void fill(MsuAudioRing& r, uint32_t want)
{
    std::vector<uint8_t> scratch(want);
    while (want > 0) {
        uint32_t n = r.producer_chunk(want);
        if (n == 0) { break; }
        for (uint32_t i = 0; i < n; i++) { scratch[i] = pat(r.prod_pos + i); }
        r.append(scratch.data(), n);
        want -= n;
    }
}

static bool served_matches(const std::vector<uint8_t>& dst, uint32_t start_pos,
                           uint32_t n, uint32_t audio_size, uint32_t loop_bytes)
{
    uint32_t pos = start_pos;
    for (uint32_t i = 0; i < n; i++) {
        if (dst[i] != pat(pos)) { return false; }
        pos++;
        if (audio_size > 0 && pos == audio_size) { pos = loop_bytes; }
    }
    return true;
}

TEST_CASE("sequential serve returns appended bytes and advances")
{
    uint8_t storage[64];
    MsuAudioRing r;
    r.init(storage, sizeof(storage));
    r.reset(100, 1000, 0);
    r.producer_ok = true;

    fill(r, 48);
    std::vector<uint8_t> dst(48);
    bool alive = false;
    CHECK(r.serve(100, dst.data(), 16, &alive) == 16);
    CHECK(alive);
    CHECK(served_matches(dst, 100, 16, 1000, 0));
    CHECK(r.serve(116, dst.data(), 32, &alive) == 32);
    CHECK(served_matches(dst, 116, 32, 1000, 0));
    CHECK(r.next_expected == 148);
}

TEST_CASE("append and serve cross the loop seam gapless")
{
    uint8_t storage[64];
    MsuAudioRing r;
    r.init(storage, sizeof(storage));
    // track of 40 bytes looping to 8: stream ...38,39,8,9,...
    r.reset(30, 40, 8);
    r.producer_ok = true;

    fill(r, 40);   // 30..39 then 8..37 (seam-bounded appends)
    CHECK(r.prod_pos == 8 + 30);

    std::vector<uint8_t> dst(40);
    bool alive = false;
    CHECK(r.serve(30, dst.data(), 40, &alive) == 40);
    CHECK(served_matches(dst, 30, 40, 40, 8));
    CHECK(r.next_expected == 8 + 30);
}

TEST_CASE("short loop wraps several times through one window")
{
    uint8_t storage[256];
    MsuAudioRing r;
    r.init(storage, sizeof(storage));
    // 24-byte track looping to 4 - the 200-byte window spans many laps
    r.reset(0, 24, 4);
    r.producer_ok = true;

    fill(r, 200);
    std::vector<uint8_t> dst(200);
    bool alive = false;
    CHECK(r.serve(0, dst.data(), 200, &alive) == 200);
    CHECK(served_matches(dst, 0, 200, 24, 4));
}

TEST_CASE("position jump resets the window and reports a live producer")
{
    uint8_t storage[64];
    MsuAudioRing r;
    r.init(storage, sizeof(storage));
    r.reset(0, 1000, 0);
    r.producer_ok = true;
    fill(r, 64);

    uint32_t gen_before = r.gen;
    std::vector<uint8_t> dst(16);
    bool alive = false;
    CHECK(r.serve(500, dst.data(), 16, &alive) == 0);   // stream jump
    CHECK(alive);
    CHECK(r.gen == gen_before + 1);
    CHECK(r.next_expected == 500);
    CHECK(r.prod_pos == 500);
    CHECK(r.avail == 0);

    // producer refills at the new position; the next serve succeeds
    fill(r, 32);
    CHECK(r.serve(500, dst.data(), 16, &alive) == 16);
    CHECK(served_matches(dst, 500, 16, 1000, 0));
}

TEST_CASE("dead producer surfaces through the alive flag")
{
    uint8_t storage[32];
    MsuAudioRing r;
    r.init(storage, sizeof(storage));
    r.reset(0, 100, 0);
    r.producer_ok = false;

    std::vector<uint8_t> dst(8);
    bool alive = true;
    CHECK(r.serve(0, dst.data(), 8, &alive) == 0);
    CHECK_FALSE(alive);
}

TEST_CASE("producer_chunk bounds at free space and at the seam")
{
    uint8_t storage[32];
    MsuAudioRing r;
    r.init(storage, sizeof(storage));
    r.reset(90, 100, 20);
    r.producer_ok = true;

    CHECK(r.producer_chunk(64) == 10);   // seam at 100 caps the chunk
    fill(r, 10);
    CHECK(r.prod_pos == 20);             // wrapped exactly on the seam
    CHECK(r.producer_chunk(64) == 22);   // free space (32-10) caps it now
    fill(r, 22);
    CHECK(r.producer_chunk(64) == 0);    // ring full
}

TEST_CASE("loop at or past the end restarts at zero like the core")
{
    uint8_t storage[64];
    MsuAudioRing r;
    r.init(storage, sizeof(storage));
    r.reset(0, 40, 40);                  // defensive clamp case
    CHECK(r.loop_bytes == 0);
    r.producer_ok = true;
    fill(r, 60);
    std::vector<uint8_t> dst(60);
    bool alive = false;
    CHECK(r.serve(0, dst.data(), 60, &alive) == 60);
    CHECK(served_matches(dst, 0, 60, 40, 0));
}
