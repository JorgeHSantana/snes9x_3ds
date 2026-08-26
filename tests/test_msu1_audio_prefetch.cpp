#include "doctest.h"
#include "msu1.h"
#include "fixtures.h"
#include <cstring>
#include <cstdint>
using fixtures::put_file;
using fixtures::make_tmpdir;
using fixtures::write_pcm_at;

// Scripted audio-prefetch source. Serves pat(pos) bytes while 'serving' is
// true; otherwise returns 0 with the configured 'alive'. Counts calls and
// remembers the last requested position so tests can assert the core's
// position arithmetic (loop wrap included) without a real ring.
namespace {
bool     g_serving = true;
bool     g_alive   = true;
uint32_t g_calls   = 0;
uint32_t g_last_pos = 0;

uint8_t pat(uint32_t pos) { return (uint8_t)(pos * 5u + 1u); }

uint32_t scripted_read(uint32_t pos, uint8_t* dst, uint32_t count, bool* alive)
{
    g_calls++;
    g_last_pos = pos;
    if (alive != nullptr) { *alive = g_alive; }
    if (!g_serving) { return 0; }
    for (uint32_t i = 0; i < count; i++) { dst[i] = pat(pos + i); }
    return count;
}

struct PrefetchGuard {
    // notify must never be required for the core to work; tests leave it null
    PrefetchGuard() { msu1_set_audio_prefetch(scripted_read);
                      g_serving = true; g_alive = true; g_calls = 0; }
    ~PrefetchGuard() { msu1_set_audio_prefetch(nullptr); }
};

// Track fixture: Game-1.pcm, loop_point=4 samples (16 bytes), 16 samples
// (64 data bytes) - same shape the plain audio tests use.
struct TrackFixture {
    std::string dir, rom;
    Msu1State st = {};
    bool ok = false;
    TrackFixture()
    {
        dir = make_tmpdir();
        if (dir.empty()) { return; }
        rom = put_file(dir, "Game.sfc", "", 0);
        if (rom.empty()) { return; }
        put_file(dir, "Game.msu", "", 0);
        std::string pcm1 = dir + "/Game-1.pcm";
        if (!write_pcm_at(pcm1.c_str(), 4, 16)) { return; }
        if (msu1_init(st, rom.c_str()) != Msu1Result::Ok) { return; }
        msu1_write_port(st, 4, 1);
        msu1_write_port(st, 5, 0);
        ok = true;
    }
    ~TrackFixture() { msu1_shutdown(st); }
};
} // namespace

TEST_CASE("prefetch serves reads and the loop seam wraps position-only")
{
    PrefetchGuard hook;
    TrackFixture t;
    REQUIRE(t.ok);

    msu1_write_port(t.st, 7, 0x03);   // play + repeat

    uint8_t buf[256] = {};
    CHECK(msu1_read_audio(t.st, buf, 64) == 64);
    CHECK(buf[0] == pat(0));
    CHECK(t.st.audio_play_pos == 64);          // wrap is lazy: applied on the next read

    // 64 bytes = whole track; the next read starts at the loop point
    // (4 samples * 4 bytes) and must be served from there.
    CHECK(msu1_read_audio(t.st, buf, 8) == 8);
    CHECK(g_last_pos == 16);
    CHECK(buf[0] == pat(16));
    CHECK((t.st.status & MSU1_FLAG_AUDIO_PLAYING) != 0);
}

TEST_CASE("alive shortfall pads silence without burning the stall budget")
{
    PrefetchGuard hook;
    TrackFixture t;
    REQUIRE(t.ok);

    msu1_write_port(t.st, 7, 0x03);
    g_serving = false;                 // ring catching up
    g_alive = true;

    uint8_t buf[64] = {};
    for (int i = 0; i < 200; i++) {    // far past MSU1_AUDIO_STALL_LIMIT
        CHECK(msu1_read_audio(t.st, buf, 16) == 0);
    }
    CHECK(t.st.audio_read_stalls == 0);
    CHECK((t.st.status & MSU1_FLAG_AUDIO_PLAYING) != 0);

    g_serving = true;                  // producer landed the data
    CHECK(msu1_read_audio(t.st, buf, 16) == 16);
    CHECK(buf[0] == pat(0));
}

TEST_CASE("dead producer stalls out and stops the track")
{
    PrefetchGuard hook;
    TrackFixture t;
    REQUIRE(t.ok);

    msu1_write_port(t.st, 7, 0x03);
    g_serving = false;
    g_alive = false;                   // producer reported a persistent error

    uint8_t buf[64] = {};
    for (uint32_t i = 0; i <= MSU1_AUDIO_STALL_LIMIT + 1; i++) {
        msu1_read_audio(t.st, buf, 16);
    }
    CHECK((t.st.status & MSU1_FLAG_AUDIO_PLAYING) == 0);
}

TEST_CASE("play validates the resume position without seeking the decoder")
{
    PrefetchGuard hook;
    TrackFixture t;
    REQUIRE(t.ok);

    // pause at 32 then resume (0x07 = resume + repeat + play)
    msu1_write_port(t.st, 7, 0x03);
    uint8_t buf[64] = {};
    CHECK(msu1_read_audio(t.st, buf, 32) == 32);
    msu1_write_port(t.st, 7, 0x00);    // pause stores resume_pos = 32
    msu1_write_port(t.st, 7, 0x07);
    CHECK((t.st.status & MSU1_FLAG_AUDIO_PLAYING) != 0);
    CHECK(t.st.audio_play_pos == 32);

    CHECK(msu1_read_audio(t.st, buf, 8) == 8);
    CHECK(g_last_pos == 32);
    CHECK(buf[0] == pat(32));
}

TEST_CASE("snapshot restore keeps its position with the prefetch active")
{
    PrefetchGuard hook;
    TrackFixture t;
    REQUIRE(t.ok);

    msu1_write_port(t.st, 7, 0x03);
    uint8_t buf[64] = {};
    CHECK(msu1_read_audio(t.st, buf, 24) == 24);

    Msu1Snapshot snap = {};
    msu1_capture(t.st, snap);
    CHECK(msu1_restore(t.st, snap) == Msu1Result::Ok);
    CHECK(t.st.audio_play_pos == 24);
    CHECK((t.st.status & MSU1_FLAG_AUDIO_PLAYING) != 0);

    CHECK(msu1_read_audio(t.st, buf, 8) == 8);
    CHECK(g_last_pos == 24);
}
