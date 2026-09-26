#ifndef SNAP_READER_H
#define SNAP_READER_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Input side of a savestate: a file, or a rewind ring slot in memory.
// The unfreeze code only reads, tells and seeks; the memory case used
// newlib's fmemopen, which allocates a FILE (with its buffer) on every
// rewind step (#78). This reads the slot in place.
struct SnapIn
{
    FILE*          file = nullptr;
    const uint8_t* mem = nullptr;
    uint32_t       len = 0;
    uint32_t       pos = 0;

    static SnapIn fromFile(FILE* f) { SnapIn s; s.file = f; return s; }
    static SnapIn fromMemory(const uint8_t* data, uint32_t length) { SnapIn s; s.mem = data; s.len = length; return s; }

    // bytes actually read (short at the end, like fread)
    size_t read(void* dst, size_t n)
    {
        if (file) return fread(dst, 1, n, file);
        if (mem == nullptr || pos >= len) return 0;
        size_t avail = len - pos;
        if (n > avail) n = avail;
        memcpy(dst, mem + pos, n);
        pos += (uint32_t)n;
        return n;
    }

    long tell() const
    {
        if (file) return ftell(file);
        return (long)pos;
    }

    // 0 on success, like fseek; a memory position past the end is refused
    int seek(long offset, int whence)
    {
        if (file) return fseek(file, offset, whence);
        long target = whence == SEEK_SET ? offset : whence == SEEK_CUR ? (long)pos + offset : (long)len + offset;
        if (target < 0 || target > (long)len) return -1;
        pos = (uint32_t)target;
        return 0;
    }
};

#endif
