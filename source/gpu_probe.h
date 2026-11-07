#ifndef GPU_PROBE_H
#define GPU_PROBE_H

// GPU cost probe (Jorge's MMX3 logs, 2026-11-05): with every Auto tier
// shed the GPU still took 13-15 ms a frame in the heavy phase, and the
// 3DS has no GPU profiler. This measures by elimination: with the game
// running it switches one piece of the render off at a time, holds each
// configuration for GPU_PROBE_STEP_FRAMES frames, and the caller logs
// that step's GPU wait / load. The differences between steps are the
// cost of each piece. Pure logic, host-tested; the render hooks read
// the mask through g_gpuProbeMask.

#define GPU_PROBE_STEP_FRAMES   300   // 5 s at 60 Hz
#define GPU_PROBE_SETTLE_FRAMES  60   // the Auto crossfades and the pacer settle: not counted
#define GPU_PROBE_STEPS          10

struct GpuProbeMask {
    bool noGrade;       // the output colour grade (stages 1-5 of the blit)
    bool noGlow;        // the glow phase
    bool noShadows;     // the shadow phase
    bool noBlur;        // the depth blur (the ghost passes)
    bool noFog;         // fade / haze (the per-tier tint)
    bool sharedRight;   // the right eye's layer pass skipped: composite reads the left texture twice
    bool flat2D;        // the slider reads 0: one pass, one composite
    bool noCull;        // the BG occlusion cull off (occlusion_cull.h): every hidden tile drawn again
    bool on;            // a probe is running: the Auto tiers hold Full so the steps compare like with like
};

struct GpuProbeState {
    bool active;
    int step;
    int frame;              // frames into the current step
    int counted;            // frames measured (after the settle)
    unsigned long long waitTicks, loadTicks, busyTicks;
    unsigned long long draws, verts;   // draw calls and vertices submitted, summed
    int overBudget, skipped;
};

static inline const char *gpu_probe_name(int step)
{
    switch (step) {
    case 0: return "baseline";
    case 1: return "no grade";
    case 2: return "no glow";
    case 3: return "no shadows";
    case 4: return "no blur";
    case 5: return "no fade/haze";
    case 6: return "plain stereo (1-5 off)";
    case 7: return "plain stereo, right pass shared";
    case 8: return "3D off";
    case 9: return "no occlusion cull";
    default: return "?";
    }
}

static inline GpuProbeMask gpu_probe_mask(int step)
{
    GpuProbeMask m = { false, false, false, false, false, false, false, false, false };
    switch (step) {
    case 1: m.noGrade = true; break;
    case 2: m.noGlow = true; break;
    case 3: m.noShadows = true; break;
    case 4: m.noBlur = true; break;
    case 5: m.noFog = true; break;
    case 6: case 7: case 8:
        m.noGrade = m.noGlow = m.noShadows = m.noBlur = m.noFog = true;
        m.sharedRight = step >= 7;
        m.flat2D = step == 8;
        break;
    case 9: m.noCull = true; break;
    default: break;
    }
    return m;
}

static inline void gpu_probe_start(GpuProbeState *s)
{
    s->active = true;
    s->step = 0; s->frame = 0; s->counted = 0;
    s->waitTicks = s->loadTicks = s->busyTicks = 0;
    s->draws = s->verts = 0;
    s->overBudget = s->skipped = 0;
}

// One frame of the current step. Returns true when the step just filled:
// the caller logs the accumulators (still holding that step's totals and
// s->step naming it), then gpu_probe_next moves on.
static inline bool gpu_probe_frame(GpuProbeState *s, unsigned long long waitTicks, unsigned long long loadTicks, unsigned long long busyTicks, unsigned draws, unsigned verts, bool overBudget, bool skipped)
{
    if (!s->active) return false;
    s->frame++;
    if (s->frame > GPU_PROBE_SETTLE_FRAMES) {
        s->counted++;
        s->waitTicks += waitTicks;
        s->loadTicks += loadTicks;
        s->busyTicks += busyTicks;
        s->draws += draws;
        s->verts += verts;
        if (overBudget) s->overBudget++;
        if (skipped) s->skipped++;
    }
    return s->frame >= GPU_PROBE_STEP_FRAMES;
}

// After the log: the next step, or the end (active cleared, mask neutral).
static inline void gpu_probe_next(GpuProbeState *s)
{
    s->step++;
    s->frame = 0; s->counted = 0;
    s->waitTicks = s->loadTicks = s->busyTicks = 0;
    s->draws = s->verts = 0;
    s->overBudget = s->skipped = 0;
    if (s->step >= GPU_PROBE_STEPS) s->active = false;
}

static inline GpuProbeMask gpu_probe_current_mask(const GpuProbeState *s)
{
    GpuProbeMask m = gpu_probe_mask(s->active ? s->step : 0);
    m.on = s->active;
    return m;
}

// the mask the render reads (defined in 3dsmain.cpp, neutral when idle)
extern GpuProbeMask g_gpuProbeMask;

#endif
