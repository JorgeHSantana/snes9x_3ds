#ifndef _3DSSTALL_H
#define _3DSSTALL_H

#include <atomic>
#include <stdint.h>

// Stall watchdog (stall_watch.h): each thread names the step it is in, at
// the cost of two atomic stores; a thread above the main one writes those
// names straight to the card when a frame takes over two seconds to end.

enum { STALL_T_MAIN, STALL_T_MIXER, STALL_T_MSURA, STALL_T_LOGW, STALL_T_COUNT };

extern std::atomic<uint32_t>    g_stallBeat;
extern std::atomic<const char*> g_stallStage[STALL_T_COUNT];
extern std::atomic<uint32_t>    g_stallSteps[STALL_T_COUNT];

void stall3dsInitialize();          // starts the watchdog thread (once)
void stall3dsArm(bool armed);       // armed while frames are expected to end
void stall3dsPath(char *out, unsigned int size);   // the file the reports go to; Send Log carries and deletes it

// the main thread, once a frame
static inline void stall3dsBeat() { g_stallBeat.fetch_add(1, std::memory_order_relaxed); }

// 'what' must be a string literal: only the pointer is kept
static inline void stall3dsStage(int thread, const char *what)
{
    g_stallStage[thread].store(what, std::memory_order_relaxed);
    g_stallSteps[thread].fetch_add(1, std::memory_order_relaxed);
}

#endif
