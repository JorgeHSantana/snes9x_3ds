#ifndef LOG_SPOOL_H
#define LOG_SPOOL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Two fixed chunks between the log's producers and the SD writer. Lines
// are appended to the front chunk; when it fills, or once a second, it
// is handed over ("ready") and the other chunk becomes the front. The
// writer takes the ready chunk, writes it outside any lock, releases it.
// If the front fills while the writer still holds the other chunk, the
// line is dropped and counted - the log must never wait for the SD on
// the emulation thread (issue #59: a 13 ms flush inside a rewind
// capture on the Old 3DS). Pure state; the .cpp adds the lock and thread.
struct LogSpool
{
    static constexpr size_t CHUNK = 32 * 1024;

    char     data[2][CHUNK];
    size_t   len[2];
    int      front;           // chunk taking appends
    int      ready;           // chunk handed to the writer, -1 = none
    uint32_t droppedLines;
    uint32_t droppedBytes;

    void reset() { len[0] = len[1] = 0; front = 0; ready = -1; droppedLines = 0; droppedBytes = 0; }

    // true when the front chunk was handed over (the writer has work)
    bool rotate()
    {
        if (len[front] == 0 || ready >= 0) return false;
        ready = front;
        front ^= 1;
        len[front] = 0;
        return true;
    }

    // one whole line; returns whether it was kept
    bool append(const char* line, size_t n)
    {
        if (n == 0) return true;
        if (n > CHUNK) { droppedLines++; droppedBytes += (uint32_t)n; return false; }
        if (len[front] + n > CHUNK && !rotate()) {
            droppedLines++; droppedBytes += (uint32_t)n;
            return false;
        }
        memcpy(data[front] + len[front], line, n);
        len[front] += n;
        return true;
    }

    int take() const { return ready; }
    void release(int chunk) { if (chunk == ready) { ready = -1; len[chunk] = 0; } }
    bool empty() const { return ready < 0 && len[front] == 0; }
};

#endif
