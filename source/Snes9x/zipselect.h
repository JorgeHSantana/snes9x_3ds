#ifndef _ZIPSELECT_H_
#define _ZIPSELECT_H_

// Pure zip-entry selection rule shared by the loader (memmap.cpp) and the
// host test suite: the first archive entry this accepts is the ROM loaded
// from a .zip. Multi-ROM archives are out of scope (first match wins).

#include <cstring>
#include <strings.h>

inline bool zipEntryIsRom(const char* name)
{
    if (!name || name[0] == '\0')
        return false;

    // macOS zips carry AppleDouble junk that mirrors the real entries
    // (__MACOSX/._Game.sfc) and would win the "first match" pick
    if (strstr(name, "__MACOSX/") != NULL)
        return false;

    const char* base = strrchr(name, '/');
    base = base ? base + 1 : name;
    if (base[0] == '\0' || strncmp(base, "._", 2) == 0)
        return false;

    const char* dot = strrchr(base, '.');
    if (!dot || dot[1] == '\0')
        return false;

    static const char* romExtensions[] = { ".smc", ".sfc", ".fig", ".bs", ".bsx" };
    for (size_t i = 0; i < sizeof(romExtensions) / sizeof(romExtensions[0]); i++) {
        if (strcasecmp(dot, romExtensions[i]) == 0)
            return true;
    }

    return false;
}

#endif
