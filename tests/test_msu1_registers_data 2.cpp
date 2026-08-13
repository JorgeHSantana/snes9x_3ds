#include "doctest.h"
#include "msu1.h"
#include "fixtures.h"
#include <cstring>
#include <cstdint>
using fixtures::put_file;
using fixtures::make_tmpdir;

TEST_CASE("data port: seek + sequential reads + EOF behavior")
{
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());

    // Create Game.msu with bytes 0x10..0x1F
    char data[16];
    for (int i = 0; i < 16; i++) { data[i] = (char)(0x10 + i); }
    std::string msu = put_file(dir, "Game.msu", data, 16);
    REQUIRE_FALSE(msu.empty());

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);

    // seek to offset 4: write $2000..$2003 little-endian
    msu1_write_port(st, 0, 0x04);
    msu1_write_port(st, 1, 0x00);
    msu1_write_port(st, 2, 0x00);
    msu1_write_port(st, 3, 0x00);          // triggers the seek
    CHECK(msu1_read_port(st, 1) == 0x14);  // data[4]
    CHECK(msu1_read_port(st, 1) == 0x15);  // auto-increment

    // seek past EOF clamps; reads return 0
    msu1_write_port(st, 0, 0xFF);
    msu1_write_port(st, 1, 0xFF);
    msu1_write_port(st, 2, 0x00);
    msu1_write_port(st, 3, 0x00);
    CHECK(msu1_read_port(st, 1) == 0x00);

    msu1_shutdown(st);
}

TEST_CASE("status read returns revision; ID string on ports 2-7")
{
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);

    Msu1State st = {};
    REQUIRE(msu1_init(st, rom.c_str()) == Msu1Result::Ok);

    CHECK(msu1_read_port(st, 0) == MSU1_REVISION);
    CHECK(msu1_read_port(st, 2) == 'S');
    CHECK(msu1_read_port(st, 3) == '-');
    CHECK(msu1_read_port(st, 4) == 'M');
    CHECK(msu1_read_port(st, 5) == 'S');
    CHECK(msu1_read_port(st, 6) == 'U');
    CHECK(msu1_read_port(st, 7) == '1');
    msu1_shutdown(st);
}

TEST_CASE("port interface validates: out-of-range port, disabled state")
{
    Msu1State st = {};                       // never initialized
    CHECK(msu1_read_port(st, 1) == 0x00);    // disabled -> harmless zero
    msu1_write_port(st, 3, 0xFF);            // must not crash
    Msu1State st2 = {};
    CHECK(msu1_read_port(st2, 8) == 0x00);   // invalid port
    msu1_write_port(st2, 200, 1);            // must not crash
}
