#include "3dsupdate.h"

#include <string.h>
#include <stdio.h>

//-----------------------------------------------------------------------------
// Minimal JSON field scanning. The GitHub payloads are machine-generated
// (no exotic escapes in the fields we read), so a targeted scanner keeps
// this dependency-free; anything it cannot find simply stays empty.
//-----------------------------------------------------------------------------

// Copies the string value following `"key":` at or after 'from' (bounded by
// 'end') into out. Returns the position after the value, or NULL.
static const char* jsonFindString(const char* from, const char* end,
                                  const char* key, char* out, size_t outSize)
{
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    size_t patLen = strlen(pattern);

    for (const char* p = from; p + patLen < end; p++)
    {
        if (memcmp(p, pattern, patLen) != 0)
            continue;
        p += patLen;
        while (p < end && (*p == ' ' || *p == ':' || *p == '\t'))
            p++;
        if (p >= end || *p != '"')
            return NULL;
        p++;
        size_t n = 0;
        while (p < end && *p != '"')
        {
            char c = *p++;
            if (c == '\\' && p < end)      // keep \/ and \" readable
                c = *p++;
            if (n + 1 < outSize)
                out[n++] = c;
        }
        out[n < outSize ? n : outSize - 1] = 0;
        return (p < end) ? p + 1 : end;
    }
    return NULL;
}

bool update3dsShaValid(const char* sha)
{
    if (sha == NULL || strlen(sha) != UPDATE3DS_SHA_LEN)
        return false;
    for (int i = 0; i < UPDATE3DS_SHA_LEN; i++)
    {
        char c = sha[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex)
            return false;
    }
    return true;
}

// stable tag "stable-YYYYMMDD-<sha7>" -> text after the last '-'
static void shaFromTag(const char* tag, char* out)
{
    out[0] = 0;
    const char* dash = strrchr(tag, '-');
    if (dash != NULL && strlen(dash + 1) == UPDATE3DS_SHA_LEN)
        memcpy(out, dash + 1, UPDATE3DS_SHA_LEN + 1);
    if (!update3dsShaValid(out))
        out[0] = 0;
}

// nightly title "Nightly YYYY-MM-DD (<sha7>)" -> text in the last parens
static void shaFromTitle(const char* title, char* out)
{
    out[0] = 0;
    const char* open = strrchr(title, '(');
    if (open == NULL)
        return;
    const char* close = strchr(open, ')');
    if (close == NULL || close - open != UPDATE3DS_SHA_LEN + 1)
        return;
    memcpy(out, open + 1, UPDATE3DS_SHA_LEN);
    out[UPDATE3DS_SHA_LEN] = 0;
    if (!update3dsShaValid(out))
        out[0] = 0;
}

void update3dsApiPath(int channel, char* out, size_t outSize)
{
    snprintf(out, outSize, "/repos/JorgeHSantana/snes9x_3ds/releases/%s",
             channel == UPDATE3DS_CHANNEL_NIGHTLY ? "tags/nightly-latest"
                                                  : "latest");
}

bool update3dsParseRelease(const char* json, size_t len, Update3dsRelease& out)
{
    memset(&out, 0, sizeof(out));
    if (json == NULL || len == 0)
        return false;
    const char* end = json + len;

    jsonFindString(json, end, "tag_name", out.tag, sizeof(out.tag));
    jsonFindString(json, end, "name", out.title, sizeof(out.title));

    // Walk every download url; each asset object lists "name" before
    // "browser_download_url", so remember the last seen asset name.
    // (Assets live in their own array, and top-level "name" was consumed
    // above only for the title copy - order independence is tested.)
    char assetName[96] = "";
    const char* p = json;
    for (;;)
    {
        char url[UPDATE3DS_URL_MAX];
        const char* afterName = jsonFindString(p, end, "name",
                                               assetName, sizeof(assetName));
        const char* afterUrl = jsonFindString(p, end, "browser_download_url",
                                              url, sizeof(url));
        if (afterUrl == NULL)
            break;
        if (afterName != NULL && afterName < afterUrl)
        {
            // assetName belongs to this asset object; refine it if more
            // "name" keys appear before the url (nested fields).
            const char* q = afterName;
            for (;;)
            {
                char refined[96];
                const char* r = jsonFindString(q, afterUrl, "name",
                                               refined, sizeof(refined));
                if (r == NULL || r > afterUrl)
                    break;
                memcpy(assetName, refined, sizeof(assetName));
                q = r;
            }
        }
        size_t nameLen = strlen(assetName);
        if (nameLen > 5 && strcmp(assetName + nameLen - 5, ".3dsx") == 0)
            memcpy(out.url3dsx, url, sizeof(out.url3dsx));
        else if (nameLen > 4 && strcmp(assetName + nameLen - 4, ".cia") == 0)
            memcpy(out.urlCia, url, sizeof(out.urlCia));
        p = afterUrl;
    }

    shaFromTag(out.tag, out.sha);
    if (out.sha[0] == 0)
        shaFromTitle(out.title, out.sha);

    out.valid = (out.sha[0] != 0) &&
                (out.url3dsx[0] != 0 || out.urlCia[0] != 0);
    return out.valid;
}

bool update3dsIsNewer(const char* runningSha, const Update3dsRelease& release)
{
    if (!release.valid || !update3dsShaValid(release.sha))
        return false;
    if (!update3dsShaValid(runningSha))
        return false;
    return strcmp(runningSha, release.sha) != 0;
}

static bool allDigits(const char* p, int n)
{
    for (int i = 0; i < n; i++)
        if (p[i] < '0' || p[i] > '9')
            return false;
    return true;
}

void update3dsReleaseDate(const Update3dsRelease& release,
                          char* out, size_t outSize)
{
    if (outSize == 0)
        return;
    out[0] = 0;

    // stable tag: "stable-YYYYMMDD-<sha>"
    const char* dash = strchr(release.tag, '-');
    if (dash != NULL && allDigits(dash + 1, 8) && dash[9] == '-')
    {
        snprintf(out, outSize, "%.2s-%.2s-%.4s",
                 dash + 5, dash + 7, dash + 1);
        return;
    }

    // nightly title: "Nightly YYYY-MM-DD (<sha>)"
    for (const char* p = release.title; *p != 0; p++)
    {
        if (allDigits(p, 4) && p[4] == '-' && allDigits(p + 5, 2) &&
            p[7] == '-' && allDigits(p + 8, 2))
        {
            snprintf(out, outSize, "%.2s-%.2s-%.4s", p + 5, p + 8, p);
            return;
        }
    }
}

void update3dsReleaseVersion(const Update3dsRelease& release,
                             char* out, size_t outSize)
{
    if (outSize == 0)
        return;
    out[0] = 0;
    // "v" followed by digits/dots, bounded by space or end
    for (const char* p = release.title; *p != 0; p++)
    {
        if (*p != 'v' || p[1] < '0' || p[1] > '9')
            continue;
        size_t n = 0;
        const char* q = p + 1;
        while (*q != 0 && ((*q >= '0' && *q <= '9') || *q == '.'))
        {
            if (n + 1 < outSize)
                out[n++] = *q;
            q++;
        }
        out[n < outSize ? n : outSize - 1] = 0;
        return;
    }
}

bool update3dsVerifyImage(const unsigned char* head, size_t headLen,
                          size_t fileSize, bool isCia)
{
    if (head == NULL || headLen < 4 || fileSize < UPDATE3DS_MIN_IMAGE_BYTES)
        return false;
    if (isCia)
    {
        // CIA archive header starts with its own size, always 0x2020 LE
        unsigned int headerSize = (unsigned int)head[0] |
                                  ((unsigned int)head[1] << 8) |
                                  ((unsigned int)head[2] << 16) |
                                  ((unsigned int)head[3] << 24);
        return headerSize == 0x2020;
    }
    return memcmp(head, "3DSX", 4) == 0;
}

const char* update3dsAssetUrl(const Update3dsRelease& release, bool isCia)
{
    if (!release.valid)
        return NULL;
    const char* url = isCia ? release.urlCia : release.url3dsx;
    return (url[0] != 0) ? url : NULL;
}
