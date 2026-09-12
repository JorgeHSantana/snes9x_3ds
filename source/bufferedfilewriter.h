#ifndef _BUFFERED_FILE_WRITER_H_
#define _BUFFERED_FILE_WRITER_H_

#include <stdio.h>
#include <string.h>
#include "file_stream.h"

class BufferedFileWriter {
    FILE* RawFilePointer;
    size_t Position;
    bool WriteFailed;

    // memory mode (rewind snapshots): write straight into a caller buffer,
    // no FILE* and no g_fileBuffer involved
    uint8_t* MemBuffer;
    size_t MemCapacity;
    bool MemOverflow;

public:
    BufferedFileWriter() : RawFilePointer(NULL), Position(0), WriteFailed(false),
        MemBuffer(NULL), MemCapacity(0), MemOverflow(false) {
    }

    // safety: prevent copying
    BufferedFileWriter(const BufferedFileWriter&) = delete;
    BufferedFileWriter& operator=(const BufferedFileWriter&) = delete;

    ~BufferedFileWriter() {
        close();
    }
    
    explicit operator bool() const { 
        return RawFilePointer != NULL || MemBuffer != NULL; 
    }

    bool openMem(void* buffer, size_t capacity) {
        if (RawFilePointer || MemBuffer || !buffer || capacity == 0) return false;
        MemBuffer = (uint8_t*)buffer;
        MemCapacity = capacity;
        MemOverflow = false;
        WriteFailed = false;
        Position = 0;
        return true;
    }

    size_t memLength() const { return Position; }
    bool memOverflowed() const { return MemOverflow; }
    void fail() { WriteFailed = true; if (MemBuffer) MemOverflow = true; }

    FILE* get() const { 
        return RawFilePointer; 
    }

    bool open(const char* filename, const char* mode) {
        if (RawFilePointer || MemBuffer || !filename || !mode) return false;
        // Early startup reads settings before the linear write buffer exists.
        // Only writable modes require it; validate before fopen can truncate.
        const bool writable = strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+');
        if (writable && !g_fileBuffer) return false;
        RawFilePointer = file3dsOpen(filename, mode);
        if (!RawFilePointer) return false;
        WriteFailed = false;
        Position = 0;

        return true;
    }

    // returns bytes written
    size_t write(const void* ptr, size_t count) {
        if (WriteFailed) return 0;
        if (count == 0) return 0;
        if (!ptr) { WriteFailed = true; MemOverflow = true; return 0; }
        if (MemBuffer) {
            if (count > MemCapacity - Position) {
                MemOverflow = true;
                WriteFailed = true;
                return 0;
            }
            memcpy(MemBuffer + Position, ptr, count);
            Position += count;
            return count;
        }
        if (!RawFilePointer || !g_fileBuffer) { WriteFailed = true; return 0; }

        uint8_t* buffer = g_fileBuffer;

        // data fits in buffer (expected scenario)
        if (count <= MAX_IO_BUFFER_SIZE - Position) {
            memcpy(buffer + Position, ptr, count);
            Position += count;
            return count;
        } 

        // buffer overflow: Flush current data (rare scencario)
        if (!flushBuffer()) {
            return 0;
        }

        // handle new data
        // write directly to disk to bypass the copy if it's huge
        if (count > MAX_IO_BUFFER_SIZE) {
            size_t written = fwrite(ptr, 1, count, RawFilePointer);
            WriteFailed = written != count;
            return written;
        }
            
        // otherwise buffer it
        memcpy(buffer, ptr, count);
        Position = count;      
        return count;
    }

    int flush() {
        if (RawFilePointer) {
            if (!flushBuffer()) return EOF;
            if (fflush(RawFilePointer) != 0) WriteFailed = true;
            return WriteFailed ? EOF : 0;
        }
        return EOF;
    }

    int close() {
        if (MemBuffer) {
            MemBuffer = NULL;
            MemCapacity = 0;
            Position = 0;
            return WriteFailed ? EOF : 0;
        }
        if (RawFilePointer) {
            bool flushed = flushBuffer();
            int rv = file3dsClose(RawFilePointer);
            
            RawFilePointer = NULL;
            Position = 0;
            return flushed && rv == 0 && !WriteFailed ? 0 : EOF;
        }
        return 0; // closing a closed file is technically a success
    }

private:
    bool flushBuffer() {
        if (WriteFailed) return false;
        // trust the caller: RawFilePointer is valid here
        if (Position > 0) {
            // write directly from the global linear heap buffer
            size_t written = fwrite(g_fileBuffer, 1, Position, RawFilePointer);
            bool success = (written == Position);
            WriteFailed = !success;
            Position = 0;
            return success;
        }
        return true; // nothing to flush
    }
};

#endif // _BUFFERED_FILE_WRITER_H_
