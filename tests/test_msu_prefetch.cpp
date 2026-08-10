#include "doctest.h"
#include "3dsmsu.h"
#include "fake_backend.h"
#include "fixtures.h"
#include <cstring>

using fixtures::make_tmpdir;
using fixtures::put_file;

static int16_t staging[fake::CAP_SAMPLES * 2];
static int g_locks = 0, g_unlocks = 0;
static void count_lock(void)   { g_locks++; }
static void count_unlock(void) { g_unlocks++; }

// A game dir with an .msu data track of n pattern bytes (i*7+3 mod 256).
static std::string make_msu_game(int n)
{
    std::string dir = make_tmpdir();
    std::string bytes;
    for (int i = 0; i < n; i++) { bytes.push_back((char)((i * 7 + 3) & 0xFF)); }
    put_file(dir, "game.msu", bytes.data(), bytes.size());
    return dir;
}

static uint8_t expected_byte(uint32_t pos) { return (uint8_t)((pos * 7 + 3) & 0xFF); }

static void fresh(uint8_t* storage, uint32_t cap, const std::string& dir)
{
    fake::reset();
    msu3dsFinalize();
    msu3dsDataPrefetchLocks(count_lock, count_unlock);
    msu3dsDataPrefetchInit(storage, cap);
    REQUIRE(msu3dsInitialize(fake::make(), staging, fake::CAP_SAMPLES));
    REQUIRE(msu1_init(MSU1, (dir + "/game.sfc").c_str()) == Msu1Result::Ok);
}

TEST_CASE("prefetch: miss falls back to fread; fill turns later reads into hits")
{
    std::string dir = make_msu_game(4096);
    static uint8_t storage[1024];
    fresh(storage, sizeof(storage), dir);

    // cold read: ring empty -> miss (re-bases window) -> fallback fread
    uint8_t buf[128];
    REQUIRE(msu1_read_data_bulk(MSU1, buf, 64) == 64);
    for (int i = 0; i < 64; i++) { CHECK(buf[i] == expected_byte(i)); }

    // producer tick: fills from data_pos (=64, the re-based window start... 
    // actually the miss re-based to 0; data_pos advanced to 64 -> next miss
    // re-bases to 64, then a fill serves from there)
    msu3dsDataPrefetchFill();
    uint32_t got = msu1_read_data_bulk(MSU1, buf, 64);
    REQUIRE(got == 64);
    for (int i = 0; i < 64; i++) { CHECK(buf[i] == expected_byte(64 + i)); }

    msu3dsDataPrefetchFill();
    got = msu1_read_data_bulk(MSU1, buf, 128);
    REQUIRE(got == 128);
    for (int i = 0; i < 128; i++) { CHECK(buf[i] == expected_byte(128 + i)); }

    CHECK(g_locks == g_unlocks);   // leaf lock always balanced
    msu1_shutdown(MSU1);
}

TEST_CASE("prefetch: every byte identical to plain reads across wraps and seeks")
{
    std::string dir = make_msu_game(4096);
    static uint8_t storage[256];              // tiny ring -> constant wrapping
    fresh(storage, sizeof(storage), dir);

    uint8_t buf[96];
    uint32_t pos = 0;
    while (pos < 4096) {
        msu3dsDataPrefetchFill();
        uint32_t want = 96;
        if (want > 4096 - pos) { want = 4096 - pos; }
        uint32_t got = msu1_read_data_bulk(MSU1, buf, want);
        REQUIRE(got == want);
        for (uint32_t i = 0; i < got; i++) {
            CAPTURE(pos); CAPTURE(i);
            REQUIRE(buf[i] == expected_byte(pos + i));
        }
        pos += got;
    }
    CHECK(msu1_read_data_bulk(MSU1, buf, 1) == 0);   // EOF

    // seek back via ports 0-3, re-read with the ring active
    msu1_write_port(MSU1, 0, 0x10);
    msu1_write_port(MSU1, 1, 0x00);
    msu1_write_port(MSU1, 2, 0x00);
    msu1_write_port(MSU1, 3, 0x00);                  // data_pos = 0x10
    msu3dsDataPrefetchFill();
    REQUIRE(msu1_read_data_bulk(MSU1, buf, 32) == 32);
    for (int i = 0; i < 32; i++) { CHECK(buf[i] == expected_byte(0x10 + i)); }
    msu1_shutdown(MSU1);
}

TEST_CASE("prefetch: port-1 single reads stay correct after ring-served bulk reads")
{
    std::string dir = make_msu_game(1024);
    static uint8_t storage[512];
    fresh(storage, sizeof(storage), dir);

    uint8_t buf[64];
    REQUIRE(msu1_read_data_bulk(MSU1, buf, 64) == 64);   // miss+fallback, pos=64
    msu3dsDataPrefetchFill();                             // window from 64... (miss re-based)
    REQUIRE(msu1_read_data_bulk(MSU1, buf, 64) == 64);   // pos=128 (may be ring-served)
    // the FILE* position may now lag data_pos; port-1 must still see byte 128
    CHECK(msu1_read_port(MSU1, 1) == expected_byte(128));
    CHECK(msu1_read_port(MSU1, 1) == expected_byte(129));
    msu1_shutdown(MSU1);
}

TEST_CASE("prefetch disabled (no storage): everything still works via fread")
{
    std::string dir = make_msu_game(256);
    fresh(nullptr, 0, dir);
    uint8_t buf[256];
    REQUIRE(msu1_read_data_bulk(MSU1, buf, 256) == 256);
    for (int i = 0; i < 256; i++) { CHECK(buf[i] == expected_byte(i)); }
    msu1_shutdown(MSU1);
}
