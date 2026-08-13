#include "doctest.h"
#include "msu1.h"
#include "fixtures.h"
#include <cstring>
using fixtures::put_file;
using fixtures::make_tmpdir;

TEST_CASE("msu1_detect: true iff <base>.msu exists next to the ROM")
{
    std::string dir = make_tmpdir();
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    CHECK_FALSE(msu1_detect(rom.c_str()));
    put_file(dir, "Game.msu", "", 0);          // empty data file is valid
    CHECK(msu1_detect(rom.c_str()));
    CHECK_FALSE(msu1_detect(nullptr));
}

TEST_CASE("msu1_init opens the data file and fills state; shutdown closes and zeroes")
{
    std::string dir = make_tmpdir();
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    put_file(dir, "Game.msu", "ABCD", 4);

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);
    CHECK(st.enabled);
    CHECK(st.data_file != nullptr);
    CHECK(st.data_size == 4);
    CHECK(st.audio_file == nullptr);
    CHECK(st.status == MSU1_REVISION);         // no flags set at boot
    CHECK(strcmp(st.base_path, (dir + "/Game").c_str()) == 0);

    msu1_shutdown(st);
    CHECK_FALSE(st.enabled);
    CHECK(st.data_file == nullptr);
    CHECK(st.audio_file == nullptr);
    CHECK(st.status == 0);
}

TEST_CASE("msu1_init without .msu file yields FileMissing and a disabled state")
{
    std::string dir = make_tmpdir();
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    Msu1State st = {};
    CHECK(msu1_init(st, rom.c_str()) == Msu1Result::FileMissing);
    CHECK_FALSE(st.enabled);
    CHECK(st.data_file == nullptr);
}

TEST_CASE("msu1_init validates parameters")
{
    Msu1State st = {};
    CHECK(msu1_init(st, nullptr) == Msu1Result::InvalidParam);
    char long_path[MSU1_MAX_BASE_PATH + 32];
    memset(long_path, 'a', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';
    CHECK(msu1_init(st, long_path) == Msu1Result::InvalidParam);
}

TEST_CASE("double shutdown is safe")
{
    Msu1State st = {};
    msu1_shutdown(st);
    msu1_shutdown(st);
    CHECK(st.data_file == nullptr);
}

TEST_CASE("msu1_soft_reset: stops playback, resets chip regs, keeps data file/enabled/volume")
{
    std::string dir = make_tmpdir();
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    put_file(dir, "Game.msu", "ABCDEFGH", 8);
    std::string pcm1 = dir + "/Game-1.pcm";
    REQUIRE(fixtures::write_pcm_at(pcm1.c_str(), 0, 32));

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);

    // build a busy state: track loaded and playing, volume set, data seeked
    msu1_write_port(st, 4, 1);
    msu1_write_port(st, 5, 0);
    msu1_write_port(st, 6, 150);
    msu1_write_port(st, 7, 0x03);
    msu1_write_port(st, 0, 3);
    msu1_write_port(st, 1, 0);
    msu1_write_port(st, 2, 0);
    msu1_write_port(st, 3, 0);              // seek data stream to 3
    uint8_t buf[8];
    REQUIRE(msu1_read_audio(st, buf, 8) == 8);

    msu1_soft_reset(st);

    CHECK((st.status & MSU1_FLAG_AUDIO_PLAYING) == 0);   // not playing
    CHECK((st.status & MSU1_FLAG_AUDIO_ERROR) == 0);     // no error
    CHECK(st.status == MSU1_REVISION);                   // only revision bits remain
    CHECK(st.current_track == 0);
    CHECK(st.track_latch == 0);
    CHECK(st.data_seek_latch == 0);
    CHECK(st.audio_file == nullptr);
    CHECK(st.audio_play_pos == 0);
    CHECK(st.audio_size == 0);
    CHECK(st.resume_track == 0);
    CHECK(st.resume_pos == 0);
    CHECK(st.data_pos == 0);                             // data stream back at 0
    CHECK(st.data_file != nullptr);                      // data file stays open
    CHECK(st.enabled);                                   // chip stays enabled
    CHECK(st.volume == 150);                             // volume preserved

    // data stream really is at offset 0 again
    CHECK(msu1_read_port(st, 1) == 'A');

    msu1_shutdown(st);
}

TEST_CASE("msu1_soft_reset on an idle/fileless state is safe")
{
    Msu1State st = {};
    msu1_soft_reset(st);                                 // no files, disabled: no crash
    CHECK(st.status == 0);                               // disabled: no revision bits
    st.enabled = true;
    msu1_soft_reset(st);
    CHECK(st.status == MSU1_REVISION);
}
