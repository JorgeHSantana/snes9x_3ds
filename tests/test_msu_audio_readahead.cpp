#include "doctest.h"
#include "3dsmsu.h"
#include "msu1.h"
#include "fixtures.h"
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <string>
using fixtures::put_file;
using fixtures::make_tmpdir;
using fixtures::write_pcm_at;

// End-to-end: msu1 core + readahead producer over real track files, the
// tick driven by the test where the 3DS build runs it on its own thread.
namespace {

struct ReadaheadFixture {
    std::string dir, rom;
    Msu1State st = {};
    uint8_t ring_storage[192];   // small on purpose: forces tick/read cycles
    bool ok = false;

    ReadaheadFixture()
    {
        dir = make_tmpdir();
        if (dir.empty()) { return; }
        rom = put_file(dir, "Game.sfc", "", 0);
        if (rom.empty()) { return; }
        put_file(dir, "Game.msu", "", 0);
        // Game-1.pcm: loop_point=4 samples, 16 samples (64 data bytes)
        if (!write_pcm_at((dir + "/Game-1.pcm").c_str(), 4, 16)) { return; }
        msu3dsAudioReadaheadInit(ring_storage, sizeof(ring_storage));
        if (msu1_init(st, rom.c_str()) != Msu1Result::Ok) { return; }
        ok = true;
    }
    ~ReadaheadFixture()
    {
        msu3dsAudioReadaheadStop();
        msu1_shutdown(st);
    }

    void load_and_play(uint16_t track, uint8_t control = 0x03)
    {
        msu1_write_port(st, 4, (uint8_t)(track & 0xFF));
        msu1_write_port(st, 5, (uint8_t)(track >> 8));
        msu1_write_port(st, 7, control);
    }
};

// The pcm fixture writes each sample's L and R as its sample index, so the
// expected int16 at play position p follows the same loop arithmetic the
// core uses (64-byte track looping to sample 4 -> byte 16).
int16_t expected_sample(uint32_t byte_pos) { return (int16_t)(byte_pos / 4); }

} // namespace

TEST_CASE("readahead serves decoded pcm across the loop seam")
{
    ReadaheadFixture f;
    REQUIRE(f.ok);
    // The notify hook fires inside these port writes; msu1_read_audio must
    // route through the ring (msu1's own decoder stays where load left it).
    f.load_and_play(1);
    msu3dsAudioReadaheadTick();

    uint8_t buf[64] = {};
    uint32_t pos = 0;
    // read 3 laps' worth in small chunks, ticking between reads
    for (int step = 0; step < 20; step++) {
        uint32_t got = msu1_read_audio(f.st, buf, 8);
        if (got == 0) { msu3dsAudioReadaheadTick(); continue; }
        REQUIRE(got == 8);
        for (uint32_t i = 0; i < got; i += 4) {
            int16_t l;
            memcpy(&l, buf + i, 2);
            CHECK(l == expected_sample(pos + i));
        }
        pos += got;
        if (pos == 64) { pos = 16; }   // loop_point 4 samples = byte 16
        msu3dsAudioReadaheadTick();
    }
    CHECK((f.st.status & MSU1_FLAG_AUDIO_PLAYING) != 0);
    CHECK(f.st.audio_read_stalls == 0);
}

TEST_CASE("position jump after pause-resume is served after a refill")
{
    ReadaheadFixture f;
    REQUIRE(f.ok);
    f.load_and_play(1);
    msu3dsAudioReadaheadTick();

    uint8_t buf[64] = {};
    while (msu1_read_audio(f.st, buf, 32) == 0) { msu3dsAudioReadaheadTick(); }

    msu1_write_port(f.st, 7, 0x00);          // pause at 32
    msu1_write_port(f.st, 7, 0x02 | 0x01 | 0x04);   // resume + repeat
    CHECK(f.st.audio_play_pos == 32);

    msu3dsAudioReadaheadTick();
    uint32_t got = 0;
    for (int i = 0; i < 5 && got == 0; i++) {
        got = msu1_read_audio(f.st, buf, 8);
        msu3dsAudioReadaheadTick();
    }
    REQUIRE(got == 8);
    int16_t l;
    memcpy(&l, buf, 2);
    CHECK(l == expected_sample(32));
}

TEST_CASE("track change adopts the new file")
{
    ReadaheadFixture f;
    REQUIRE(f.ok);
    // Game-2.pcm: no loop, 8 samples, values offset by their index too
    REQUIRE(write_pcm_at((f.dir + "/Game-2.pcm").c_str(), 0, 8));

    f.load_and_play(1);
    msu3dsAudioReadaheadTick();
    uint8_t buf[64] = {};
    while (msu1_read_audio(f.st, buf, 16) == 0) { msu3dsAudioReadaheadTick(); }

    f.load_and_play(2);
    msu3dsAudioReadaheadTick();
    uint32_t got = 0;
    for (int i = 0; i < 5 && got == 0; i++) {
        got = msu1_read_audio(f.st, buf, 8);
        msu3dsAudioReadaheadTick();
    }
    REQUIRE(got == 8);
    int16_t l;
    memcpy(&l, buf, 2);
    CHECK(l == expected_sample(0));
}

TEST_CASE("flac track loops gapless through the readahead")
{
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);
    // flac fixture: 2000-sample ramp, MSU1_LOOPPOINT=500
    {
        FILE* src = fopen("fixtures/ramp2000-loop500.flac", "rb");
        REQUIRE(src != nullptr);
        std::string dstp = dir + "/Game-1.flac";
        FILE* dst = fopen(dstp.c_str(), "wb");
        REQUIRE(dst != nullptr);
        char tmp[4096];
        size_t n;
        while ((n = fread(tmp, 1, sizeof(tmp), src)) > 0) { fwrite(tmp, 1, n, dst); }
        fclose(src);
        fclose(dst);
    }

    static uint8_t storage[4096];
    msu3dsAudioReadaheadInit(storage, sizeof(storage));
    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);
    msu1_write_port(st, 7, 0x03);
    msu3dsAudioReadaheadTick();

    // stream to just before the end (2000 samples = 8000 bytes), then across
    uint8_t buf[512] = {};
    uint32_t streamed = 0;
    while (streamed < 7936) {
        uint32_t got = msu1_read_audio(st, buf, 256);
        if (got == 0) { msu3dsAudioReadaheadTick(); continue; }
        streamed += got;
    }
    // next 256 bytes cross the seam: ...1999, then 500, 501...
    uint32_t got = 0;
    for (int i = 0; i < 10 && got < 256; i++) {
        got += msu1_read_audio(st, buf + got, 256 - got);
        msu3dsAudioReadaheadTick();
    }
    REQUIRE(got == 256);
    int16_t before, after;
    memcpy(&before, buf + (8000 - 7936 - 4), 2);   // last sample of the lap
    memcpy(&after,  buf + (8000 - 7936), 2);       // first sample past the seam
    CHECK(before == 1999);
    CHECK(after == 500);

    msu3dsAudioReadaheadStop();
    msu1_shutdown(st);
}

TEST_CASE("track change never serves the previous track's samples")
{
    ReadaheadFixture f;
    REQUIRE(f.ok);
    // Game-2.pcm hand-built with values 1000+i so any stale track-1 byte
    // (values 0..15) is unmistakable.
    {
        std::string p2 = f.dir + "/Game-2.pcm";
        FILE* fp = fopen(p2.c_str(), "wb");
        REQUIRE(fp != nullptr);
        uint32_t loop = 0;
        fwrite("MSU1", 1, 4, fp);
        fwrite(&loop, 4, 1, fp);
        for (int i = 0; i < 12; i++) {
            int16_t v = (int16_t)(1000 + i);
            fwrite(&v, 2, 1, fp);   // L
            fwrite(&v, 2, 1, fp);   // R
        }
        fclose(fp);
    }

    f.load_and_play(1);
    msu3dsAudioReadaheadTick();
    uint8_t buf[64] = {};
    while (msu1_read_audio(f.st, buf, 32) == 0) { msu3dsAudioReadaheadTick(); }

    // switch mid-playback; from here on every served sample must be >= 1000
    f.load_and_play(2);
    uint32_t served = 0;
    for (int i = 0; i < 30 && served < 32; i++) {
        uint32_t got = msu1_read_audio(f.st, buf + served, 8);
        for (uint32_t b = 0; b < got; b += 4) {
            int16_t l;
            memcpy(&l, buf + served + b, 2);
            CHECK(l >= 1000);
        }
        served += got;
        msu3dsAudioReadaheadTick();
    }
    REQUIRE(served == 32);
}

TEST_CASE("vanished track file stalls the stream out honestly")
{
    ReadaheadFixture f;
    REQUIRE(f.ok);
    f.load_and_play(1);
    REQUIRE(remove((f.dir + "/Game-1.pcm").c_str()) == 0);
    // force the producer to reopen: a fresh adopt with the file gone
    msu1_write_port(f.st, 7, 0x00);
    msu1_write_port(f.st, 7, 0x03);
    msu3dsAudioReadaheadTick();

    uint8_t buf[64] = {};
    for (uint32_t i = 0; i <= MSU1_AUDIO_STALL_LIMIT + 1; i++) {
        msu1_read_audio(f.st, buf, 16);
        msu3dsAudioReadaheadTick();
    }
    CHECK((f.st.status & MSU1_FLAG_AUDIO_PLAYING) == 0);
}
