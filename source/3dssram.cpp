#include <3ds.h>
#include <stdio.h>
#include <atomic>

#include "3dssram.h"
#include "3dslog.h"
#include "sram_writer.h"
#include "perf_stats.h"
#include "Snes9x/snes9x.h"
#include "Snes9x/cpuexec.h"

// The queue and its buffer live here (128 KiB + path), touched only under
// s_lock; the worker sleeps on s_request and reports on s_done.
static SramWriterQueue s_queue;
static LightLock  s_lock;
static LightEvent s_request;
static LightEvent s_done;
static Thread     s_thread = nullptr;
static std::atomic<bool> s_quit{false};
static std::atomic<bool> s_started{false};
static u64 s_queuedTick = 0;

static void sramWorker(void*)
{
    for (;;) {
        LightEvent_Wait(&s_request);
        if (s_quit.load()) break;

        LightLock_Lock(&s_lock);
        bool have = s_queue.take();
        char path[SRAM_WRITER_PATH_MAX];
        size_t size = s_queue.size;
        if (have) memcpy(path, s_queue.path, sizeof(path));
        LightLock_Unlock(&s_lock);
        if (!have) continue;

        // plain stdio on purpose: BufferedFileWriter's transport buffer
        // (g_fileBuffer) belongs to the main thread
        u64 t0 = svcGetSystemTick();
        bool ok = false;
        FILE *f = fopen(path, "wb");
        if (f) {
            // the queue's data is stable while WRITING: request() refuses
            // new jobs until poll() consumed this one
            ok = fwrite(s_queue.data, 1, size, f) == size;
            if (fclose(f) != 0) ok = false;
        }
        u64 t1 = svcGetSystemTick();

        LightLock_Lock(&s_lock);
        s_queue.complete(ok);
        LightLock_Unlock(&s_lock);
        log3dsWrite("[perf][sram] async write=%lluus bytes=%u result=%s",
            (unsigned long long)ticks_to_microseconds(t1 - t0, SYSCLOCK_ARM11),
            (unsigned)size, ok ? "ok" : "FAILED; retry pending");
        LightEvent_Signal(&s_done);
    }
    s_started.store(false);
}

void sram3dsInitialize()
{
    if (s_thread) return;
    LightLock_Init(&s_lock);
    LightEvent_Init(&s_request, RESET_ONESHOT);
    LightEvent_Init(&s_done, RESET_STICKY);
    s_queue.reset();
    s_quit.store(false);
    // lowest priority, default core, joinable: the write must never
    // compete with emulation, and finalize waits for it
    s_thread = threadCreate(sramWorker, NULL, 0x4000, 0x3F, -2, false);
    s_started.store(s_thread != nullptr);
    if (!s_thread)
        log3dsWrite("[sram] async writer thread failed; saves stay synchronous");
}

bool sram3dsBusy()
{
    if (!s_thread) return false;
    LightLock_Lock(&s_lock);
    bool b = s_queue.busy();
    LightLock_Unlock(&s_lock);
    return b;
}

bool sram3dsRequestAsync(const char* path, const uint8_t* sram, size_t size)
{
    if (!s_thread) return false;
    LightLock_Lock(&s_lock);
    bool queued = s_queue.request(path, sram, size);
    LightLock_Unlock(&s_lock);
    if (queued) {
        s_queuedTick = svcGetSystemTick();
        LightEvent_Clear(&s_done);
        LightEvent_Signal(&s_request);
    }
    return queued;
}

void sram3dsPoll()
{
    if (!s_thread) return;
    LightLock_Lock(&s_lock);
    bool consumed = s_queue.poll(CPU.SRAMModified);
    bool ok = s_queue.lastOk;
    LightLock_Unlock(&s_lock);
    if (consumed && !ok)
        log3dsWrite("[sram] async save failed - SRAM kept dirty, the timer retries");
}

void sram3dsDrain()
{
    if (!s_thread) return;
    // bounded: a stuck SD must not hang exit; 3 s covers a slow FAT write
    for (int i = 0; i < 300 && sram3dsBusy(); i++)
        svcSleepThread(10 * 1000 * 1000LL);
    if (sram3dsBusy())
        log3dsWrite("[sram] drain: async write still running after 3s");
    sram3dsPoll();
}

void sram3dsFinalize()
{
    if (!s_thread) return;
    sram3dsDrain();
    s_quit.store(true);
    LightEvent_Signal(&s_request);
    threadJoin(s_thread, U64_MAX);
    threadFree(s_thread);
    s_thread = nullptr;
}
