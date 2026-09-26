#include <atomic>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <3ds.h>
#include "3dssettings.h"
#include "3dslog.h"
#include "buffered_log.h"

// Initialize/close are main-thread-only. READY publishes the initialized
// lock. Every spool access is serialized with background producers; the
// SD is written by the writer thread (log3dsStartWriter, on the mixer's
// core) from the chunk it took, outside the lock - or synchronously by
// log3dsTick until that thread exists (boot) and by log3dsFlush at the
// moments a crash report needs the tail.
static LightLock LOG_LOCK;
static bool LOCK_INITIALIZED = false;
static std::atomic<bool> READY{false};
static BufferedLog LOG;
static uint64_t START_MS = 0;
static uint64_t LAST_ELAPSED_MS = 0;

static Thread     s_writer = nullptr;
static LightEvent s_writerWake;
static LightEvent s_writerIdle;
static std::atomic<bool> s_writerQuit{false};

static void logFail()
{
    READY.store(false);
    (void)LOG.close(); // Failed sink: stop; never recursively log I/O errors.
}

static void logWriter(void*)
{
    for (;;) {
        LightEvent_Wait(&s_writerWake);
        if (s_writerQuit.load()) break;
        // take under the lock, write with no lock held (producers never
        // wait for the SD), release under the lock
        LightLock_Lock(&LOG_LOCK);
        const int c = READY.load() ? LOG.drain_take() : -1;
        LightLock_Unlock(&LOG_LOCK);
        if (c >= 0) {
            const bool ok = LOG.drain_write(c);      // the SD access: no lock held
            LightLock_Lock(&LOG_LOCK);
            LOG.drain_release(c, ok);
            if (!ok) logFail();
            LightLock_Unlock(&LOG_LOCK);
        }
        LightEvent_Signal(&s_writerIdle);
    }
}

void log3dsInitialize()
{
    if (READY.load() || !settings3DS.LogFileEnabled) {
        return;
    }
    if (!LOCK_INITIALIZED) {
        LightLock_Init(&LOG_LOCK);
        LightEvent_Init(&s_writerWake, RESET_ONESHOT);
        LightEvent_Init(&s_writerIdle, RESET_STICKY);
        LOCK_INITIALIZED = true;
    }
    char path[PATH_MAX];
    const int32_t count = snprintf(path, sizeof(path), "%s/debug_%s_session.log",
        settings3DS.RootDir, settings3dsGetAppVersion("v"));
    if (count < 0 || static_cast<size_t>(count) >= sizeof(path)) {
        return;
    }
    LightLock_Lock(&LOG_LOCK);
    START_MS = osGetTime();
    LAST_ELAPSED_MS = 0;
    READY.store(LOG.open(path, START_MS));
    LightLock_Unlock(&LOG_LOCK);
}

void log3dsStartWriter(int coreId)
{
    if (s_writer != nullptr || !LOCK_INITIALIZED) return;
    s_writerQuit.store(false);
    LightEvent_Signal(&s_writerIdle);
    s_writer = threadCreate(logWriter, NULL, 0x2000, 0x3F, coreId, false);
    log3dsWrite(s_writer ? "[log] SD writer on core %d" : "[log] SD writer thread failed; the log drains on the main thread", coreId);
}

void log3dsWrite(const char* format, ...)
{
    if (format == nullptr || !READY.load()) {
        return;
    }
    // the whole line is composed here so the spool takes it in one piece
    char line[BufferedLog::LINE_CAPACITY];
    LightLock_Lock(&LOG_LOCK);
    if (!READY.load()) {
        LightLock_Unlock(&LOG_LOCK);
        return;
    }
    const uint64_t now_ms = osGetTime();
    const uint64_t elapsed_ms = now_ms >= START_MS ? now_ms - START_MS : 0;
    int n;
    if (elapsed_ms > LAST_ELAPSED_MS) {
        n = snprintf(line, sizeof(line), "[%02u.%03u] ", static_cast<unsigned>((elapsed_ms / 1000) % 100),
            static_cast<unsigned>(elapsed_ms % 1000));
        LAST_ELAPSED_MS = elapsed_ms;
    } else {
        n = snprintf(line, sizeof(line), "       | ");
    }
    va_list args;
    va_start(args, format);
    int m = vsnprintf(line + n, sizeof(line) - (size_t)n - 1, format, args);
    va_end(args);
    if (m < 0) m = 0;
    if ((size_t)(n + m) >= sizeof(line) - 1) m = (int)sizeof(line) - 2 - n;
    line[n + m] = '\n'; line[n + m + 1] = '\0';
    bool ok = LOG.write("%s", line);
    // once a second the front chunk goes to the writer; no SD access here
    if (ok && LOG.tick(now_ms) && s_writer) LightEvent_Signal(&s_writerWake);
    if (!ok) logFail();
    LightLock_Unlock(&LOG_LOCK);
}

void log3dsFlush()
{
    if (!READY.load()) {
        return;
    }
    // let the writer finish a chunk in flight first (safe moments only)
    if (s_writer) {
        LightEvent_Clear(&s_writerIdle);
        LightEvent_Signal(&s_writerWake);
        LightEvent_Wait(&s_writerIdle);
    }
    LightLock_Lock(&LOG_LOCK);
    if (READY.load() && !LOG.flush(osGetTime())) logFail();
    LightLock_Unlock(&LOG_LOCK);
}

void log3dsTick()
{
    if (!READY.load() || LightLock_TryLock(&LOG_LOCK) != 0) {
        return;
    }
    if (READY.load() && LOG.tick(osGetTime())) {
        if (s_writer) LightEvent_Signal(&s_writerWake);
        else if (!LOG.drain()) logFail();   // no writer yet (boot): drain here, no game runs
    }
    LightLock_Unlock(&LOG_LOCK);
}

bool log3dsIsReady()
{
    return READY.load();
}

void log3dsClose()
{
    if (!LOCK_INITIALIZED) {
        return;
    }
    if (s_writer) {
        s_writerQuit.store(true);
        LightEvent_Signal(&s_writerWake);
        threadJoin(s_writer, U64_MAX);
        threadFree(s_writer);
        s_writer = nullptr;
    }
    LightLock_Lock(&LOG_LOCK);
    READY.store(false);
    (void)LOG.close(); // Best-effort shutdown; no working diagnostic sink.
    LightLock_Unlock(&LOG_LOCK);
}

const char* log3dsGetCurrentDate() {
    static char dateFormatted[19];

    time_t seconds =  osGetTime() / 1000;
    const time_t SECONDS_BETWEEN_1900_AND_1970 = 2208988800ULL;
    time_t unix_time = seconds - SECONDS_BETWEEN_1900_AND_1970;
    struct tm *timeinfo = localtime(&unix_time);

    if (!timeinfo || strftime(dateFormatted, sizeof(dateFormatted), "%m/%d/%y %H:%M:%S", timeinfo) == 0) {
        dateFormatted[0] = '\0';
    }

    return dateFormatted;
}
