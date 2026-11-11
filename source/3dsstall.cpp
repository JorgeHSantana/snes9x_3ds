#include <3ds.h>
#include <stdio.h>
#include <limits.h>

#include "3dsstall.h"
#include "3dslog.h"
#include "3dssettings.h"
#include "stall_watch.h"

std::atomic<uint32_t>    g_stallBeat{0};
std::atomic<const char*> g_stallStage[STALL_T_COUNT];
std::atomic<uint32_t>    g_stallSteps[STALL_T_COUNT];

static std::atomic<bool> s_armed{false};
static Thread   s_thread = nullptr;
static uint64_t s_startMs = 0;

void stall3dsPath(char *out, unsigned int size)
{
    snprintf(out, size, "%s/debug_%s_stall.log", settings3DS.RootDir, settings3dsGetAppVersion("v"));
}

void stall3dsArm(bool armed)
{
    s_armed.store(armed);
}

static const char* stageOf(int t)
{
    const char* s = g_stallStage[t].load(std::memory_order_relaxed);
    return s ? s : "-";
}

static void stallThread(void*)
{
    StallWatch w = { g_stallBeat.load(), osGetTime(), 0 };
    for (;;) {
        svcSleepThread(250 * 1000 * 1000LL);
        const uint64_t now = osGetTime();
        const StallVerdict v = stall_watch_step(&w, s_armed.load(), g_stallBeat.load(), now);
        if (v == STALL_NONE) continue;

        // the step counters tell a thread that is blocked (same number in
        // the follow-up line) from one that still runs
        char line[512];
        const unsigned ms = (unsigned)(now - s_startMs);
        snprintf(line, sizeof(line),
            "[%02u.%03u] [stall] %s: main='%s' #%u | mixer='%s' #%u | msu-ra='%s' #%u | log-writer='%s' #%u",
            ms / 1000, ms % 1000,
            v == STALL_BEGAN ? "no frame ended for 2 s" : (v == STALL_STILL ? "still stalled" : "frames run again"),
            stageOf(STALL_T_MAIN),  (unsigned)g_stallSteps[STALL_T_MAIN].load(),
            stageOf(STALL_T_MIXER), (unsigned)g_stallSteps[STALL_T_MIXER].load(),
            stageOf(STALL_T_MSURA), (unsigned)g_stallSteps[STALL_T_MSURA].load(),
            stageOf(STALL_T_LOGW),  (unsigned)g_stallSteps[STALL_T_LOGW].load());

        // its own file, opened and closed here: nothing another thread
        // holds stands between this line and the card
        char path[PATH_MAX];
        stall3dsPath(path, sizeof(path));
        FILE* f = fopen(path, "a");
        if (f != NULL) {
            fputs(line, f);
            fputc('\n', f);
            fclose(f);
        }
        log3dsWrite("%s", line);   // after the file: the log's lock may be part of the stall
    }
}

void stall3dsInitialize()
{
    if (s_thread != nullptr) return;
    s_startMs = osGetTime();
    // above the main thread and the page-flip stamper (prio - 2) on the
    // main thread's core, so a main thread that spins is seen too
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    s_thread = threadCreate(stallThread, NULL, 0x2000, prio - 3, -2, true);
    if (s_thread == nullptr)
        log3dsWrite("[stall] watchdog thread failed to start");
}
