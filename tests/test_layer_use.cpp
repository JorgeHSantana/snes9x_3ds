#include "doctest.h"
#include <initializer_list>
#include "../source/3dslayeruse.h"

TEST_CASE("layer toggle: hide unused never hides a disabled layer") {
    for (bool used : {false, true}) {
        for (bool enabled : {false, true}) {
            for (bool hide_unused : {false, true}) {
                CHECK(layer_toggle_visible(used, enabled, hide_unused) ==
                      !(hide_unused && enabled && !used));
            }
        }
    }
}

TEST_CASE("layer toggle: disable, render, reopen and reenable") {
    for (int layer = 0; layer < LAYER_USE_LAYERS; ++layer) {
        LayerUse usage;
        layerUseReset(&usage);
        layerUseMark(&usage, layer, 0);
        layerUseFrameEnd(&usage);
        CHECK(layer_toggle_visible(layerUseLayerUsed(&usage, layer), true, true));
        layerUseFrameStart(&usage);
        layerUseFrameEnd(&usage); // Disabled layer no longer draws.
        CHECK_FALSE(layerUseLayerUsed(&usage, layer));
        CHECK(layer_toggle_visible(layerUseLayerUsed(&usage, layer), false, true));
        layerUseMark(&usage, layer, 0); // Reenabled layer draws again.
        layerUseFrameEnd(&usage);
        CHECK(layer_toggle_visible(layerUseLayerUsed(&usage, layer), true, true));
    }
}

TEST_CASE("layer use: nothing is used before a frame is latched") {
    LayerUse u; layerUseReset(&u);
    layerUseMark(&u, 0, 0);
    CHECK(layerUseUsed(&u, 0, 0) == false);      // accumulating, not latched
    layerUseFrameEnd(&u);
    CHECK(layerUseUsed(&u, 0, 0) == true);
}

TEST_CASE("layer use: frame start clears the accumulator but keeps the latched frame") {
    LayerUse u; layerUseReset(&u);
    layerUseMark(&u, 2, 1); layerUseFrameEnd(&u);
    layerUseFrameStart(&u);
    CHECK(layerUseUsed(&u, 2, 1) == true);       // editor still sees last frame
    layerUseFrameEnd(&u);                         // an empty frame latches empty
    CHECK(layerUseUsed(&u, 2, 1) == false);
}

TEST_CASE("layer use: rows are independent") {
    LayerUse u; layerUseReset(&u);
    layerUseMark(&u, 4, 3); layerUseFrameEnd(&u);
    CHECK(layerUseUsed(&u, 4, 3) == true);
    CHECK(layerUseUsed(&u, 4, 2) == false);
    CHECK(layerUseUsed(&u, 3, 1) == false);
    CHECK(layerUseLayerUsed(&u, 4) == true);
    CHECK(layerUseLayerUsed(&u, 3) == false);
}

TEST_CASE("layer use: out-of-range rows are ignored, not written") {
    LayerUse u; layerUseReset(&u);
    layerUseMark(&u, -1, 0); layerUseMark(&u, 5, 0); layerUseMark(&u, 0, 4); layerUseMark(&u, 0, -1);
    layerUseFrameEnd(&u);
    for (int l = 0; l < LAYER_USE_LAYERS; l++)
        CHECK(layerUseLayerUsed(&u, l) == false);
    CHECK(layerUseUsed(&u, 9, 9) == false);
}

TEST_CASE("layer use: the counter saturates instead of wrapping to zero") {
    LayerUse u; layerUseReset(&u);
    for (int i = 0; i < 70000; i++) layerUseMark(&u, 1, 0);
    layerUseFrameEnd(&u);
    CHECK(layerUseUsed(&u, 1, 0) == true);
}
