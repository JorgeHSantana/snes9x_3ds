#include "doctest.h"
#include "ibo_identity.h"

TEST_CASE("ibo identity: every entry holds its own position") {
    static unsigned short list[IBO_IDENTITY_ENTRIES];
    ibo_identity_fill(list, IBO_IDENTITY_ENTRIES);
    CHECK(list[0] == 0);
    CHECK(list[377] == 377);
    CHECK(list[65535] == 65535);
}

TEST_CASE("ibo identity: a run is the slice that starts at its first vertex") {
    unsigned short list[64];
    ibo_identity_fill(list, 64);
    const unsigned short *run = ibo_identity_run(list, 64, 10, 6);
    REQUIRE(run != nullptr);
    for (int k = 0; k < 6; k++) CHECK(run[k] == 10 + k);
    CHECK(ibo_identity_run(list, 64, 0, 64) == list);
    CHECK(ibo_identity_run(list, 64, 63, 1) == list + 63);
}

TEST_CASE("ibo identity: a run past the list, an empty run or no list gives no indices") {
    unsigned short list[64];
    ibo_identity_fill(list, 64);
    CHECK(ibo_identity_run(list, 64, 60, 5) == nullptr);
    CHECK(ibo_identity_run(list, 64, 64, 1) == nullptr);
    CHECK(ibo_identity_run(list, 64, 70, 1) == nullptr);
    CHECK(ibo_identity_run(list, 64, -1, 4) == nullptr);
    CHECK(ibo_identity_run(list, 64, 4, 0) == nullptr);
    CHECK(ibo_identity_run(nullptr, 64, 4, 2) == nullptr);
}
