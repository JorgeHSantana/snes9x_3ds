#ifndef BUFFERED_LOG_H
#define BUFFERED_LOG_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Caller serializes all operations. Storage is never shared with the main
// thread's file transport: background diagnostic producers use this too.
class BufferedLog {
public:
    static constexpr size_t BUFFER_CAPACITY = 32 * 1024;
    static constexpr uint64_t FLUSH_INTERVAL_MS = 1000;
    BufferedLog() = default;
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
        if (setvbuf(file_, buffer_, _IOFBF, sizeof(buffer_)) != 0) {
            (void)fclose(file_); // No data written.
            file_ = nullptr;
            return false;
        }
        failed_ = false;
        pending_ = false;
        last_flush_ms_ = now_ms;
        return true;
    }

    bool write_v(const char* format, va_list args)
    {
        if (file_ == nullptr || failed_ || format == nullptr) {
            return false;
        }
        if (vfprintf(file_, format, args) < 0) {
            failed_ = true;
            return false;
        }
        pending_ = true;
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

    bool tick(uint64_t now_ms)
    {
        if (file_ == nullptr || failed_) {
            return false;
        }
        if (now_ms < last_flush_ms_) {
            last_flush_ms_ = now_ms;
        }
        if (!pending_ || now_ms - last_flush_ms_ < FLUSH_INTERVAL_MS) {
            return true;
        }
        if (fflush(file_) != 0) {
            failed_ = true;
            return false;
        }
        pending_ = false;
        last_flush_ms_ = now_ms;
        return true;
    }

    bool close()
    {
        if (file_ == nullptr) {
            return !failed_;
        }
        const bool flushed = fflush(file_) == 0;
        const bool closed = fclose(file_) == 0;
        file_ = nullptr;
        failed_ = failed_ || !flushed || !closed;
        pending_ = false;
        return !failed_;
    }

private:
    FILE* file_ = nullptr;
    char buffer_[BUFFER_CAPACITY];
    uint64_t last_flush_ms_ = 0;
    bool pending_ = false;
    bool failed_ = false;
};

#endif
