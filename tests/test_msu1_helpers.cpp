#include "doctest.h"
#include "msu1.h"
#include "fixtures.h"
#include <cstring>

TEST_CASE("msu1_build_base_path strips the last extension only")
{
    char out[MSU1_MAX_BASE_PATH];
    REQUIRE(msu1_build_base_path("/roms/Zelda ALttP.sfc", out, sizeof(out)));
    CHECK(strcmp(out, "/roms/Zelda ALttP") == 0);
    REQUIRE(msu1_build_base_path("/r/a.b/Game.v1.smc", out, sizeof(out)));
    CHECK(strcmp(out, "/r/a.b/Game.v1") == 0);       // only last ext stripped
    REQUIRE(msu1_build_base_path("/roms/noext", out, sizeof(out)));
    CHECK(strcmp(out, "/roms/noext") == 0);           // no dot in basename
}

TEST_CASE("msu1_build_base_path validates parameters")
{
    char out[8];
    CHECK_FALSE(msu1_build_base_path(nullptr, out, sizeof(out)));
    CHECK_FALSE(msu1_build_base_path("/roms/Game.sfc", nullptr, 99));
    CHECK_FALSE(msu1_build_base_path("/roms/Game.sfc", out, sizeof(out))); // too small
    CHECK_FALSE(msu1_build_base_path("", out, sizeof(out)));
}

TEST_CASE("msu1_build_track_path formats <base>-<N>.pcm")
{
    char out[MSU1_MAX_BASE_PATH];
    REQUIRE(msu1_build_track_path("/roms/Game", 7, out, sizeof(out)));
    CHECK(strcmp(out, "/roms/Game-7.pcm") == 0);
    char tiny[8];
    CHECK_FALSE(msu1_build_track_path("/roms/Game", 7, tiny, sizeof(tiny)));
    CHECK_FALSE(msu1_build_track_path(nullptr, 7, out, sizeof(out)));
}

TEST_CASE("msu1_parse_pcm_header accepts valid header, rejects garbage")
{
    Msu1PcmHeader hdr{};
    FILE* good = fixtures::make_pcm_file(/*loop_point=*/1234, /*sample_count=*/16);
    REQUIRE(good != nullptr);
    CHECK(msu1_parse_pcm_header(good, hdr));
    CHECK(hdr.loop_point == 1234);
    fclose(good);

    FILE* bad_magic = fixtures::make_raw_file("XYZ1\x00\x00\x00\x00", 8);
    CHECK_FALSE(msu1_parse_pcm_header(bad_magic, hdr));
    fclose(bad_magic);

    FILE* truncated = fixtures::make_raw_file("MSU1\x01", 5);
    CHECK_FALSE(msu1_parse_pcm_header(truncated, hdr));
    fclose(truncated);

    CHECK_FALSE(msu1_parse_pcm_header(nullptr, hdr));
}

TEST_CASE("msu1_is_port_address covers exactly $2000-$2007")
{
    CHECK(msu1_is_port_address(0x2000));
    CHECK(msu1_is_port_address(0x2007));
    CHECK_FALSE(msu1_is_port_address(0x1FFF));
    CHECK_FALSE(msu1_is_port_address(0x2008));
    CHECK_FALSE(msu1_is_port_address(0x2100));
}

TEST_CASE("msu1_is_dma_source requires fixed A-address in $2000-$2007, system banks")
{
    CHECK(msu1_is_dma_source(0x00, 0x2001, true));
    CHECK(msu1_is_dma_source(0xBF, 0x2001, true));
    CHECK_FALSE(msu1_is_dma_source(0x00, 0x2001, false));   // incrementing source
    CHECK_FALSE(msu1_is_dma_source(0x40, 0x2001, true));    // bank $40-$7F: not register space
    CHECK_FALSE(msu1_is_dma_source(0x00, 0x2008, true));
}

static char g_last_log[512];
static void capture_log(const char* m) { snprintf(g_last_log, sizeof(g_last_log), "%s", m); }

TEST_CASE("log hook reports track-load failures; null hook is safe")
{
    Msu1State s = {};
    s.enabled = true;
    snprintf(s.base_path, sizeof(s.base_path), "/nonexistent/dir/game");
    msu1_set_log_hook(nullptr);
    msu1_write_port(s, 4, 1);
    msu1_write_port(s, 5, 0);          // load track 1: no crash with null hook
    CHECK((s.status & MSU1_FLAG_AUDIO_ERROR) != 0);

    g_last_log[0] = 0;
    msu1_set_log_hook(capture_log);
    msu1_write_port(s, 4, 2);
    msu1_write_port(s, 5, 0);          // load track 2: hook captures the failure
    CHECK(strstr(g_last_log, "fopen failed") != nullptr);
    msu1_set_log_hook(nullptr);
}
