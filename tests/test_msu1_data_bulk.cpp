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
