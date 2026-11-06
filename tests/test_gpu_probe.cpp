#include "doctest.h"
#include "gpu_probe.h"

GpuProbeMask g_gpuProbeMask = { false, false, false, false, false, false, false, false };

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
}

TEST_CASE("gpu_probe: every step has a name") {
    for (int i = 0; i < GPU_PROBE_STEPS; i++)
        CHECK(gpu_probe_name(i) != gpu_probe_name(GPU_PROBE_STEPS));
}

TEST_CASE("gpu_probe: a step fills after STEP_FRAMES, counting only after the settle") {
    GpuProbeState s;
    gpu_probe_start(&s);
    CHECK(s.active);
    CHECK(s.step == 0);
    for (int f = 1; f < GPU_PROBE_STEP_FRAMES; f++)
        CHECK_FALSE(gpu_probe_frame(&s, 10, 100, 7, f % 2 == 0, f % 3 == 0));
    CHECK(gpu_probe_frame(&s, 10, 100, 7, true, true));
    CHECK(s.busyTicks == 7ull * s.counted);
    CHECK(s.counted == GPU_PROBE_STEP_FRAMES - GPU_PROBE_SETTLE_FRAMES);
    CHECK(s.waitTicks == 10ull * s.counted);
    CHECK(s.loadTicks == 100ull * s.counted);
    CHECK(s.overBudget > 0);
    CHECK(s.skipped > 0);
    CHECK(s.step == 0);   // still naming the step that filled
}

TEST_CASE("gpu_probe: next resets the accumulators and ends after the last step") {
    GpuProbeState s;
    gpu_probe_start(&s);
    for (int step = 0; step < GPU_PROBE_STEPS; step++) {
        CHECK(s.active);
        CHECK(s.step == step);
        for (int f = 0; f < GPU_PROBE_STEP_FRAMES; f++) gpu_probe_frame(&s, 1, 1, 1, false, false);
        gpu_probe_next(&s);
        CHECK(s.frame == 0);
        CHECK(s.waitTicks == 0);
    }
    CHECK_FALSE(s.active);
    CHECK_FALSE(gpu_probe_frame(&s, 1, 1, 1, false, false));   // idle: nothing counted
    CHECK(s.frame == 0);
}

TEST_CASE("gpu_probe: the current mask is neutral while idle") {
    GpuProbeState s = { false, 8, 0, 0, 0, 0, 0, 0, 0 };
    CHECK_FALSE(gpu_probe_current_mask(&s).flat2D);
    CHECK_FALSE(gpu_probe_current_mask(&s).on);
    gpu_probe_start(&s);
    CHECK(gpu_probe_current_mask(&s).on);
    s.step = 8;
    CHECK(gpu_probe_current_mask(&s).flat2D);
}
