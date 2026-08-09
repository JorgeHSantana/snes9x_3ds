#include "doctest.h"
#include "msu1.h"
#include "fixtures.h"
#include <cstring>
#include <cstdint>
using fixtures::put_file;
using fixtures::make_tmpdir;
using fixtures::write_pcm_at;

TEST_CASE("track select opens and validates the pcm; missing track sets AUDIO_ERROR")
{
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);

    // Create Game-1.pcm with loop_point=0 and 32 samples
    std::string pcm1 = dir + "/Game-1.pcm";
    REQUIRE(write_pcm_at(pcm1.c_str(), 0, 32));

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);

    // Load track 1 (port 4 = low byte, port 5 = high byte + triggers load)
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);

    CHECK((st.status & MSU1_FLAG_AUDIO_ERROR) == 0);
    CHECK(st.audio_file != nullptr);
    CHECK(st.current_track == 1);
    CHECK(st.audio_size == 32 * MSU1_BYTES_PER_SAMPLE);
    CHECK((st.status & MSU1_FLAG_AUDIO_PLAYING) == 0);      // load does not autoplay

    // Try to load track 9: absent
    msu1_write_port(st, 4, 9);
    msu1_write_port(st, 5, 0);

    CHECK((st.status & MSU1_FLAG_AUDIO_ERROR) != 0);
    CHECK(st.audio_file == nullptr);

    // Valid load clears error
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);

    CHECK((st.status & MSU1_FLAG_AUDIO_ERROR) == 0);

    msu1_shutdown(st);
}

TEST_CASE("control $2007: play/repeat bits; ignored under AUDIO_ERROR")
{
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);

    // Create Game-1.pcm with loop_point=0 and 32 samples
    std::string pcm1 = dir + "/Game-1.pcm";
    REQUIRE(write_pcm_at(pcm1.c_str(), 0, 32));

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);

    // Load track 1
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);

    // Set play + repeat
    msu1_write_port(st, 7, 0x03);
    CHECK((st.status & MSU1_FLAG_AUDIO_PLAYING) != 0);
    CHECK((st.status & MSU1_FLAG_AUDIO_REPEAT) != 0);

    // Stop
    msu1_write_port(st, 7, 0x00);
    CHECK((st.status & MSU1_FLAG_AUDIO_PLAYING) == 0);

    // Try to load missing track -> error
    msu1_write_port(st, 4, 9);
    msu1_write_port(st, 5, 0);

    // Control write must be ignored under AUDIO_ERROR
    msu1_write_port(st, 7, 0x02);
    CHECK((st.status & MSU1_FLAG_AUDIO_PLAYING) == 0);

    msu1_shutdown(st);
}

TEST_CASE("volume write stores value and fires callback when installed")
{
    static int cb_calls = 0;
    cb_calls = 0;

    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);

    st.volume_changed_cb = [] { cb_calls++; };
    msu1_write_port(st, 6, 200);
    CHECK(st.volume == 200);
    CHECK(cb_calls == 1);

    st.volume_changed_cb = nullptr;
    msu1_write_port(st, 6, 10);      // no crash with null callback
    CHECK(st.volume == 10);

    msu1_shutdown(st);
}

TEST_CASE("pcm with bad magic is rejected as AUDIO_ERROR")
{
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);

    // Write Game-2.pcm with wrong magic
    std::string pcm2 = dir + "/Game-2.pcm";
    put_file(dir, "Game-2.pcm", "XYZ1\x00\x00\x00\x00", 8);

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);

    // Select track 2 (write port 4 = 2, port 5 = 0)
    msu1_write_port(st, 4, 2);
    msu1_write_port(st, 5, 0);

    // Expect AUDIO_ERROR bit set and audio_file == nullptr
    CHECK((st.status & MSU1_FLAG_AUDIO_ERROR) != 0);
    CHECK(st.audio_file == nullptr);

    msu1_shutdown(st);
}
