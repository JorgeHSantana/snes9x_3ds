#include "doctest.h"
#include "snapshot_workspace.h"
#include "bufferedfilewriter.h"
#include <limits>

TEST_CASE("snapshot workspace is bounded, stable and clears serialized padding on reuse") {
    SnapshotWorkspace<65536> workspace;
    uint8_t* first = workspace.prepare(65536);
    REQUIRE(first != nullptr);
    memset(first, 0xa5, 65536);
    uint8_t* small = workspace.prepare(17);
    REQUIRE(small == first);
    for (size_t i = 0; i < 17; ++i) CHECK(small[i] == 0);
    CHECK(small[17] == 0xa5);
    REQUIRE(workspace.prepare(65536) == first);
    bool all_zero = true;
    for (size_t i = 0; i < 65536; ++i) all_zero &= first[i] == 0;
    CHECK(all_zero);
    CHECK(workspace.prepare(0) == nullptr);
    CHECK(workspace.prepare(65537) == nullptr);
    CHECK(workspace.prepare(std::numeric_limits<size_t>::max()) == nullptr);
}

TEST_CASE("serialization rejection propagates through memory writer") {
    uint8_t output[16] = {};
    BufferedFileWriter writer;
    REQUIRE(writer.openMem(output, sizeof(output)));
    writer.fail();
    CHECK(writer.memOverflowed());
    CHECK(writer.write(output, 1) == 0);
    CHECK(writer.close() == EOF);
}
