#include <atomic>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <3ds.h>
#include "3dssettings.h"
#include "3dslog.h"
#include "buffered_log.h"

// Initialize/close are main-thread-only. READY publishes the initialized
// lock. Every file/timestamp access is serialized with background producers.
static LightLock LOG_LOCK;
static bool LOCK_INITIALIZED = false;
static std::atomic<bool> READY{false};
static BufferedLog LOG;
static uint64_t START_MS = 0;
static uint64_t LAST_ELAPSED_MS = 0;

void log3dsInitialize()
{
    if (READY.load() || !settings3DS.LogFileEnabled) {
        return;
    }
    if (!LOCK_INITIALIZED) {
        LightLock_Init(&LOG_LOCK);
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

void log3dsWrite(const char* format, ...)
{
    if (format == nullptr || !READY.load()) {
        return;
    }
    LightLock_Lock(&LOG_LOCK);
    if (!READY.load()) {
        LightLock_Unlock(&LOG_LOCK);
        return;
    }
    const uint64_t now_ms = osGetTime();
    const uint64_t elapsed_ms = now_ms >= START_MS ? now_ms - START_MS : 0;
    bool ok;
    if (elapsed_ms > LAST_ELAPSED_MS) {
        ok = LOG.write("[%02u.%03u] ", static_cast<uint32_t>((elapsed_ms / 1000) % 100),
            static_cast<uint32_t>(elapsed_ms % 1000));
        LAST_ELAPSED_MS = elapsed_ms;
    } else {
        ok = LOG.write("       | ");
    }
    va_list args;
    va_start(args, format);
    if (ok) {
        ok = LOG.write_v(format, args);
    }
    va_end(args);
    if (ok) {
        ok = LOG.write("\n") && LOG.tick(now_ms);
    }
    if (!ok) {
        READY.store(false);
        (void)LOG.close(); // Failed sink: stop; never recursively log I/O errors.
    }
    LightLock_Unlock(&LOG_LOCK);
}

void log3dsTick()
{
    if (!READY.load() || LightLock_TryLock(&LOG_LOCK) != 0) {
        return;
    }
    if (READY.load() && !LOG.tick(osGetTime())) {
        READY.store(false);
        (void)LOG.close(); // Preserve gameplay when the SD rejects log writes.
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
