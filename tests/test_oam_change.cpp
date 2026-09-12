#include "doctest.h"
#include "oam_change.h"

TEST_CASE("OAM appearance writes only rebuild geometry for vertical flip") {
    for (uint16_t before = 0; before < 256; ++before) {
        for (uint16_t after = 0; after < 256; ++after) {
            CHECK(oam_word_changes_geometry(2, 0, before, 255, after)
                == (((before ^ after) & 0x80) != 0));
        }
    }
}

TEST_CASE("OAM position changes invalidate each sprite and unchanged words do not") {
    for (uint16_t sprite = 0; sprite < 128; ++sprite) {
        const uint16_t position = sprite * 4;
        CHECK_FALSE(oam_word_changes_geometry(position, 1, 2, 1, 2));
        CHECK(oam_word_changes_geometry(position, 1, 2, 3, 2));
        CHECK(oam_word_changes_geometry(position, 1, 2, 1, 3));
        CHECK_FALSE(oam_word_changes_geometry(position + 2, 1, 2, 255, 0x72));
        CHECK(oam_word_changes_geometry(position + 2, 1, 2, 1, 0x82));
    }
    CHECK(oam_word_changes_geometry(512, 0, 0, 0, 0));
    CHECK(oam_word_changes_geometry(1, 0, 0, 0, 0));
}
