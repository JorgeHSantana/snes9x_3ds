#ifndef BUFFERED_LOG_H
#define BUFFERED_LOG_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "log_spool.h"

// The session log's sink. Producers format a line and append it to the
// spool (no I/O); the SD is touched only by drain(), which the .cpp runs
// on a writer thread, and by flush()/close() at the moments a crash
// report needs the tail (menu, exit, unload). Caller serializes calls
// on the spool; drain() writes the taken chunk outside that serialization.
class BufferedLog {
public:
    static constexpr size_t LINE_CAPACITY = 640;
    static constexpr uint64_t FLUSH_INTERVAL_MS = 1000;
    BufferedLog() { spool_.reset(); }
    BufferedLog(const BufferedLog&) = delete;
    BufferedLog& operator=(const BufferedLog&) = delete;
    ~BufferedLog() { (void)close(); }

    bool open(const char* path, uint64_t now_ms)
    {
        if (file_ != nullptr || path == nullptr || path[0] == '\0') {
            return false;
        }
        file_ = fopen(path, "w");
        if (file_ == nullptr) {
            return false;
        }
        setvbuf(file_, nullptr, _IONBF, 0);   // chunks go straight to the file
        failed_ = false;
        spool_.reset();
        reportedDrops_ = 0;
        last_rotate_ms_ = now_ms;
        return true;
    }

    // one line (the caller adds the newline); a line longer than
    // LINE_CAPACITY-1 is cut. Dropped lines (writer behind) are counted and
    // announced by the next line that fits.
    bool write_v(const char* format, va_list args)
    {
        if (file_ == nullptr || failed_ || format == nullptr) {
            return false;
        }
        char line[LINE_CAPACITY];
        int n = vsnprintf(line, sizeof(line), format, args);
        if (n < 0) return false;
        if ((size_t)n >= sizeof(line)) n = (int)sizeof(line) - 1;
        if (spool_.droppedLines != reportedDrops_) {
            char note[96];
            int m = snprintf(note, sizeof(note), "[log] %u lines dropped (SD writer behind)\n",
                (unsigned)(spool_.droppedLines - reportedDrops_));
            // the notice itself is not a dropped line: its failure is not counted
            const uint32_t dl = spool_.droppedLines, db = spool_.droppedBytes;
            if (m > 0 && spool_.append(note, (size_t)m)) reportedDrops_ = spool_.droppedLines;
            else { spool_.droppedLines = dl; spool_.droppedBytes = db; }
        }
        spool_.append(line, (size_t)n);
        return true;
    }

    bool write(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        const bool result = write_v(format, args);
        va_end(args);
        return result;
    }

    // once a second: hand the front chunk to the writer (no I/O). Returns
    // whether there is now a chunk to drain.
    bool tick(uint64_t now_ms)
    {
        if (file_ == nullptr || failed_) {
            return false;
        }
        if (now_ms < last_rotate_ms_) {
            last_rotate_ms_ = now_ms;
        }
        if (spool_.take() >= 0) return true;
        if (now_ms - last_rotate_ms_ < FLUSH_INTERVAL_MS) return false;
        last_rotate_ms_ = now_ms;
        return spool_.rotate();
    }

    bool has_ready() const { return spool_.take() >= 0; }

    // The writer, in three steps so the SD access runs outside the
    // producers' lock: take the ready chunk (serialized), write it (the
    // chunk is the writer's alone), release it (serialized). drain() is
    // the three in a row, for the synchronous paths and the tests.
    int drain_take()
    {
        if (file_ == nullptr || failed_ || inFlight_) return -1;
        const int c = spool_.take();
        if (c >= 0) inFlight_ = true;
        return c;
    }
    bool drain_write(int c) const
    {
        if (c < 0 || file_ == nullptr) return false;
        const size_t n = spool_.len[c];
        return n == 0 || (fwrite(spool_.data[c], 1, n, file_) == n && fflush(file_) == 0);
    }
    void drain_release(int c, bool ok)
    {
        if (c < 0) return;
        spool_.release(c);
        inFlight_ = false;
        if (!ok) failed_ = true;
    }
    bool drain()
    {
        if (file_ == nullptr || failed_) {
            return false;
        }
        if (inFlight_) return true;        // the writer thread has it
        const int c = drain_take();
        if (c < 0) return true;
        const bool ok = drain_write(c);
        drain_release(c, ok);
        return ok;
    }

    // everything to the file now (safe moments only: menu, exit, unload)
    bool flush(uint64_t now_ms)
    {
        if (file_ == nullptr || failed_) {
            return false;
        }
        last_rotate_ms_ = now_ms;
        if (!drain()) return false;
        if (!spool_.rotate()) return true;
        return drain();
    }

    bool close()
    {
        if (file_ == nullptr) {
            return !failed_;
        }
        const bool flushed = flush(last_rotate_ms_);
        const bool closed = fclose(file_) == 0;
        file_ = nullptr;
        failed_ = failed_ || !flushed || !closed;
        return !failed_;
    }

    uint32_t dropped_lines() const { return spool_.droppedLines; }

private:
    FILE* file_ = nullptr;
    LogSpool spool_;
    uint64_t last_rotate_ms_ = 0;
    uint32_t reportedDrops_ = 0;
    bool failed_ = false;
    bool inFlight_ = false;
};

#endif
