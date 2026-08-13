#ifndef _3DSDIRENTRY_H
#define _3DSDIRENTRY_H

// Pure directory-entry types and helpers shared by the file browser
// (3dsfiles.cpp) and the host test suite. No 3DS dependencies here.

#include <cstdio>
#include <cstring>
#include <limits.h>
#include <strings.h>
#include <vector>

// VirtualFile: a first-level subdirectory holding exactly one ROM plus
// MSU-1 pack files, listed in the parent as a directly bootable entry.
// Filename then holds "<subdir>/<rom>" (relative to the current dir).
enum class FileEntryType { ParentDirectory, ChildDirectory, File, VirtualFile };

struct DirectoryEntry {
    char Filename[NAME_MAX + 1];
    FileEntryType Type;

    DirectoryEntry() {
        Filename[0] = '\0';
        Type = FileEntryType::File;
    }

    DirectoryEntry(const char* name, FileEntryType type) {
        snprintf(Filename, sizeof(Filename), "%.*s",
                 static_cast<int>(sizeof(Filename) - 1),
                 name ? name : "");

        Type = type;
    }

    operator const char*() const { return Filename; }
};

// Sort rank: parent first, then child directories, then files and MSU packs
// interleaved alphabetically. RandomGame relies on all bootable entries
// (File + VirtualFile) forming one contiguous block at the list's tail.
inline int dirEntrySortRank(FileEntryType t) {
    return t == FileEntryType::VirtualFile
        ? static_cast<int>(FileEntryType::File)
        : static_cast<int>(t);
}

inline bool dirEntrySortLess(const DirectoryEntry& a, const DirectoryEntry& b) {
    if (dirEntrySortRank(a.Type) != dirEntrySortRank(b.Type))
        return dirEntrySortRank(a.Type) < dirEntrySortRank(b.Type);
    return strcasecmp(a.Filename, b.Filename) < 0;
}

// Field-wise comparison: the structs are fwritten raw to the dir cache, so
// bytes past each name's terminator are garbage — never memcmp these.
inline bool dirEntryListsEqual(const std::vector<DirectoryEntry>& a,
                               const std::vector<DirectoryEntry>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].Type != b[i].Type || strcmp(a[i].Filename, b[i].Filename) != 0)
            return false;
    }
    return true;
}

// display name for a VirtualFile entry: the pack folder name
inline void dirEntryVirtualDisplayName(const DirectoryEntry& entry,
                                       char* output, size_t bufferSize) {
    snprintf(output, bufferSize, "%s", entry.Filename);
    char* slash = strchr(output, '/');
    if (slash) *slash = '\0';
}

// True when a directory listing (romCount ROMs, hasMsuFiles pack data)
// qualifies for flattening into a single bootable entry.
inline bool dirEntryQualifiesAsMsuPack(int romCount, bool hasMsuFiles) {
    return romCount == 1 && hasMsuFiles;
}

#endif
