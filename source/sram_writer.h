#ifndef SRAM_WRITER_H
#define SRAM_WRITER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sram_save.h"

// The SRAM autosave off the emulation thread (issue #59: 225-259 ms of
// frozen game per periodic save on the Old 3DS, all of it the SD/FAT
// write). The emulation thread copies the SRAM into this queue's own
// buffer - a memcpy of at most 128 KiB - and a low-priority worker does
// the file write from the copy. Pure state, no threads here: the .cpp
// wraps it in a lock and an event; the tests drive it directly.
//
// One job at a time. A request while a job is in flight is refused
// (`busy`) and the caller keeps the SRAM marked dirty, so the autosave
// timer simply tries again later - no second buffer, no queue growth.
// A failed write reports back through poll(), which re-marks the SRAM
// dirty for the same retry path.

#define SRAM_WRITER_PATH_MAX 512

struct SramWriterQueue
{
    enum State : uint8_t { IDLE, QUEUED, WRITING, DONE };

    uint8_t  data[SRAM_SAVE_MAX_SIZE];
    char     path[SRAM_WRITER_PATH_MAX];
    size_t   size;
    State    state;
    bool     lastOk;
    uint32_t jobs;       // requests accepted (diagnostics)
    uint32_t refused;    // requests refused while busy (diagnostics)

    void reset()
    {
        size = 0; state = IDLE; lastOk = true; jobs = 0; refused = 0;
        path[0] = '\0';
    }

    bool busy() const { return state == QUEUED || state == WRITING; }

    // emulation thread: copy the SRAM and queue the write
    bool request(const char* filePath, const uint8_t* sram, size_t bytes)
    {
        if (filePath == nullptr || filePath[0] == '\0' || sram == nullptr ||
            bytes == 0 || bytes > SRAM_SAVE_MAX_SIZE)
            return false;
        if (busy() || state == DONE) {   // DONE: poll() has not consumed the result yet
            if (refused != UINT32_MAX) refused++;
            return false;
        }
        size_t n = strlen(filePath);
        if (n >= sizeof(path)) return false;
        memcpy(path, filePath, n + 1);
        memcpy(data, sram, bytes);
        size = bytes;
        state = QUEUED;
        if (jobs != UINT32_MAX) jobs++;
        return true;
    }

    // worker: claim the queued job (false = nothing to do)
    bool take()
    {
        if (state != QUEUED) return false;
        state = WRITING;
        return true;
    }

    // worker: the write finished
    void complete(bool ok)
    {
        if (state != WRITING) return;
        lastOk = ok;
        state = DONE;
    }

    // emulation thread, once per frame: consume a finished job. Returns
    // true when one was consumed; `dirty` is re-raised on failure so the
    // autosave timer retries (the SRAM data is still in the core).
    bool poll(uint8_t& dirty)
    {
        if (state != DONE) return false;
        if (!lastOk) dirty = 1;
        state = IDLE;
        return true;
    }
};

#endif
