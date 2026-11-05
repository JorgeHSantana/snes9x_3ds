#ifndef GPU_PRESSURE_H
#define GPU_PRESSURE_H

// GPU pressure (Jorge's New-mode log, 2026-11-05): with the CPU done in
// 5 ms the frame still stalled up to 9 ms in C3D_FrameBegin waiting for
// the GPU, 100 of 300 frames over budget - and the Auto controllers only
// react to skipped frames, so they sat at Full while the GPU drowned.
// A frame whose GPU wait exceeds this share of the budget counts as a
// skipped frame for Blur/Shadow/Glow Auto: the blur ghosts, the soft
// shadow edge and the glow spread are the GPU work those tiers shed.
#define GPU_PRESSURE_WAIT_PERCENT 20

static inline bool gpu_pressure(unsigned long long waitTicks, unsigned long long ticksPerFrame)
{
    return ticksPerFrame != 0 && waitTicks * 100 > ticksPerFrame * GPU_PRESSURE_WAIT_PERCENT;
}

#endif
