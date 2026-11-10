#include "doctest.h"
#include "gpu_probe.h"

GpuProbeMask g_gpuProbeMask = { false, false, false, false, false, false, false, false, false, false, false };

TEST_CASE("gpu_probe: the masks remove one piece per step, then stack") {
    const GpuProbeMask base = gpu_probe_mask(0);
    CHECK_FALSE(base.noGrade); CHECK_FALSE(base.noGlow); CHECK_FALSE(base.noShadows);
    CHECK_FALSE(base.noBlur); CHECK_FALSE(base.noFog); CHECK_FALSE(base.sharedRight); CHECK_FALSE(base.flat2D);
    CHECK(gpu_probe_mask(1).noGrade);
    CHECK_FALSE(gpu_probe_mask(1).noGlow);
    CHECK(gpu_probe_mask(2).noGlow);
    CHECK(gpu_probe_mask(3).noShadows);
    CHECK(gpu_probe_mask(4).noBlur);
    CHECK_FALSE(gpu_probe_mask(4).noFog);
    CHECK(gpu_probe_mask(5).noFog);
    CHECK_FALSE(gpu_probe_mask(5).noBlur);
    const GpuProbeMask plain = gpu_probe_mask(6);
    CHECK(plain.noGrade); CHECK(plain.noGlow); CHECK(plain.noShadows); CHECK(plain.noBlur); CHECK(plain.noFog);
    CHECK_FALSE(plain.sharedRight); CHECK_FALSE(plain.flat2D);
    CHECK(gpu_probe_mask(7).sharedRight);
    CHECK_FALSE(gpu_probe_mask(7).flat2D);
    CHECK(gpu_probe_mask(8).flat2D);
    CHECK(gpu_probe_mask(8).sharedRight);
    CHECK(gpu_probe_mask(9).noCull);
    CHECK_FALSE(gpu_probe_mask(9).noGrade);
    CHECK_FALSE(gpu_probe_mask(8).noCull);
}

TEST_CASE("gpu_probe: the tile cost steps run on plain stereo and stack fill, then the geometry shader") {
    for (int step = 0; step < 10; step++) {
        CHECK_FALSE(gpu_probe_mask(step).noFill);
        CHECK_FALSE(gpu_probe_mask(step).vsOnly);
    }
    const GpuProbeMask plain = gpu_probe_mask(6), fill = gpu_probe_mask(10), vs = gpu_probe_mask(11);
    CHECK(fill.noFill); CHECK_FALSE(fill.vsOnly);
    CHECK(vs.noFill); CHECK(vs.vsOnly);
    // same effects configuration as step 6: the three read against each other
    CHECK(fill.noGrade == plain.noGrade); CHECK(fill.noGlow == plain.noGlow); CHECK(fill.noShadows == plain.noShadows);
    CHECK(fill.noBlur == plain.noBlur); CHECK(fill.noFog == plain.noFog);
    CHECK(vs.noBlur); CHECK(vs.noFog);
    CHECK_FALSE(fill.sharedRight); CHECK_FALSE(fill.flat2D); CHECK_FALSE(fill.noCull);
    CHECK_FALSE(vs.sharedRight); CHECK_FALSE(vs.flat2D); CHECK_FALSE(vs.noCull);
}

TEST_CASE("gpu_probe: every step has a name") {
    for (int i = 0; i < GPU_PROBE_STEPS; i++)
        CHECK(gpu_probe_name(i) != gpu_probe_name(GPU_PROBE_STEPS));
}

TEST_CASE("gpu_probe: a step fills after STEP_FRAMES, counting only after the settle") {
    GpuProbeState s;
    const unsigned long T[4] = { 300, 957, 957, 0 }, O[4] = { 100, 957, 900, 0 };
    gpu_probe_start(&s);
    CHECK(s.active);
    CHECK(s.step == 0);
    for (int f = 1; f < GPU_PROBE_STEP_FRAMES; f++)
        CHECK_FALSE(gpu_probe_frame(&s, 10, 100, 7, 50, 3000, f % 2 == 0, f % 3 == 0, T, O, 40));
    CHECK(gpu_probe_frame(&s, 10, 100, 7, 50, 3000, true, true, T, O, 40));
    CHECK(s.busyTicks == 7ull * s.counted);
    CHECK(s.draws == 50ull * s.counted);
    CHECK(s.verts == 3000ull * s.counted);
    CHECK(s.counted == GPU_PROBE_STEP_FRAMES - GPU_PROBE_SETTLE_FRAMES);
    CHECK(s.waitTicks == 10ull * s.counted);
    CHECK(s.loadTicks == 100ull * s.counted);
    CHECK(s.overBudget > 0);
    CHECK(s.skipped > 0);
    CHECK(s.drawn == s.counted - s.skipped);   // tile stats only from frames that drew
    CHECK(s.culled == 40ull * s.drawn);
    CHECK(s.tiles[1] == 957ull * s.drawn);
    CHECK(s.opaque[2] == 900ull * s.drawn);
    CHECK(gpu_probe_per_drawn(&s, s.opaque[0]) == 100);
    CHECK(s.step == 0);   // still naming the step that filled
}

TEST_CASE("gpu_probe: next resets the accumulators and ends after the last step") {
    GpuProbeState s;
    const unsigned long T[4] = { 1, 1, 1, 1 }, O[4] = { 1, 1, 1, 1 };
    gpu_probe_start(&s);
    for (int step = 0; step < GPU_PROBE_STEPS; step++) {
        CHECK(s.active);
        CHECK(s.step == step);
        for (int f = 0; f < GPU_PROBE_STEP_FRAMES; f++) gpu_probe_frame(&s, 1, 1, 1, 1, 1, false, false, T, O, 0);
        gpu_probe_next(&s);
        CHECK(s.frame == 0);
        CHECK(s.waitTicks == 0);
        CHECK(s.draws == 0);
        CHECK(s.drawn == 0);
        CHECK(s.tiles[0] == 0);
    }
    CHECK_FALSE(s.active);
    CHECK_FALSE(gpu_probe_frame(&s, 1, 1, 1, 1, 1, false, false, T, O, 0));   // idle: nothing counted
    CHECK(s.frame == 0);
}

TEST_CASE("gpu_probe: the current mask is neutral while idle") {
    GpuProbeState s = { false, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0 };
    CHECK_FALSE(gpu_probe_current_mask(&s).flat2D);
    CHECK_FALSE(gpu_probe_current_mask(&s).on);
    gpu_probe_start(&s);
    CHECK(gpu_probe_current_mask(&s).on);
    s.step = 8;
    CHECK(gpu_probe_current_mask(&s).flat2D);
}
