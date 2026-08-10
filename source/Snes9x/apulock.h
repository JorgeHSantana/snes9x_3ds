#pragma once

// Installable lock hooks serializing the emulation thread's APU/SPC700
// execution against the platform audio-mixing thread (which reads and
// mutates APU/DSP/sound state under the same lock on another core).
//
// Root cause this closes: the mixer thread held snesAccessLock while the
// emulation thread ran SPC700 opcodes and DSP register replay with NO lock
// at all — a cross-core data race that intermittently corrupted the SNES
// CPU<->SPC700 boot handshake of strict games (proven on Mega Man X3:
// mixer-disabled probe booted 5/5, normal build failed 3/3).
//
// Same pattern as msu1's lock hooks: null hooks are safe no-ops (host
// tests, early boot); the platform installs the real lock after the sound
// system is initialized. Install both or neither.

void apulock_set_hooks(void (*lock)(void), void (*unlock)(void));

// Null-safe guards used by the core around SPC700/DSP-touching sections.
void apulock_lock(void);
void apulock_unlock(void);
