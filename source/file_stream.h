#ifndef FILE_STREAM_H
#define FILE_STREAM_H

#include <stdint.h>
#include <stdio.h>

#define CACHE_LINE_SIZE 32
#define MAX_IO_BUFFER_SIZE (512 * 256 * 4)

// Owned by file3dsInitialize/Finalize. Main-thread file/UI operations share
// these buffers; background readers must not use this transport wrapper.
extern uint8_t* g_fileBuffer;
extern uint8_t g_streamBuffer[CACHE_LINE_SIZE * 1024];
extern FILE* g_streamBufferOwner;

inline FILE* file3dsOpen(const char* filename, const char* mode)
{
    if (filename == nullptr || mode == nullptr) {
        return nullptr;
    }
    FILE* fp = fopen(filename, mode);
    if (fp != nullptr && g_streamBufferOwner == nullptr) {
        if (setvbuf(fp, reinterpret_cast<char*>(g_streamBuffer), _IOFBF, sizeof(g_streamBuffer)) != 0) {
            (void)fclose(fp); // No data has been written.
            return nullptr;
        }
        g_streamBufferOwner = fp;
    }
    return fp;
}

inline int file3dsClose(FILE* fp)
{
    if (fp == nullptr) {
        return 0;
    }
    if (g_streamBufferOwner == fp) {
        g_streamBufferOwner = nullptr;
    }
    return fclose(fp);
}

#endif
