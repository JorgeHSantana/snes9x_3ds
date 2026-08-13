#include "doctest.h"
#include "msu1.h"
#include "fixtures.h"
#include <cstring>
#include <cstdint>
#include <cstdio>
using fixtures::put_file;
using fixtures::make_tmpdir;
using fixtures::write_pcm_at;

TEST_CASE("capture/restore round-trips play position, track, volume, flags")
{
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "ABCDEFGH", 8);

    std::string pcm1 = dir + "/Game-1.pcm";
    REQUIRE(write_pcm_at(pcm1.c_str(), 0, 32));

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);

    // load track 1
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);

    // volume 180
    msu1_write_port(st, 6, 180);

    // play + repeat
    msu1_write_port(st, 7, 0x03);

    // advance audio position by 40 bytes
    uint8_t buf[40];
    uint32_t got = msu1_read_audio(st, buf, 40);
    REQUIRE(got == 40);

    // seek data stream to offset 3, then read one byte -> data_pos becomes 4
    msu1_write_port(st, 0, 3);
    msu1_write_port(st, 1, 0);
    msu1_write_port(st, 2, 0);
    msu1_write_port(st, 3, 0);          // triggers the seek
    msu1_read_port(st, 1);              // reads data[3], data_pos -> 4

    Msu1Snapshot snap = {};
    msu1_capture(st, snap);
    CHECK(snap.current_track == 1);
    CHECK(snap.audio_play_pos == 40);
    CHECK(snap.data_pos == 4);
    CHECK(snap.volume == 180);

    Msu1State st2 = {};
    REQUIRE(msu1_init(st2, rom.c_str()) == Msu1Result::Ok);
    REQUIRE(msu1_restore(st2, snap) == Msu1Result::Ok);
    CHECK(st2.current_track == 1);
    CHECK(st2.audio_play_pos == 40);
    CHECK(st2.volume == 180);
    CHECK((st2.status & MSU1_FLAG_AUDIO_PLAYING) != 0);

    uint8_t a[4], b[4];
    CHECK(msu1_read_audio(st, a, 4) == 4);
    CHECK(msu1_read_audio(st2, b, 4) == 4);
    CHECK(memcmp(a, b, 4) == 0);        // identical continuation

    msu1_shutdown(st);
    msu1_shutdown(st2);
}

TEST_CASE("restore with the pcm file deleted stops audio safely")
{
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "ABCDEFGH", 8);

    std::string pcm1 = dir + "/Game-1.pcm";
    REQUIRE(write_pcm_at(pcm1.c_str(), 0, 32));

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);
    msu1_write_port(st, 7, 0x03);        // play + repeat

    uint8_t buf[16];
    REQUIRE(msu1_read_audio(st, buf, 16) == 16);

    Msu1Snapshot snap = {};
    msu1_capture(st, snap);
    CHECK((snap.status & MSU1_FLAG_AUDIO_PLAYING) != 0);

    // the pcm file backing the captured track disappears before restore
    REQUIRE(remove(pcm1.c_str()) == 0);

    msu1_shutdown(st);
    Msu1State st2 = {};
    REQUIRE(msu1_init(st2, rom.c_str()) == Msu1Result::Ok);

    CHECK(msu1_restore(st2, snap) == Msu1Result::FileMissing);
    CHECK((st2.status & MSU1_FLAG_AUDIO_PLAYING) == 0);
    CHECK((st2.status & MSU1_FLAG_AUDIO_ERROR) != 0);
    CHECK(st2.audio_file == nullptr);

    msu1_shutdown(st2);
}

TEST_CASE("restore validates snapshot ranges (position beyond file clamps to stop)")
{
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "ABCDEFGH", 8);

    // track file exists for this test (separate tmpdir avoids ordering issues)
    std::string pcm1 = dir + "/Game-1.pcm";
    REQUIRE(write_pcm_at(pcm1.c_str(), 0, 32));

    Msu1State st2 = {};
    REQUIRE(msu1_init(st2, rom.c_str()) == Msu1Result::Ok);

    Msu1Snapshot bad = {};
    bad.current_track = 1;
    bad.status = MSU1_FLAG_AUDIO_PLAYING;
    bad.audio_play_pos = 0xFFFFFFFF;

    CHECK(msu1_restore(st2, bad) == Msu1Result::Ok);      // track exists...
    CHECK((st2.status & MSU1_FLAG_AUDIO_PLAYING) == 0);   // ...but position invalid: stopped

    msu1_shutdown(st2);
}
