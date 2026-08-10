#include "doctest.h"
#include "msu1.h"
#include "fixtures.h"
#include <cstring>
#include <cstdint>
using fixtures::put_file;
using fixtures::make_tmpdir;
using fixtures::write_pcm_at;

TEST_CASE("read_audio streams, loops at loop point when repeating")
{
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);

    // Create Game-1.pcm: loop_point=4, 16 samples (64 data bytes)
    std::string pcm1 = dir + "/Game-1.pcm";
    REQUIRE(write_pcm_at(pcm1.c_str(), 4, 16));

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);

    // Load track 1 (port 4 = low byte, port 5 = high byte + triggers load)
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);

    // Play with repeat (0x03 = repeat + play)
    msu1_write_port(st, 7, 0x03);

    // Read 64 bytes (all 16 samples)
    uint8_t buf[256] = {};
    uint32_t got = msu1_read_audio(st, buf, 64);
    CHECK(got == 64);

    // Next read wraps to sample 4 (loop_point)
    got = msu1_read_audio(st, buf, 8);
    CHECK(got == 8);

    // Verify: first sample in wrapped buffer is int16 value 4
    int16_t l0;
    memcpy(&l0, buf, 2);
    CHECK(l0 == 4);

    // Still playing
    CHECK((st.status & MSU1_FLAG_AUDIO_PLAYING) != 0);

    msu1_shutdown(st);
}

TEST_CASE("read_audio without repeat stops at end and clears PLAYING")
{
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);

    // Create Game-1.pcm: loop_point=4, 16 samples (64 data bytes)
    std::string pcm1 = dir + "/Game-1.pcm";
    REQUIRE(write_pcm_at(pcm1.c_str(), 4, 16));

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);

    // Load track 1
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);

    // Play without repeat (0x02 = play only, no repeat)
    msu1_write_port(st, 7, 0x01);

    // Request 256 bytes, but only 64 available
    uint8_t buf[256] = {};
    uint32_t got = msu1_read_audio(st, buf, 256);
    CHECK(got == 64);

    // PLAYING should be cleared
    CHECK((st.status & MSU1_FLAG_AUDIO_PLAYING) == 0);

    // Subsequent reads return 0
    CHECK(msu1_read_audio(st, buf, 64) == 0);

    msu1_shutdown(st);
}

TEST_CASE("read_audio validates parameters")
{
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);

    // Create Game-1.pcm: loop_point=4, 16 samples
    std::string pcm1 = dir + "/Game-1.pcm";
    REQUIRE(write_pcm_at(pcm1.c_str(), 4, 16));

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);

    // Load track 1 and play
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);
    msu1_write_port(st, 7, 0x03);

    // Test: nullptr output buffer
    CHECK(msu1_read_audio(st, nullptr, 64) == 0);

    // Test: max_bytes == 0
    uint8_t buf[8] = {};
    CHECK(msu1_read_audio(st, buf, 0) == 0);

    // Test: disabled state (all zeros)
    Msu1State idle = {};
    CHECK(msu1_read_audio(idle, buf, 8) == 0);

    msu1_shutdown(st);
}

TEST_CASE("zero-length pcm with repeat does not hang; read returns 0 and clears PLAYING")
{
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);

    // Create Game-1.pcm: loop_point=0, 0 samples (8-byte file, zero audio data)
    std::string pcm1 = dir + "/Game-1.pcm";
    REQUIRE(write_pcm_at(pcm1.c_str(), 0, 0));

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);

    // Load track 1 (must NOT set AUDIO_ERROR on 8-byte PCM with valid magic)
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);
    CHECK((st.status & MSU1_FLAG_AUDIO_ERROR) == 0);
    CHECK(st.audio_file != nullptr);

    // Play with repeat
    msu1_write_port(st, 7, 0x03);

    // Read should return 0 and clear PLAYING (not spin)
    uint8_t buf[16] = {};
    uint32_t got = msu1_read_audio(st, buf, 16);
    CHECK(got == 0);
    CHECK((st.status & MSU1_FLAG_AUDIO_PLAYING) == 0);

    msu1_shutdown(st);
}

TEST_CASE("transient zero-byte read stalls but does not stop the track")
{
    std::string dir = fixtures::make_tmpdir();
    Msu1State st = {};
    st.enabled = true;
    snprintf(st.base_path, sizeof(st.base_path), "%s/game", dir.c_str());
    REQUIRE(fixtures::write_pcm_at((dir + "/game-1.pcm").c_str(), 0, 512));
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);
    msu1_write_port(st, 7, 0x01);
    REQUIRE((st.status & MSU1_FLAG_AUDIO_PLAYING) != 0);

    // Claim more bytes than the file really has: after the real bytes are
    // consumed, fread hits EOF with remaining_in_track > 0 -> the
    // zero-read stall path (same shape as a transient SD read failure).
    st.audio_size += 4096;
    uint8_t buf[4096];
    uint32_t total = 0;
    while (total < 512 * MSU1_BYTES_PER_SAMPLE) {
        uint32_t got = msu1_read_audio(st, buf, sizeof(buf));
        if (got == 0) { break; }
        total += got;
    }
    CHECK(total == 512 * MSU1_BYTES_PER_SAMPLE);         // real bytes all served
    CHECK(msu1_read_audio(st, buf, sizeof(buf)) == 0);   // stall begins
    CHECK((st.status & MSU1_FLAG_AUDIO_PLAYING) != 0);   // still alive
    CHECK(st.audio_read_stalls >= 1);

    // Persistent failure eventually stops it.
    for (uint32_t i = 0; i < MSU1_AUDIO_STALL_LIMIT + 2; i++) {
        msu1_read_audio(st, buf, sizeof(buf));
    }
    CHECK((st.status & MSU1_FLAG_AUDIO_PLAYING) == 0);
    msu1_shutdown(st);
}
