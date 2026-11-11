#ifndef _3DSLOG_H
#define _3DSLOG_H

void log3dsInitialize();
void log3dsWrite(const char *fmt, ...);
void log3dsClose(void);
void log3dsTick();
void log3dsStartWriter(int coreId);   // the SD writer thread, once the sound init picked a core
void log3dsFlush();   // drain now: before an operation a crash report would need the tail of
void log3dsMark(const char *what);   // a line plus a drain: the last one on the SD names where a freeze happened
void log3dsUncleanPath(char *out, unsigned int size);   // the log a session that never closed left behind (log_blackbox.h)
bool log3dsIsReady();
const char* log3dsGetCurrentDate();

#define DUMP_VECTOR_INFO(name, vec) \
    log3dsWrite("%s: size=%zu, cap=%zu, data=%p", name, vec.size(), vec.capacity(), vec.data())

#define DUMP_UNORDERED_MAP_INFO(name, map) \
    log3dsWrite("%s: size=%zu, bucket_count=%zu, addr=%p", name, map.size(), map.bucket_count(), &map)

#endif
