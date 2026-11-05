#ifndef IDLE_SKIP_H
#define IDLE_SKIP_H

// Idle-loop skip (the Snes9x 1.43 "CPU_SHUTDOWN", re-done without a
// per-opcode PC store). A read of $4210/$4212, of the APU ports $2140-$2143,
// of the SuperFX status $3030 or of plain RAM records the END of the reading
// instruction (the PC after its operand) as the wait mark. A taken backward
// branch whose target lies within `IDLE_SKIP_WINDOW` bytes before the mark
// has the read as the loop's first instruction (LDA/BIT abs = 3 bytes, long
// = 4), i.e. a loop that only polls and can jump straight to the next event.
// Every write and every register-changing op (DEC/INC/...) clears the mark,
// so a countdown or a loop with side effects never qualifies.
static const int IDLE_SKIP_WINDOW = 4;

// `target`: the branch's destination; `wait`: the recorded mark (0 = none)
static inline bool idle_skip_hit(unsigned long target, unsigned long wait)
{
    return wait != 0 && target < wait && (wait - target) <= (unsigned long)IDLE_SKIP_WINDOW;
}

#endif
