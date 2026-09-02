#ifndef _3DSSTEREOKEY_H_
#define _3DSSTEREOKEY_H_

#include <stddef.h>
#include <string.h>

// Key for the per-game stereo3d/<key>.3d file: the ROM's internal header
// title, made filesystem-safe. The title survives renames, zips and MSU-1
// pack folders, so a .3d shared between two people finds the same game;
// regions and revisions of one game share it too (same layout). Returns
// the key length, 0 when the title has no letter or digit to key on - the
// caller then falls back to the ROM filename.
static inline size_t stereo3dKeyFromRomName(const char *romName, char *out, size_t outSize)
{
    if (out == nullptr || outSize == 0)
        return 0;
    out[0] = '\0';
    if (romName == nullptr)
        return 0;

    // trim
    const char *s = romName;
    while (*s == ' ') s++;
    size_t len = strlen(s);
    while (len > 0 && s[len - 1] == ' ') len--;
    if (len > outSize - 1) len = outSize - 1;

    bool usable = false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        bool alnum = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (alnum) usable = true;
        if (c < 0x20 || c >= 0x7F || strchr("\\/:*?\"<>|", (char)c) != nullptr)
            c = '_';
        out[i] = (char)c;
    }
    out[len] = '\0';
    if (!usable) { out[0] = '\0'; return 0; }
    return len;
}

#endif
