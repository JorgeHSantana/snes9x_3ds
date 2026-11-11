#ifndef LOG_BLACKBOX_H
#define LOG_BLACKBOX_H

#include <stddef.h>
#include <string.h>

// The session log is truncated at boot, so a freeze that needs the power
// button used to take its evidence with it. A log that does not end with
// the close marker is set aside at the next boot (one file, replaced) and
// Send Log carries its tail ahead of the running session's.

#define LOG_CLOSE_MARKER      "[log] closed"
#define LOG_UNCLEAN_TAIL_MAX  12000   // bytes of the set-aside log Send Log carries
#define LOG_CLOSE_SCAN_BYTES  256     // the marker is the last line: this much of the tail is read

// true when the tail of a log file shows the session closed it; an empty
// file counts as closed (nothing worth keeping)
static inline bool log_closed_cleanly(const char* tail, size_t n)
{
    if (n == 0) return true;
    if (tail == NULL) return false;
    const size_t m = sizeof(LOG_CLOSE_MARKER) - 1;
    if (n < m) return false;
    for (size_t i = 0; i + m <= n; i++)
        if (memcmp(tail + i, LOG_CLOSE_MARKER, m) == 0) return true;
    return false;
}

#endif
