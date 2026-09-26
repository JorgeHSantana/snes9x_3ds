#include "doctest.h"
#include "snap_reader.h"

TEST_CASE("snap reader: memory reads are sequential, short at the end, and tell follows") {
    const uint8_t data[10] = {0,1,2,3,4,5,6,7,8,9};
    SnapIn in = SnapIn::fromMemory(data, 10);
    uint8_t buf[8] = {0};
    CHECK(in.read(buf, 4) == 4);
    CHECK(buf[3] == 3);
    CHECK(in.tell() == 4);
    CHECK(in.read(buf, 8) == 6);       // only 6 left
    CHECK(buf[5] == 9);
    CHECK(in.tell() == 10);
    CHECK(in.read(buf, 1) == 0);
}

TEST_CASE("snap reader: seek set/cur/end within bounds, refused past them (the unfreeze rewinds a header it could not parse)") {
    const uint8_t data[20] = {0};
    SnapIn in = SnapIn::fromMemory(data, 20);
    uint8_t buf[11];
    CHECK(in.read(buf, 11) == 11);
    CHECK(in.seek(in.tell() - 11, SEEK_SET) == 0);   // UnfreezeBlock's revert
    CHECK(in.tell() == 0);
    CHECK(in.seek(5, SEEK_CUR) == 0);                 // skipping a longer block's remainder
    CHECK(in.tell() == 5);
    CHECK(in.seek(0, SEEK_END) == 0);
    CHECK(in.tell() == 20);
    CHECK(in.seek(1, SEEK_CUR) != 0);
    CHECK(in.tell() == 20);
    CHECK(in.seek(-1, SEEK_SET) != 0);
}

TEST_CASE("snap reader: an empty memory source reads nothing") {
    SnapIn in = SnapIn::fromMemory(nullptr, 0);
    uint8_t b;
    CHECK(in.read(&b, 1) == 0);
    CHECK(in.tell() == 0);
}
