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

// ---- $2007 bit 2: resume (byuu spec) -------------------------------------
// Pausing (play=0 while playing) stores resume_track/resume_pos. A later
// $2007 with play=1 + bit2=1 on the SAME track resumes from the stored
// position; any other play starts from the beginning of the track.

static Msu1State make_paused_state(std::string& dir_out)
{
    dir_out = make_tmpdir();
    REQUIRE_FALSE(dir_out.empty());
    std::string rom = put_file(dir_out, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir_out, "Game.msu", "", 0);
    std::string pcm1 = dir_out + "/Game-1.pcm";
    REQUIRE(write_pcm_at(pcm1.c_str(), 0, 64));

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);
    msu1_write_port(st, 7, 0x02);            // play

    uint8_t buf[32];
    REQUIRE(msu1_read_audio(st, buf, 32) == 32);   // frames 0..7 consumed
    msu1_write_port(st, 7, 0x00);            // pause
    return st;
}

TEST_CASE("$2007 pause stores the resume position and track")
{
    std::string dir;
    Msu1State st = make_paused_state(dir);

    CHECK((st.status & MSU1_FLAG_AUDIO_PLAYING) == 0);
    CHECK(st.resume_track == 1);
    CHECK(st.resume_pos == 32);

    msu1_shutdown(st);
}

TEST_CASE("$2007 play+resume continues byte-exact; plain play restarts from the top")
{
    std::string dir;
    Msu1State st = make_paused_state(dir);

    // play + resume (bit2): back to byte 32 => next frame is 8
    msu1_write_port(st, 7, 0x06);
    CHECK((st.status & MSU1_FLAG_AUDIO_PLAYING) != 0);
    CHECK(st.audio_play_pos == 32);
    uint8_t frame[4];
    REQUIRE(msu1_read_audio(st, frame, 4) == 4);
    int16_t l, r;
    memcpy(&l, frame, 2); memcpy(&r, frame + 2, 2);
    CHECK(l == 8);
    CHECK(r == -8);

    // pause again (stores pos 36), then PLAIN play: restarts from frame 0
    msu1_write_port(st, 7, 0x00);
    CHECK(st.resume_pos == 36);
    msu1_write_port(st, 7, 0x02);
    CHECK(st.audio_play_pos == 0);
    REQUIRE(msu1_read_audio(st, frame, 4) == 4);
    memcpy(&l, frame, 2);
    CHECK(l == 0);

    msu1_shutdown(st);
}

TEST_CASE("$2007 resume with a mismatched track plays that track from the start")
{
    std::string dir;
    Msu1State st = make_paused_state(dir);   // resume stored for track 1 @ 32

    std::string pcm2 = dir + "/Game-2.pcm";
    REQUIRE(write_pcm_at(pcm2.c_str(), 0, 64));
    msu1_write_port(st, 4, 2);
    msu1_write_port(st, 5, 0);               // load track 2 (does NOT clear resume)
    CHECK(st.resume_track == 1);
    CHECK(st.resume_pos == 32);

    msu1_write_port(st, 7, 0x06);            // resume bit, but track differs
    CHECK(st.audio_play_pos == 0);           // from the start

    msu1_shutdown(st);
}

TEST_CASE("$2007 resume with a stored position beyond the track plays from the start")
{
    std::string dir;
    Msu1State st = make_paused_state(dir);

    st.resume_pos = st.audio_size + 4;       // corrupt/oversized stored position
    msu1_write_port(st, 7, 0x06);
    CHECK((st.status & MSU1_FLAG_AUDIO_PLAYING) != 0);
    CHECK(st.audio_play_pos == 0);

    msu1_shutdown(st);
}

TEST_CASE("snapshot round-trip preserves resume fields; resume works after restore")
{
    std::string dir;
    Msu1State st = make_paused_state(dir);   // resume: track 1 @ byte 32

    Msu1Snapshot snap = {};
    msu1_capture(st, snap);
    CHECK(snap.resume_track == 1);
    CHECK(snap.resume_pos == 32);

    std::string rom = dir + "/Game.sfc";
    Msu1State st2 = {};
    REQUIRE(msu1_init(st2, rom.c_str()) == Msu1Result::Ok);
    REQUIRE(msu1_restore(st2, snap) == Msu1Result::Ok);
    CHECK(st2.resume_track == 1);
    CHECK(st2.resume_pos == 32);

    msu1_write_port(st2, 7, 0x06);           // resume on the restored state
    CHECK(st2.audio_play_pos == 32);
    uint8_t frame[4];
    REQUIRE(msu1_read_audio(st2, frame, 4) == 4);
    int16_t l;
    memcpy(&l, frame, 2);
    CHECK(l == 8);

    msu1_shutdown(st);
    msu1_shutdown(st2);
}
