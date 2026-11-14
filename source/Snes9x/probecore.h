// Core time probe (docs/perf-backlog.md E0). Compiled in only with
// -DPROBE_CORE (timers and call counters, meant for the console) and
// -DPROBE_CORE_HIST (per-opcode counters, meant for Azahar: counting costs
// time on every opcode, so the timers of a HIST build are not to be trusted).
// Never part of a release build.
#ifndef _PROBECORE_H_
#define _PROBECORE_H_

#if defined(PROBE_CORE_HIST) && !defined(PROBE_CORE)
#define PROBE_CORE
#endif

#ifdef PROBE_CORE
#include <3ds.h>

struct SProbeCore
{
    u64 apuTicks;    // inside S9xUpdateAPUTimer (every caller)
    u64 hbTicks;     // inside S9xDoHBlankProcessing, minus apu, ppu and hdma
    u64 hdmaTicks;   // inside S9xDoHDMA
    u64 dmaTicks;    // inside S9xDoDMA
    u64 ppuTicks;    // mirror of GPU3DSExt.ppuTicks, never reset (for the hb subtraction)
    u32 apuCalls;    // S9xUpdateAPUTimer calls
    u32 slowReads;   // S9xGetByteFromRegister calls
    u32 slowWrites;  // S9xSetByteToRegister calls
    u32 hbCalls;     // S9xDoHBlankProcessing calls
#ifdef PROBE_CORE_HIST
    u32 ops;                 // 65816 opcodes run
    u32 aops;                // SPC700 opcodes run
    u32 cpuHist[5][256];     // by table (0 M1X1, 1 E1, 2 M1X0, 3 M0X0, 4 M0X1) and opcode byte, cumulative
    u32 apuHist[256];        // by opcode byte, cumulative
    // the last BEQ (0xF0) seen: where the hottest loop of a waiting game is
    u32 beqPC;               // bank << 16 | offset of the BEQ
    u8  beqBytes[8];         // code from 4 bytes before the BEQ
    s32 beqWaitDelta;        // CPU.WaitAddress - address of the BEQ (0x7fffffff: no mark)
    u8  beqShutdown;         // Settings.Shutdown at that moment
    u8  beqTaken;            // zero flag set: the branch is taken
    u32 beqFlags;            // CPU.Flags
    u32 beqMax;              // count of the hottest BEQ site so far
    // why CPUShutdown did not skip, cumulative
    u32 sdCalls, sdNoMark, sdFar, sdFlags, sdFlagsOr, sdTaken;
    u32 sdNmi, sdIrqOpen;    // sdFlags split: NMI due, IRQ pending with I clear
    u32 sdIrqMasked;         // skipped with an IRQ pending behind the I flag (part of sdTaken)
    u32 beqHist[4096];       // by PC offset & 0xFFF
    u32 apuPcHist[65536];    // SPC700 opcodes by address, cumulative
#endif
};

extern SProbeCore g_probeCore;

struct SProbeCoreScope
{
    u64 *acc; u64 t0;
    SProbeCoreScope(u64 *a) : acc(a), t0(svcGetSystemTick()) {}
    ~SProbeCoreScope() { *acc += svcGetSystemTick() - t0; }
};

#define PROBE_CORE_SCOPE(field)  SProbeCoreScope probeCoreScope_##field(&g_probeCore.field)
#define PROBE_CORE_COUNT(field)  (g_probeCore.field++)

// writes the "[perf][core]" line(s) for a window of `frames` frames and
// resets the window counters (cpuexec.cpp)
void probeCoreReport(unsigned frames);

#else

#define PROBE_CORE_SCOPE(field)
#define PROBE_CORE_COUNT(field)

#endif

#endif
