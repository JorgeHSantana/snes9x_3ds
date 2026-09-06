
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/stat.h>

#include "3dssettings.h"
#include "3dslog.h"
#include <3ds.h>

// The MSU-1 read-ahead thread logs through msu1_diag while the main
// thread logs too (and closes the file at exit): newlib FILE state is not
// thread-safe, and a check-then-fprintf on a file being closed reads a
// NULL FILE. One lock around every use.
static LightLock s_logLock;
static bool s_logLockInit = false;
static inline void logLockEnsure(void) { if (!s_logLockInit) { LightLock_Init(&s_logLock); s_logLockInit = true; } }

static const bool FORCE_DEBUG_LOGS = false;

static FILE *logFile = NULL;
static u64 osTime;
static u64 elapsedMs = 0;

void setElapsedTime(u64 ms) {
    char timestamp[16];
    int seconds = ((int)(ms / 1000)) % 100;
    int milliseconds = ((int)ms) % 1000;

    snprintf(timestamp, sizeof(timestamp) - 1, "[%02d.%03d]", seconds, milliseconds);
    fprintf(logFile, "%s ", timestamp);
}

void log3dsInitialize() {
    if (logFile || (!settings3DS.LogFileEnabled && !FORCE_DEBUG_LOGS)) return;

    // only needed if we also want to debug the very first run
    if (FORCE_DEBUG_LOGS) {
        mkdir(settings3DS.RootDir, 0777);
    }

    char filepath[PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/debug_%s_session.log", settings3DS.RootDir, settings3dsGetAppVersion("v"));

    logFile = fopen(filepath, "w"); // overwrite file on each run
    if (logFile) {
        setvbuf(logFile, NULL, _IOLBF, 512);   // line-buffered: diagnostics reach the file immediately
        osTime = osGetTime();
    }
}

void log3dsWrite(const char *fmt, ...) {
    if (!logFile || (!settings3DS.LogFileEnabled && !FORCE_DEBUG_LOGS)) return;
    logLockEnsure();
    LightLock_Lock(&s_logLock);
    if (!logFile) { LightLock_Unlock(&s_logLock); return; }   // closed while we waited

    u64 currentElapsedMs = osGetTime() - osTime;

    if (currentElapsedMs > elapsedMs) {
        setElapsedTime(currentElapsedMs);
        elapsedMs = currentElapsedMs;
    } else {
        fprintf(logFile, "%s ", "       |");
    }
    
    va_list args;
    va_start(args, fmt);
    vfprintf(logFile, fmt, args);
    fprintf(logFile, "\n");
    fflush(logFile);
    va_end(args);
    LightLock_Unlock(&s_logLock);
}

void log3dsClose(void) {
    logLockEnsure();
    LightLock_Lock(&s_logLock);
    if (logFile) {
        fclose(logFile);
        logFile = NULL;
    }
    LightLock_Unlock(&s_logLock);
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
