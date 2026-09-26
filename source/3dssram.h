#ifndef _3DSSRAM_H_
#define _3DSSRAM_H_

#include <stddef.h>
#include <stdint.h>

// Asynchronous SRAM writer (issue #59): see sram_writer.h for the model.
// All entry points are for the emulation/main thread except the worker.

void sram3dsInitialize();
void sram3dsFinalize();          // drains, stops and joins the worker

// copies `size` bytes of SRAM and queues the file write; false when a
// write is still in flight (the caller keeps the SRAM dirty and retries)
bool sram3dsRequestAsync(const char* path, const uint8_t* sram, size_t size);

// once per emulated frame: consumes a finished job (a failure re-marks
// the SRAM dirty) and logs the result
void sram3dsPoll();

// waits for an in-flight write to finish (before a synchronous save, a
// ROM unload, sleep and exit)
void sram3dsDrain();

bool sram3dsBusy();

#endif
