#include "doctest.h"
#include "layer_defer.h"

TEST_CASE("layer defer: no layer behind the next line means nothing to drain") {
    const unsigned int startY[5] = {100, 100, 100, 100, 100};
    CHECK_FALSE(layer_any_deferred(startY, 100));
    CHECK_FALSE(layer_any_deferred(startY, 0));
}

TEST_CASE("layer defer: any single layer behind the line is deferred") {
    for (int lag = 0; lag < 5; lag++) {
        unsigned int startY[5] = {100, 100, 100, 100, 100};
        startY[lag] = 99;
        CHECK(layer_any_deferred(startY, 100));
    }
}

TEST_CASE("layer defer: a frame-start reset (cursors at 0) is deferred only once the line moved") {
    const unsigned int startY[5] = {0, 0, 0, 0, 0};
    CHECK_FALSE(layer_any_deferred(startY, 0));
    CHECK(layer_any_deferred(startY, 1));
}
