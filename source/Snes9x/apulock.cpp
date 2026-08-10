#include "apulock.h"
#include <cstddef>

// Written once by the platform at init (before the mixing thread can
// contend), read by the emulation thread every scanline. Both-or-neither
// install keeps lock/unlock balanced by construction.
static void (*g_apu_lock)(void)   = nullptr;
static void (*g_apu_unlock)(void) = nullptr;

void apulock_set_hooks(void (*lock)(void), void (*unlock)(void))
{
    if ((lock == nullptr) != (unlock == nullptr)) {
        return;   // refuse unbalanced installs
    }
    g_apu_lock = lock;
    g_apu_unlock = unlock;
}

void apulock_lock(void)
{
    if (g_apu_lock != nullptr) {
        g_apu_lock();
    }
}

void apulock_unlock(void)
{
    if (g_apu_unlock != nullptr) {
        g_apu_unlock();
    }
}
