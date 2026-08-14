#include "doctest.h"
#include "msu1.h"
#include "fixtures.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using fixtures::make_tmpdir;
using fixtures::put_file;
using fixtures::write_pcm_at;

// Fixtures generated with:
//   ffmpeg -f s16le -ar 44100 -ac 2 -i ramp.raw
//          -metadata MSU1_LOOPPOINT=500 ramp2000-loop500.flac   (one line)
// where ramp.raw holds 2000 frames of L=i, R=-i — the same pattern
// fixtures::write_pcm_at produces, so decode output can be byte-compared.
static const uint32_t kFrames = 2000;
static const uint32_t kLoop   = 500;

static bool copy_file(const char* src, const std::string& dst)
{
    FILE* in = fopen(src, "rb");
    if (!in) return false;
    FILE* out = fopen(dst.c_str(), "wb");
    if (!out) { fclose(in); return false; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in); fclose(out);
    return true;
}

static std::vector<uint8_t> expected_ramp(uint32_t firstFrame, uint32_t frameCount)
{
    std::vector<uint8_t> v(frameCount * MSU1_BYTES_PER_SAMPLE);
    for (uint32_t i = 0; i < frameCount; i++) {
        int16_t l = (int16_t)(firstFrame + i);
        int16_t r = (int16_t)-(int32_t)(firstFrame + i);
        memcpy(&v[i * 4 + 0], &l, 2);
        memcpy(&v[i * 4 + 2], &r, 2);
    }
    return v;
}

static std::string setup_game(std::string& dir)
{
    dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);
    return rom;
}

TEST_CASE("flac fallback loads when the pcm is missing; loop from metadata tag")
{
    std::string dir;
    std::string rom = setup_game(dir);
    REQUIRE(copy_file("fixtures/ramp2000-loop500.flac", dir + "/Game-1.flac"));

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);

    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);

    CHECK((st.status & MSU1_FLAG_AUDIO_ERROR) == 0);
    CHECK(st.audio_file == nullptr);
    CHECK(st.audio_flac != nullptr);
    CHECK(st.audio_size == kFrames * MSU1_BYTES_PER_SAMPLE);
    CHECK(st.audio_loop_point == kLoop);

    msu1_shutdown(st);
}

TEST_CASE("flac decode is byte-identical to the raw pcm it replaces")
{
    std::string dir;
    std::string rom = setup_game(dir);
    REQUIRE(copy_file("fixtures/ramp2000-loop500.flac", dir + "/Game-1.flac"));

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);
    msu1_write_port(st, 7, 0x01);   // play, no repeat
    REQUIRE((st.status & MSU1_FLAG_AUDIO_PLAYING) != 0);

    std::vector<uint8_t> out(kFrames * MSU1_BYTES_PER_SAMPLE);
    uint32_t got = msu1_read_audio(st, out.data(), (uint32_t)out.size());
    CHECK(got == out.size());
    CHECK(out == expected_ramp(0, kFrames));

    // no repeat: track ends exactly at the last sample
    uint8_t extra[64];
    CHECK(msu1_read_audio(st, extra, sizeof(extra)) == 0);
    CHECK((st.status & MSU1_FLAG_AUDIO_PLAYING) == 0);

    msu1_shutdown(st);
}

TEST_CASE("flac loop wraps exactly at the tagged sample")
{
    std::string dir;
    std::string rom = setup_game(dir);
    REQUIRE(copy_file("fixtures/ramp2000-loop500.flac", dir + "/Game-1.flac"));

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);
    msu1_write_port(st, 7, 0x03);   // play + repeat

    // drain the whole track, then 100 more frames past the wrap
    std::vector<uint8_t> out(kFrames * MSU1_BYTES_PER_SAMPLE);
    REQUIRE(msu1_read_audio(st, out.data(), (uint32_t)out.size()) == out.size());

    std::vector<uint8_t> wrapped(100 * MSU1_BYTES_PER_SAMPLE);
    uint32_t got = msu1_read_audio(st, wrapped.data(), (uint32_t)wrapped.size());
    CHECK(got == wrapped.size());
    CHECK(wrapped == expected_ramp(kLoop, 100));
    CHECK((st.status & MSU1_FLAG_AUDIO_PLAYING) != 0);

    msu1_shutdown(st);
}

TEST_CASE("flac without a loop tag loops from the start")
{
    std::string dir;
    std::string rom = setup_game(dir);
    REQUIRE(copy_file("fixtures/ramp2000-notag.flac", dir + "/Game-1.flac"));

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);

    CHECK((st.status & MSU1_FLAG_AUDIO_ERROR) == 0);
    CHECK(st.audio_loop_point == 0);

    msu1_shutdown(st);
}

TEST_CASE("a raw pcm is preferred over a flac with the same number")
{
    std::string dir;
    std::string rom = setup_game(dir);
    REQUIRE(write_pcm_at((dir + "/Game-1.pcm").c_str(), 0, 32));
    REQUIRE(copy_file("fixtures/ramp2000-loop500.flac", dir + "/Game-1.flac"));

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);

    CHECK(st.audio_file != nullptr);
    CHECK(st.audio_flac == nullptr);
    CHECK(st.audio_size == 32 * MSU1_BYTES_PER_SAMPLE);

    msu1_shutdown(st);
}

TEST_CASE("msu1_detect works when the pack's ROM is a .zip (issue #29)")
{
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    // the zip's basename is what matters: FileLoader sets ROMFilename to the
    // zip path, and msu1 strips the last extension to find "<base>.msu"
    std::string rom = put_file(dir, "Game.zip", "PK", 2);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);

    CHECK(msu1_detect(rom.c_str()));

    Msu1State st = {};
    CHECK(msu1_init(st, rom.c_str()) == Msu1Result::Ok);
    msu1_shutdown(st);
}

TEST_CASE("msu1_build_track_path_ext formats <base>-<N><ext>")
{
    char out[64];
    REQUIRE(msu1_build_track_path_ext("/roms/Game", 7, ".flac", out, sizeof(out)));
    CHECK(strcmp(out, "/roms/Game-7.flac") == 0);
}
