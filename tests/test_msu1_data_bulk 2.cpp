#include "doctest.h"
#include "msu1.h"
#include "fixtures.h"
#include <cstring>

using fixtures::put_file;
using fixtures::make_tmpdir;

// Builds a state with a data file of n sequential bytes 0..n-1 (mod 256).
static Msu1State make_data_state(const std::string& dir, int n)
{
    std::string bytes;
    for (int i = 0; i < n; i++) { bytes.push_back((char)(i & 0xFF)); }
    std::string path = put_file(dir, "game.msu", bytes.data(), bytes.size());
    Msu1State state = {};
    state.enabled   = true;
    state.data_file = fopen(path.c_str(), "rb");
    state.data_size = (uint32_t)n;
    state.data_pos  = 0;
    REQUIRE(state.data_file != nullptr);
    return state;
}

TEST_CASE("bulk read equals successive port-1 reads")
{
    std::string dir = make_tmpdir();
    Msu1State a = make_data_state(dir, 300);
    Msu1State b = make_data_state(dir, 300);

    uint8_t bulk[300];
    CHECK(msu1_read_data_bulk(a, bulk, 300) == 300);
    CHECK(a.data_pos == 300);
    for (int i = 0; i < 300; i++) {
        CHECK(bulk[i] == msu1_read_port(b, 1));
    }
    fclose(a.data_file); fclose(b.data_file);
}

TEST_CASE("bulk read is short at EOF and zero afterwards")
{
    std::string dir = make_tmpdir();
    Msu1State s = make_data_state(dir, 10);
    uint8_t buf[32];
    memset(buf, 0xAA, sizeof(buf));
    CHECK(msu1_read_data_bulk(s, buf, 32) == 10);
    CHECK(s.data_pos == 10);
    CHECK(msu1_read_data_bulk(s, buf, 32) == 0);
    fclose(s.data_file);
}

TEST_CASE("bulk read after a port-3 seek starts at the new position")
{
    std::string dir = make_tmpdir();
    Msu1State s = make_data_state(dir, 300);
    msu1_write_port(s, 0, 0x00);  // seek latch = 256
    msu1_write_port(s, 1, 0x01);
    msu1_write_port(s, 2, 0x00);
    msu1_write_port(s, 3, 0x00);
    uint8_t buf[8];
    CHECK(msu1_read_data_bulk(s, buf, 8) == 8);
    CHECK(buf[0] == (uint8_t)(256 & 0xFF));
    CHECK(s.data_pos == 264);
    fclose(s.data_file);
}

TEST_CASE("bulk read: disabled or missing file reads nothing")
{
    Msu1State s = {};
    uint8_t buf[4];
    CHECK(msu1_read_data_bulk(s, buf, 4) == 0);
    s.enabled = true;
    CHECK(msu1_read_data_bulk(s, buf, 4) == 0);
}

TEST_CASE("port-1 micro-cache: singles match plain reads across seeks and bulk mixes")
{
    std::string dir = make_tmpdir();
    Msu1State s = make_data_state(dir, 300);

    // sequential single-byte reads (cache path) return the exact stream
    uint8_t b = 0;
    for (int i = 0; i < 100; i++) {
        REQUIRE(msu1_read_data_bulk(s, &b, 1) == 1);
        REQUIRE(b == (uint8_t)(i & 0xFF));
    }
    CHECK(s.data_pos == 100);

    // a bulk read in the middle bypasses and invalidates the cache
    uint8_t buf[32];
    REQUIRE(msu1_read_data_bulk(s, buf, 32) == 32);
    for (int i = 0; i < 32; i++) { CHECK(buf[i] == (uint8_t)((100 + i) & 0xFF)); }

    // singles continue correctly after the bulk read
    REQUIRE(msu1_read_data_bulk(s, &b, 1) == 1);
    CHECK(b == (uint8_t)(132 & 0xFF));

    // a port-3 seek invalidates the cache; singles read the new position
    msu1_write_port(s, 0, 0x05);
    msu1_write_port(s, 1, 0x00);
    msu1_write_port(s, 2, 0x00);
    msu1_write_port(s, 3, 0x00);          // data_pos = 5
    REQUIRE(msu1_read_data_bulk(s, &b, 1) == 1);
    CHECK(b == (uint8_t)(5 & 0xFF));

    // EOF: singles stop cleanly at the end
    msu1_write_port(s, 0, 0x2A);          // seek to 298 = 0x12A
    msu1_write_port(s, 1, 0x01);
    msu1_write_port(s, 2, 0x00);
    msu1_write_port(s, 3, 0x00);
    REQUIRE(msu1_read_data_bulk(s, &b, 1) == 1);
    CHECK(b == (uint8_t)(298 & 0xFF));
    REQUIRE(msu1_read_data_bulk(s, &b, 1) == 1);
    CHECK(b == (uint8_t)(299 & 0xFF));
    CHECK(msu1_read_data_bulk(s, &b, 1) == 0);
    fclose(s.data_file);
}
