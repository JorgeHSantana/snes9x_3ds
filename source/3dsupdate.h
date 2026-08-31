//=============================================================================
// Self-updater core (issue #64) - pure decision logic, host-testable.
//
// The platform shell (3dsupdatenet.cpp) fetches GitHub's release JSON and
// streams the chosen asset; everything here is string parsing and policy so
// the tests can pin the behavior without a network or a console.
//
// Channels map to the CI releases (see .github/workflows/release.yml):
//   Stable  -> /releases/latest        tag "stable-YYYYMMDD-<sha7>"
//   Nightly -> /releases/tags/nightly-latest  title "Nightly ... (<sha7>)"
// Both carry the 7-hex short sha of the commit they were built from; the
// running build embeds its own (BUILD_GIT_SHA) and "update available" is
// simply "the shas differ" - which also makes switching channels work.
//=============================================================================
#ifndef _3DSUPDATE_H_
#define _3DSUPDATE_H_

#include <stddef.h>

#define UPDATE3DS_SHA_LEN       7
#define UPDATE3DS_TAG_MAX       48
#define UPDATE3DS_TITLE_MAX     96
#define UPDATE3DS_URL_MAX       256

enum Update3dsChannel
{
    UPDATE3DS_CHANNEL_STABLE  = 0,
    UPDATE3DS_CHANNEL_NIGHTLY = 1,
};

struct Update3dsRelease
{
    bool    valid;
    char    tag[UPDATE3DS_TAG_MAX];
    char    title[UPDATE3DS_TITLE_MAX];
    char    sha[UPDATE3DS_SHA_LEN + 1];     // 7-hex short sha, "" if not found
    char    url3dsx[UPDATE3DS_URL_MAX];     // "" when the asset is missing
    char    urlCia[UPDATE3DS_URL_MAX];
};

// GitHub API request path for a channel (host is api.github.com).
void update3dsApiPath(int channel, char* out, size_t outSize);

// Parse one GitHub release object (the API responses above) into 'out'.
// Tolerant of field order and unknown fields; only double-quoted string
// values are consumed. Returns out.valid.
bool update3dsParseRelease(const char* json, size_t len, Update3dsRelease& out);

// An update applies when both shas are well-formed and differ. A release
// with no sha never triggers an update (fail safe, never fail loud).
bool update3dsIsNewer(const char* runningSha, const Update3dsRelease& release);

// Asset for the running format; NULL when the release lacks it.
const char* update3dsAssetUrl(const Update3dsRelease& release, bool isCia);

// True for exactly 7 lowercase-hex chars.
bool update3dsShaValid(const char* sha);

// Release date as "MM-DD-YYYY" (from the stable tag's YYYYMMDD or the
// nightly title's YYYY-MM-DD); "" when neither carries one.
void update3dsReleaseDate(const Update3dsRelease& release,
                          char* out, size_t outSize);

// App version a stable title carries ("Stable v2.1 (...)" -> "2.1");
// "" when the title has none (nightlies, older stable releases).
void update3dsReleaseVersion(const Update3dsRelease& release,
                             char* out, size_t outSize);

// Sanity-check a downloaded image before applying it: header magic
// (3DSX: "3DSX"; CIA: header-size field 0x2020) and a floor on the file
// size so a truncated download or an HTML error page never gets applied.
#define UPDATE3DS_MIN_IMAGE_BYTES (1024u * 1024u)
bool update3dsVerifyImage(const unsigned char* head, size_t headLen,
                          size_t fileSize, bool isCia);

#endif
