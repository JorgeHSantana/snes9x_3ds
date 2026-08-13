#include "doctest.h"

#include "../source/Snes9x/zipselect.h"

TEST_CASE("zip entry selection accepts ROM extensions, case-insensitive") {
    CHECK(zipEntryIsRom("Chrono Trigger.sfc"));
    CHECK(zipEntryIsRom("game.SMC"));
    CHECK(zipEntryIsRom("game.Fig"));
    CHECK(zipEntryIsRom("satella.bs"));
    CHECK(zipEntryIsRom("satella.BSX"));
}

TEST_CASE("zip entry selection accepts ROMs inside archive folders") {
    CHECK(zipEntryIsRom("roms/Chrono Trigger.sfc"));
    CHECK(zipEntryIsRom("a/b/c/game.smc"));
}

TEST_CASE("zip entry selection rejects non-ROM entries") {
    CHECK(!zipEntryIsRom("readme.txt"));
    CHECK(!zipEntryIsRom("cover.png"));
    CHECK(!zipEntryIsRom("nested.zip"));
    CHECK(!zipEntryIsRom("track-1.pcm"));
    CHECK(!zipEntryIsRom("game.msu"));
    CHECK(!zipEntryIsRom("noextension"));
    CHECK(!zipEntryIsRom("trailingdot."));
    CHECK(!zipEntryIsRom(""));
    CHECK(!zipEntryIsRom(NULL));
    CHECK(!zipEntryIsRom("folder/"));           // directory entry
}

TEST_CASE("zip entry selection rejects macOS AppleDouble junk") {
    // these mirror the real ROM's name and would otherwise win 'first match'
    CHECK(!zipEntryIsRom("__MACOSX/._Chrono Trigger.sfc"));
    CHECK(!zipEntryIsRom("__MACOSX/roms/._game.smc"));
    CHECK(!zipEntryIsRom("._game.sfc"));
    CHECK(!zipEntryIsRom("roms/._game.sfc"));
}
