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
