#include "doctest.h"
#include "3dsmsu.h"
#include "msu1.h"
#include <cstring>
#include <cstdint>

TEST_CASE("msu3dsFormatStatus: MSU-1 not present")
{
    Msu1State state = {};
    char line[64];
    char subtitle[64];

    bool result = msu3dsFormatStatus(false, true, state, 0, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == true);
    CHECK(strcmp(line, "MSU-1: not detected") == 0);
    CHECK(strcmp(subtitle, "") == 0);
}

TEST_CASE("msu3dsFormatStatus: MSU-1 not present, subtitle suppressed even with underruns")
{
    Msu1State state = {};
    char line[64];
    char subtitle[64];

    bool result = msu3dsFormatStatus(false, true, state, 42, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == true);
    CHECK(strcmp(line, "MSU-1: not detected") == 0);
    CHECK(strcmp(subtitle, "") == 0);
}

TEST_CASE("msu3dsFormatStatus: MSU-1 not present, setting disabled → distinct 'disabled' line")
{
    Msu1State state = {};
    char line[64];
    char subtitle[64];

    bool result = msu3dsFormatStatus(false, false, state, 0, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == true);
    CHECK(strcmp(line, "MSU-1: disabled") == 0);
    CHECK(strcmp(subtitle, "") == 0);
}

TEST_CASE("msu3dsFormatStatus: MSU-1 not present, setting disabled, subtitle suppressed even with underruns")
{
    Msu1State state = {};
    char line[64];
    char subtitle[64];

    bool result = msu3dsFormatStatus(false, false, state, 42, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == true);
    CHECK(strcmp(line, "MSU-1: disabled") == 0);
    CHECK(strcmp(subtitle, "") == 0);
}

TEST_CASE("msu3dsFormatStatus: MSU-1 present overrides a stale setting_enabled=false (chip present implies enabled)")
{
    // msu_present rows are unchanged by setting_enabled per the design table —
    // a present chip means enabled regardless of what the caller passes here.
    Msu1State state = {};
    state.status = 0;
    char line[64];
    char subtitle[64];

    bool result = msu3dsFormatStatus(true, false, state, 0, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == true);
    CHECK(strcmp(line, "MSU-1: detected") == 0);
}

TEST_CASE("msu3dsFormatStatus: MSU-1 present, playing track 3")
{
    Msu1State state = {};
    state.status = MSU1_FLAG_AUDIO_PLAYING;
    state.current_track = 3;
    char line[64];
    char subtitle[64];

    bool result = msu3dsFormatStatus(true, true, state, 0, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == true);
    CHECK(strcmp(line, "MSU-1: playing track 3") == 0);
    CHECK(strcmp(subtitle, "") == 0);
}

TEST_CASE("msu3dsFormatStatus: MSU-1 present, playing track 42")
{
    Msu1State state = {};
    state.status = MSU1_FLAG_AUDIO_PLAYING;
    state.current_track = 42;
    char line[64];
    char subtitle[64];

    bool result = msu3dsFormatStatus(true, true, state, 0, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == true);
    CHECK(strstr(line, "42") != nullptr);
    CHECK(strcmp(line, "MSU-1: playing track 42") == 0);
}

TEST_CASE("msu3dsFormatStatus: MSU-1 present, idle (not playing)")
{
    Msu1State state = {};
    state.status = 0;  // No AUDIO_PLAYING flag
    state.current_track = 0;
    char line[64];
    char subtitle[64];

    bool result = msu3dsFormatStatus(true, true, state, 0, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == true);
    CHECK(strcmp(line, "MSU-1: detected") == 0);
    CHECK(strcmp(subtitle, "") == 0);
}

TEST_CASE("msu3dsFormatStatus: underruns == 0 → no warning")
{
    Msu1State state = {};
    state.status = MSU1_FLAG_AUDIO_PLAYING;
    state.current_track = 1;
    char line[64];
    char subtitle[64];

    bool result = msu3dsFormatStatus(true, true, state, 0, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == true);
    CHECK(strcmp(subtitle, "") == 0);
}

TEST_CASE("msu3dsFormatStatus: underruns == 1 (threshold) → minor warning")
{
    Msu1State state = {};
    state.status = MSU1_FLAG_AUDIO_PLAYING;
    state.current_track = 1;
    char line[64];
    char subtitle[64];

    bool result = msu3dsFormatStatus(true, true, state, 1, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == true);
    CHECK(strcmp(subtitle, "Minor audio stutter detected") == 0);
}

TEST_CASE("msu3dsFormatStatus: underruns == 5 (below severe threshold) → minor warning")
{
    Msu1State state = {};
    state.status = MSU1_FLAG_AUDIO_PLAYING;
    state.current_track = 1;
    char line[64];
    char subtitle[64];

    bool result = msu3dsFormatStatus(true, true, state, 5, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == true);
    CHECK(strcmp(subtitle, "Minor audio stutter detected") == 0);
}

TEST_CASE("msu3dsFormatStatus: underruns == 6 (severe threshold) → severe warning")
{
    Msu1State state = {};
    state.status = MSU1_FLAG_AUDIO_PLAYING;
    state.current_track = 1;
    char line[64];
    char subtitle[64];

    bool result = msu3dsFormatStatus(true, true, state, 6, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == true);
    CHECK(strcmp(subtitle, "Audio is stuttering - a faster SD card may help") == 0);
}

TEST_CASE("msu3dsFormatStatus: underruns == 1000 (above severe threshold) → severe warning")
{
    Msu1State state = {};
    state.status = MSU1_FLAG_AUDIO_PLAYING;
    state.current_track = 1;
    char line[64];
    char subtitle[64];

    bool result = msu3dsFormatStatus(true, true, state, 1000, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == true);
    CHECK(strcmp(subtitle, "Audio is stuttering - a faster SD card may help") == 0);
}

TEST_CASE("msu3dsFormatStatus: nullptr line → false")
{
    Msu1State state = {};
    char subtitle[64];

    bool result = msu3dsFormatStatus(true, true, state, 0, nullptr, 64, subtitle, sizeof(subtitle));

    CHECK(result == false);
}

TEST_CASE("msu3dsFormatStatus: nullptr subtitle → false")
{
    Msu1State state = {};
    char line[64];

    bool result = msu3dsFormatStatus(true, true, state, 0, line, sizeof(line), nullptr, 64);

    CHECK(result == false);
}

TEST_CASE("msu3dsFormatStatus: line_size == 0 → false")
{
    Msu1State state = {};
    char line[64];
    char subtitle[64];

    bool result = msu3dsFormatStatus(true, true, state, 0, line, 0, subtitle, sizeof(subtitle));

    CHECK(result == false);
}

TEST_CASE("msu3dsFormatStatus: subtitle_size == 0 → false")
{
    Msu1State state = {};
    char line[64];
    char subtitle[64];

    bool result = msu3dsFormatStatus(true, true, state, 0, line, sizeof(line), subtitle, 0);

    CHECK(result == false);
}

TEST_CASE("msu3dsFormatStatus: tiny line buffer (8 bytes, not enough for 'MSU-1: detected') → false")
{
    Msu1State state = {};
    state.status = 0;
    char line[8];
    memset(line, 0x7F, sizeof(line));
    char subtitle[64];

    bool result = msu3dsFormatStatus(true, true, state, 0, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == false);
    // Verify no silent truncation: the caller must not be able to mistake
    // the truncated content for a valid status line — the false return is
    // what stops that, and snprintf's size>0 NUL-termination guarantee
    // (even on truncation) means the buffer holds a well-formed C string,
    // not sentinel-filled raw memory past that point.
    CHECK(memchr(line, '\0', sizeof(line)) != nullptr);
}

TEST_CASE("msu3dsFormatStatus: tiny subtitle buffer (8 bytes, not enough for minor warning) → false")
{
    Msu1State state = {};
    state.status = MSU1_FLAG_AUDIO_PLAYING;
    state.current_track = 1;
    char line[64];
    char subtitle[8];
    memset(subtitle, 0x7F, sizeof(subtitle));

    bool result = msu3dsFormatStatus(true, true, state, 1, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == false);
    // Verify no silent truncation: same reasoning as the tiny-line-buffer
    // case above — false return prevents callers from displaying the
    // truncated warning, and the buffer is still a well-formed C string.
    CHECK(memchr(subtitle, '\0', sizeof(subtitle)) != nullptr);
}

TEST_CASE("msu3dsFormatStatus: NUL-terminated on success (line)")
{
    Msu1State state = {};
    state.status = MSU1_FLAG_AUDIO_PLAYING;
    state.current_track = 5;
    char line[64];
    memset(line, 0xFF, sizeof(line));
    char subtitle[64];

    bool result = msu3dsFormatStatus(true, true, state, 0, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == true);
    // Verify NUL termination
    size_t len = strlen(line);
    CHECK(line[len] == '\0');
    CHECK((unsigned char)line[len+1] == 0xFF);  // Verify no write past NUL
}

TEST_CASE("msu3dsFormatStatus: NUL-terminated on success (subtitle)")
{
    Msu1State state = {};
    state.status = MSU1_FLAG_AUDIO_PLAYING;
    state.current_track = 1;
    char line[64];
    char subtitle[64];
    memset(subtitle, 0xFF, sizeof(subtitle));

    bool result = msu3dsFormatStatus(true, true, state, 1, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == true);
    // Verify NUL termination
    size_t len = strlen(subtitle);
    CHECK(subtitle[len] == '\0');
    CHECK((unsigned char)subtitle[len+1] == 0xFF);  // Verify no write past NUL
}

TEST_CASE("msu3dsFormatStatus: present + playing + idle transitions (status flag check)")
{
    Msu1State state = {};
    char line[64];
    char subtitle[64];

    // PLAYING
    state.status = MSU1_FLAG_AUDIO_PLAYING;
    state.current_track = 10;
    bool result = msu3dsFormatStatus(true, true, state, 0, line, sizeof(line), subtitle, sizeof(subtitle));
    CHECK(result == true);
    CHECK(strstr(line, "playing") != nullptr);

    // IDLE (clear flag)
    state.status = 0;
    result = msu3dsFormatStatus(true, true, state, 0, line, sizeof(line), subtitle, sizeof(subtitle));
    CHECK(result == true);
    CHECK(strcmp(line, "MSU-1: detected") == 0);

    // PLAYING again
    state.status = MSU1_FLAG_AUDIO_PLAYING;
    state.current_track = 20;
    result = msu3dsFormatStatus(true, true, state, 0, line, sizeof(line), subtitle, sizeof(subtitle));
    CHECK(result == true);
    CHECK(strstr(line, "20") != nullptr);
}

TEST_CASE("msu3dsFormatStatus: large track number (65535)")
{
    Msu1State state = {};
    state.status = MSU1_FLAG_AUDIO_PLAYING;
    state.current_track = 65535;
    char line[64];
    char subtitle[64];

    bool result = msu3dsFormatStatus(true, true, state, 0, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == true);
    CHECK(strcmp(line, "MSU-1: playing track 65535") == 0);
}

TEST_CASE("msu3dsFormatStatus: exact buffer sizes (no extra room)")
{
    Msu1State state = {};
    state.status = 0;

    // Exact size for "MSU-1: detected" + NUL = 16 bytes
    char line[16];
    char subtitle[1];  // Just for empty string + NUL

    bool result = msu3dsFormatStatus(true, true, state, 0, line, sizeof(line), subtitle, sizeof(subtitle));

    CHECK(result == true);
    CHECK(strcmp(line, "MSU-1: detected") == 0);
    CHECK(strcmp(subtitle, "") == 0);
}

TEST_CASE("msu3dsFormatStatus: thresholds are available as constexpr")
{
    // This test verifies the constants exist and have correct values
    CHECK(MSU1_STUTTER_MINOR_THRESHOLD == 1);
    CHECK(MSU1_STUTTER_SEVERE_THRESHOLD == 6);
}
