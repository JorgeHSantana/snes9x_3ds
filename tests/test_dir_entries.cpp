#include "doctest.h"

#include <algorithm>

#include "../source/3dsdirentry.h"

TEST_CASE("sort: parent first, dirs next, files and packs interleaved") {
    std::vector<DirectoryEntry> v = {
        { "Zelda.sfc",                     FileEntryType::File },
        { "Mega Man X3 MSU1/mmx3.sfc",     FileEntryType::VirtualFile },
        { "  ... Parent Directory",        FileEntryType::ParentDirectory },
        { "Chrono Trigger MSU1/ct.sfc",    FileEntryType::VirtualFile },
        { "Europe",                        FileEntryType::ChildDirectory },
        { "Aladdin.smc",                   FileEntryType::File },
    };
    std::sort(v.begin(), v.end(), dirEntrySortLess);

    CHECK(v[0].Type == FileEntryType::ParentDirectory);
    CHECK(v[1].Type == FileEntryType::ChildDirectory);
    // bootable tail block, alphabetical across File and VirtualFile
    CHECK(strcmp(v[2].Filename, "Aladdin.smc") == 0);
    CHECK(strcmp(v[3].Filename, "Chrono Trigger MSU1/ct.sfc") == 0);
    CHECK(strcmp(v[4].Filename, "Mega Man X3 MSU1/mmx3.sfc") == 0);
    CHECK(strcmp(v[5].Filename, "Zelda.sfc") == 0);
}

TEST_CASE("sort: bootable entries form a contiguous tail block (RandomGame invariant)") {
    std::vector<DirectoryEntry> v = {
        { "b dir",        FileEntryType::ChildDirectory },
        { "a.sfc",        FileEntryType::File },
        { "pack/a.sfc",   FileEntryType::VirtualFile },
        { "z dir",        FileEntryType::ChildDirectory },
        { "m.sfc",        FileEntryType::File },
    };
    std::sort(v.begin(), v.end(), dirEntrySortLess);

    int firstBootable = -1;
    for (size_t i = 0; i < v.size(); i++) {
        bool bootable = v[i].Type == FileEntryType::File || v[i].Type == FileEntryType::VirtualFile;
        if (bootable && firstBootable < 0) firstBootable = (int)i;
        if (firstBootable >= 0) CHECK(bootable);
    }
    CHECK(firstBootable == 2);
}

TEST_CASE("sort is case-insensitive") {
    std::vector<DirectoryEntry> v = {
        { "zelda.sfc",   FileEntryType::File },
        { "Aladdin.smc", FileEntryType::File },
        { "MARIO.sfc",   FileEntryType::File },
    };
    std::sort(v.begin(), v.end(), dirEntrySortLess);
    CHECK(strcmp(v[0].Filename, "Aladdin.smc") == 0);
    CHECK(strcmp(v[1].Filename, "MARIO.sfc") == 0);
    CHECK(strcmp(v[2].Filename, "zelda.sfc") == 0);
}

TEST_CASE("list equality compares fields, not raw bytes") {
    std::vector<DirectoryEntry> a = {
        { "game.sfc", FileEntryType::File },
        { "pack/rom.sfc", FileEntryType::VirtualFile },
    };
    std::vector<DirectoryEntry> b = a;
    CHECK(dirEntryListsEqual(a, b));

    // garbage past the terminator must not affect equality (cache entries
    // are fwritten raw, so those bytes differ between scan and cache)
    b[0].Filename[sizeof(b[0].Filename) - 1] = 'X';
    CHECK(dirEntryListsEqual(a, b));

    SUBCASE("different name") {
        b[0] = DirectoryEntry("other.sfc", FileEntryType::File);
        CHECK(!dirEntryListsEqual(a, b));
    }
    SUBCASE("different type") {
        b[1].Type = FileEntryType::File;
        CHECK(!dirEntryListsEqual(a, b));
    }
    SUBCASE("different size") {
        b.pop_back();
        CHECK(!dirEntryListsEqual(a, b));
    }
}

TEST_CASE("virtual display name is the pack folder") {
    DirectoryEntry e("Mega Man X3 Zero Project (MSU1)/mmx3.sfc", FileEntryType::VirtualFile);
    char out[NAME_MAX + 1];
    dirEntryVirtualDisplayName(e, out, sizeof(out));
    CHECK(strcmp(out, "Mega Man X3 Zero Project (MSU1)") == 0);

    // no slash -> unchanged
    DirectoryEntry f("plain.sfc", FileEntryType::File);
    dirEntryVirtualDisplayName(f, out, sizeof(out));
    CHECK(strcmp(out, "plain.sfc") == 0);
}

TEST_CASE("MSU pack qualification: exactly one ROM plus pack files") {
    CHECK(dirEntryQualifiesAsMsuPack(1, true));
    CHECK(!dirEntryQualifiesAsMsuPack(1, false));   // lone ROM folder: keep navigable
    CHECK(!dirEntryQualifiesAsMsuPack(2, true));    // multi-ROM: ambiguous, keep folder
    CHECK(!dirEntryQualifiesAsMsuPack(0, true));    // tracks without a ROM
}
