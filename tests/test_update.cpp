#include "doctest.h"
#include "3dsupdate.h"
#include <cstring>

// Fixtures mirror the real GitHub API shape: top-level "name" (release
// title) appears before the assets array, and each asset object carries
// its own "name" plus a nested uploader object between it and the
// download url - the exact traps the parser has to survive.

static const char* STABLE_JSON =
    "{"
    "\"url\":\"https://api.github.com/repos/JorgeHSantana/snes9x_3ds/releases/1\","
    "\"tag_name\":\"stable-20260830-d8df8d8\","
    "\"name\":\"Stable 2026-08-30\","
    "\"prerelease\":false,"
    "\"assets\":["
    "{\"name\":\"snes9x_3ds.cia\","
    "\"uploader\":{\"login\":\"JorgeHSantana\",\"type\":\"User\"},"
    "\"browser_download_url\":\"https://github.com/JorgeHSantana/snes9x_3ds/releases/download/stable-20260830-d8df8d8/snes9x_3ds.cia\"},"
    "{\"name\":\"snes9x_3ds.3dsx\","
    "\"uploader\":{\"login\":\"JorgeHSantana\",\"type\":\"User\"},"
    "\"browser_download_url\":\"https://github.com/JorgeHSantana/snes9x_3ds/releases/download/stable-20260830-d8df8d8/snes9x_3ds.3dsx\"}"
    "],"
    "\"body\":\"notes with (parens) and stable-99999999-abcdefg decoys\"}";

static const char* NIGHTLY_JSON =
    "{"
    "\"tag_name\":\"nightly-latest\","
    "\"name\":\"Nightly 2026-08-31 (0f3c21a)\","
    "\"prerelease\":true,"
    "\"assets\":["
    "{\"name\":\"snes9x_3ds.3dsx\","
    "\"browser_download_url\":\"https://github.com/JorgeHSantana/snes9x_3ds/releases/download/nightly-latest/snes9x_3ds.3dsx\"},"
    "{\"name\":\"snes9x_3ds.cia\","
    "\"browser_download_url\":\"https://github.com/JorgeHSantana/snes9x_3ds/releases/download/nightly-latest/snes9x_3ds.cia\"}"
    "]}";

TEST_CASE("stable release parses tag sha and both asset urls")
{
    Update3dsRelease r;
    REQUIRE(update3dsParseRelease(STABLE_JSON, strlen(STABLE_JSON), r));
    CHECK(strcmp(r.tag, "stable-20260830-d8df8d8") == 0);
    CHECK(strcmp(r.sha, "d8df8d8") == 0);
    CHECK(strstr(r.urlCia, "snes9x_3ds.cia") != nullptr);
    CHECK(strstr(r.url3dsx, "snes9x_3ds.3dsx") != nullptr);
    CHECK(strstr(r.url3dsx, "stable-20260830") != nullptr);
}

TEST_CASE("nightly release takes the sha from the title parens")
{
    Update3dsRelease r;
    REQUIRE(update3dsParseRelease(NIGHTLY_JSON, strlen(NIGHTLY_JSON), r));
    CHECK(strcmp(r.sha, "0f3c21a") == 0);   // tag has no sha; title wins
    CHECK(strstr(r.url3dsx, "nightly-latest") != nullptr);
    CHECK(strstr(r.urlCia, ".cia") != nullptr);
}

TEST_CASE("asset selection follows the running format")
{
    Update3dsRelease r;
    REQUIRE(update3dsParseRelease(STABLE_JSON, strlen(STABLE_JSON), r));
    const char* cia = update3dsAssetUrl(r, true);
    const char* hb  = update3dsAssetUrl(r, false);
    REQUIRE(cia != nullptr);
    REQUIRE(hb != nullptr);
    CHECK(strstr(cia, ".cia") != nullptr);
    CHECK(strstr(hb, ".3dsx") != nullptr);
}

TEST_CASE("update decision is sha inequality both ways")
{
    Update3dsRelease r;
    REQUIRE(update3dsParseRelease(STABLE_JSON, strlen(STABLE_JSON), r));
    CHECK_FALSE(update3dsIsNewer("d8df8d8", r));   // same build
    CHECK(update3dsIsNewer("6fcde24", r));         // older build
    CHECK(update3dsIsNewer("0f3c21a", r));         // channel switch counts too
}

TEST_CASE("malformed inputs never report an update")
{
    Update3dsRelease r;
    CHECK_FALSE(update3dsParseRelease("not json at all", 15, r));
    CHECK_FALSE(r.valid);
    CHECK_FALSE(update3dsIsNewer("d8df8d8", r));

    CHECK_FALSE(update3dsParseRelease(nullptr, 0, r));

    // release with assets but no recoverable sha
    const char* noSha =
        "{\"tag_name\":\"v1.0\",\"name\":\"First\",\"assets\":["
        "{\"name\":\"snes9x_3ds.3dsx\",\"browser_download_url\":\"https://x/y.3dsx\"}]}";
    CHECK_FALSE(update3dsParseRelease(noSha, strlen(noSha), r));

    // sha present but no usable asset
    const char* noAsset =
        "{\"tag_name\":\"stable-20260830-d8df8d8\",\"name\":\"S\",\"assets\":["
        "{\"name\":\"README.md\",\"browser_download_url\":\"https://x/README.md\"}]}";
    CHECK_FALSE(update3dsParseRelease(noAsset, strlen(noAsset), r));
}

TEST_CASE("bad running sha fails safe and sha validation is strict")
{
    Update3dsRelease r;
    REQUIRE(update3dsParseRelease(STABLE_JSON, strlen(STABLE_JSON), r));
    CHECK_FALSE(update3dsIsNewer("unknown", r));
    CHECK_FALSE(update3dsIsNewer("", r));
    CHECK_FALSE(update3dsIsNewer(nullptr, r));

    CHECK(update3dsShaValid("0123abc"));
    CHECK_FALSE(update3dsShaValid("0123ABC"));    // uppercase is not ours
    CHECK_FALSE(update3dsShaValid("0123ab"));     // short
    CHECK_FALSE(update3dsShaValid("0123abcd"));   // long
    CHECK_FALSE(update3dsShaValid("0123abg"));    // non-hex
}

TEST_CASE("release date renders american from either channel")
{
    Update3dsRelease r;
    char date[16];

    REQUIRE(update3dsParseRelease(STABLE_JSON, strlen(STABLE_JSON), r));
    update3dsReleaseDate(r, date, sizeof(date));
    CHECK(strcmp(date, "08-30-2026") == 0);    // tag stable-20260830-...

    REQUIRE(update3dsParseRelease(NIGHTLY_JSON, strlen(NIGHTLY_JSON), r));
    update3dsReleaseDate(r, date, sizeof(date));
    CHECK(strcmp(date, "08-31-2026") == 0);    // title "Nightly 2026-08-31"

    memset(&r, 0, sizeof(r));
    update3dsReleaseDate(r, date, sizeof(date));
    CHECK(date[0] == 0);                       // no date anywhere -> empty
}

TEST_CASE("image verification rejects everything but a plausible build")
{
    const size_t okSize = 2500000;   // our builds are ~2.5MB
    const unsigned char hb[8]  = {'3','D','S','X', 0, 0, 0, 0};
    const unsigned char cia[8] = {0x20, 0x20, 0x00, 0x00, 0, 0, 0, 0};
    const unsigned char html[8] = {'<','h','t','m','l','>', 0, 0};

    CHECK(update3dsVerifyImage(hb, sizeof(hb), okSize, false));
    CHECK(update3dsVerifyImage(cia, sizeof(cia), okSize, true));

    CHECK_FALSE(update3dsVerifyImage(hb, sizeof(hb), okSize, true));     // wrong format
    CHECK_FALSE(update3dsVerifyImage(cia, sizeof(cia), okSize, false));
    CHECK_FALSE(update3dsVerifyImage(html, sizeof(html), okSize, false)); // error page
    CHECK_FALSE(update3dsVerifyImage(hb, sizeof(hb), 40000, false));      // truncated
    CHECK_FALSE(update3dsVerifyImage(hb, 3, okSize, false));              // short head
    CHECK_FALSE(update3dsVerifyImage(nullptr, 8, okSize, false));
}

TEST_CASE("api paths name the two channels")
{
    char path[128];
    update3dsApiPath(UPDATE3DS_CHANNEL_STABLE, path, sizeof(path));
    CHECK(strcmp(path, "/repos/JorgeHSantana/snes9x_3ds/releases/latest") == 0);
    update3dsApiPath(UPDATE3DS_CHANNEL_NIGHTLY, path, sizeof(path));
    CHECK(strcmp(path, "/repos/JorgeHSantana/snes9x_3ds/releases/tags/nightly-latest") == 0);
}
