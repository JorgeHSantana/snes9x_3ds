#include "doctest.h"
#include "msu1.h"
#include "fixtures.h"
#include <cstring>
#include <string>
#include <vector>

using fixtures::make_tmpdir;
using fixtures::put_file;
using fixtures::write_pcm_at;

// Rewind-hold semantics: while deferred, msu1_restore latches without file
// I/O and audio reads are silent; leaving deferred mode applies the newest
// latched snapshot for real.

static std::string setup_game(std::string& dir)
{
    dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);
    REQUIRE(write_pcm_at((dir + "/Game-1.pcm").c_str(), 0, 64));
    REQUIRE(write_pcm_at((dir + "/Game-2.pcm").c_str(), 0, 64));
    return rom;
}

TEST_CASE("deferred restore: latches without touching state, applies on release")
{
    std::string dir;
    std::string rom = setup_game(dir);

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);

    // play track 1 and capture it
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);
    msu1_write_port(st, 7, 0x01);
    Msu1Snapshot snap = {};
    msu1_capture(st, snap);
    CHECK(snap.current_track == 1);

    // move on to track 2
    msu1_write_port(st, 4, 2);
    msu1_write_port(st, 5, 0);
    msu1_write_port(st, 7, 0x01);
    CHECK(st.current_track == 2);

    // rewind hold: restoring the snapshot must NOT change the live state
    msu1_set_restore_deferred(true);
    CHECK(msu1_restore(st, snap) == Msu1Result::Ok);
    CHECK(st.current_track == 2);

    // and audio is held silent while rewinding
    uint8_t buf[64];
    CHECK(msu1_read_audio(st, buf, sizeof(buf)) == 0);

    // release: the latched snapshot applies for real
    msu1_set_restore_deferred(false);
    CHECK(st.current_track == 1);
    CHECK((st.status & MSU1_FLAG_AUDIO_PLAYING) != 0);
    CHECK(msu1_read_audio(st, buf, sizeof(buf)) > 0);

    msu1_shutdown(st);
}

TEST_CASE("deferred restore: only the newest latched snapshot applies")
{
    std::string dir;
    std::string rom = setup_game(dir);

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);

    msu1_write_port(st, 4, 1); msu1_write_port(st, 5, 0);
    Msu1Snapshot snap1 = {}; msu1_capture(st, snap1);
    msu1_write_port(st, 4, 2); msu1_write_port(st, 5, 0);
    Msu1Snapshot snap2 = {}; msu1_capture(st, snap2);

    msu1_set_restore_deferred(true);
    msu1_restore(st, snap2);   // rewind steps land newest-first...
    msu1_restore(st, snap1);   // ...so the LAST latched one is the oldest
    msu1_set_restore_deferred(false);
    CHECK(st.current_track == 1);

    msu1_shutdown(st);
}

TEST_CASE("deferred restore: cancel drops the pending snapshot")
{
    std::string dir;
    std::string rom = setup_game(dir);

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);

    msu1_write_port(st, 4, 1); msu1_write_port(st, 5, 0);
    Msu1Snapshot snap = {}; msu1_capture(st, snap);
    msu1_write_port(st, 4, 2); msu1_write_port(st, 5, 0);

    msu1_set_restore_deferred(true);
    msu1_restore(st, snap);
    msu1_restore_deferred_cancel();

    // nothing pending anymore: state keeps track 2, audio flows again
    CHECK(st.current_track == 2);
    uint8_t buf[16];
    msu1_write_port(st, 7, 0x01);
    CHECK(msu1_read_audio(st, buf, sizeof(buf)) > 0);

    msu1_shutdown(st);
}
