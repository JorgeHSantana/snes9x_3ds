#include "copyright.h"


#ifndef _apu_h_
#define _apu_h_

// SPC700 idle skip (the 1.43 SPC700_SHUTDOWN): a read of the CPU ports
// $F4-$F7 or the timer outputs $FD-$FF records the reading instruction
// (WaitAddress1/2); a branch back onto it with nothing else going on
// (WaitCounter == 0: no INC/DEC in the loop, no timer tick or port write
// since the last pass) jumps APU.Cycles to the end of the current run,
// which is the next timer tick or the CPU's current cycle - exactly where
// the polled value can change. Shares Settings.Shutdown with the CPU skip.
#define SPC700_SHUTDOWN
extern int32 g_apuNextEvent;   // S9xUpdateAPUTimer's current run limit
extern uint32 g_apuSkips;      // skips taken, perf line aidle=

// SPC700 sleep on a proven fixed point: see apu_sleep.h for the proof. While
// asleep APU.Cycles stays at the anchor (it is still rebased every line with
// the rest) and S9xUpdateAPUTimer only steps the timers; a timer step that
// would change $FD-$FF, or a CPU write to $2140-$2143, wakes it first.
// Kept out of SAPU/SIAPU so the snapshot format does not change: a save
// moves the counter forward (S9xAPUSleepSync), a load starts over.
#include "apu_sleep.h"

enum { APU_SLEEP_AWAKE = 0, APU_SLEEP_ASLEEP = 1, APU_SLEEP_ENTERING = 2 };

struct SApuSleep
{
    SApuSleepDetector det;
    int32  period;     // cycles of one pass
    int32  cycles;     // ENTERING: the real APU.Cycles, while the opcode loop is being left
    uint8  state;
    uint8  inhibit;    // run to the limit without going back to sleep
};

extern SApuSleep g_apuSleep;
extern uint32 g_apuSleeps;     // sleeps entered, perf line asleep=

// a write of `now` over `old`: only a change can make the next pass differ
#define APU_SLEEP_NOTE_WRITE(old, now) (g_apuSleep.det.dirty |= (uint8) ((old) ^ (now)))
#define APU_SLEEP_NOTE_CHANGE() (g_apuSleep.det.dirty = 1)

#include "spc700.h"


#define DSPWRITEBUFFERSIZE 1024

struct SIAPU
{
    uint8  *PC;
    uint8  *RAM;
    uint8  *DirectPage;
    bool8  APUExecuting;
    uint8  Bit;
    //uint32 Address;
    uint8  *WaitAddress1;
    uint8  *WaitAddress2;
    uint32 WaitCounter;
    uint8  *ShadowRAM;
    uint8  *CachedSamples;
    uint8  _Carry;
    uint8  _Zero;
    uint8  _Overflow;
    uint32 TimerErrorCounter;
    int32  NextAPUTimerPos;
    int32  APUTimerCounter;
    uint32 Scanline;
    int32  OneCycle;
    int32  TwoCycles;

    int32  NextAPUTimerPosDiv10000;

    int32  DSPWriteIndex;
    int32  DSPReplayIndex;
    struct {
        uint8   reg;
        uint8   byte;
    } DSPWriteBuffer[DSPWRITEBUFFERSIZE];
    uint8  DSPCopy[0x80];
};

struct SAPU
{
    int32  OldCycles; // unused
    bool8  ShowROM;
    uint32 Flags;
    uint8  KeyedChannels;
    uint8  OutPorts [4];
    uint8  DSP [0x80];
    uint8  ExtraRAM [64];
    uint16 Timer [3];
    uint16 TimerTarget [3];
    bool8  TimerEnabled [3];
    bool8  TimerValueWritten [3];
	int32  Cycles;

    struct SIAPU FastIAPU;
    struct SAPURegisters FastRegisters;

    // Optimized
    //
    int32 S9xAPUCycles [256] =
    {
        /*        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b, c, d, e, f, */
        /* 00 */  2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 5, 4, 5, 4, 6, 8, 
        /* 10 */  2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 6, 5, 2, 2, 4, 6, 
        /* 20 */  2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 5, 4, 5, 4, 5, 4, 
        /* 30 */  2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 6, 5, 2, 2, 3, 8, 
        /* 40 */  2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 4, 4, 5, 4, 6, 6, 
        /* 50 */  2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 4, 5, 2, 2, 4, 3, 
        /* 60 */  2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 4, 4, 5, 4, 5, 5, 
        /* 70 */  2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 5, 5, 2, 2, 3, 6, 
        /* 80 */  2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 5, 4, 5, 2, 4, 5, 
        /* 90 */  2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 5, 5, 2, 2,12, 5, 
        /* a0 */  3, 8, 4, 5, 3, 4, 3, 6, 2, 6, 4, 4, 5, 2, 4, 4, 
        /* b0 */  2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 5, 5, 2, 2, 3, 4, 
        /* c0 */  3, 8, 4, 5, 4, 5, 4, 7, 2, 5, 6, 4, 5, 2, 4, 9, 
        /* d0 */  2, 8, 4, 5, 5, 6, 6, 7, 4, 5, 4, 5, 2, 2, 6, 3, 
        /* e0 */  2, 8, 4, 5, 3, 4, 3, 6, 2, 4, 5, 3, 4, 3, 4, 3, 
        /* f0 */  2, 8, 4, 5, 4, 5, 5, 6, 3, 4, 5, 4, 2, 2, 4, 3
    };    
};

EXTERN_C struct SAPU APU;
//EXTERN_C struct SIAPU IAPU;
#define IAPU  APU.FastIAPU

extern int spc_is_dumping;
extern int spc_is_dumping_temp;
extern uint8 spc_dump_dsp[0x100];
STATIC inline void S9xAPUUnpackStatus()
{
    IAPU._Zero = ((APURegisters.P & Zero) == 0) | (APURegisters.P & Negative);
    IAPU._Carry = (APURegisters.P & Carry);
    IAPU._Overflow = (APURegisters.P & Overflow) >> 6;
}

STATIC inline void S9xAPUPackStatus()
{
    APURegisters.P &= ~(Zero | Negative | Carry | Overflow);
    APURegisters.P |= IAPU._Carry | ((IAPU._Zero == 0) << 1) |
		      (IAPU._Zero & 0x80) | (IAPU._Overflow << 6);
}

START_EXTERN_C
void S9xResetAPU (void);
bool8 S9xInitAPU ();
void S9xDeinitAPU ();
void S9xDecacheSamples ();
int S9xTraceAPU ();
int S9xAPUOPrint (char *buffer, uint16 Address);
void S9xSetAPUControl (uint8 byte);
void S9xSetAPUDSP (uint8 byte, uint8 reg);
void S9xSetAPUDSPLater (uint8 byte);
void S9xSetAPUDSPReplay ();
uint8 S9xGetAPUDSP ();
void S9xSetAPUTimer (uint16 Address, uint8 byte);
void S9xUpdateAPUTimer (void);
void S9xAPUSleepReset (void);       // reset, snapshot load
void S9xAPUSleepSync (void);        // before a snapshot save: counter up to the CPU's
void S9xAPUSleepPortWrite (void);   // CPU write to $2140-$2143, after S9xUpdateAPUTimer and before the byte lands
bool8 S9xInitSound (int quality, bool8 stereo, int buffer_size);
void S9xOpenCloseSoundTracingFile (bool8);
void S9xPrintAPUState ();
//extern int32 S9xAPUCycles [256];	// Scaled cycle lengths
extern int32 S9xAPUCycleLengths [256];	// Raw data.
extern void (*S9xApuOpcodes [256]) (void);
END_EXTERN_C


#define APU_VOL_LEFT 0x00
#define APU_VOL_RIGHT 0x01
#define APU_P_LOW 0x02
#define APU_P_HIGH 0x03
#define APU_SRCN 0x04
#define APU_ADSR1 0x05
#define APU_ADSR2 0x06
#define APU_GAIN 0x07
#define APU_ENVX 0x08
#define APU_OUTX 0x09

#define APU_MVOL_LEFT 0x0c
#define APU_MVOL_RIGHT 0x1c
#define APU_EVOL_LEFT 0x2c
#define APU_EVOL_RIGHT 0x3c
#define APU_KON 0x4c
#define APU_KOFF 0x5c
#define APU_FLG 0x6c
#define APU_ENDX 0x7c

#define APU_EFB 0x0d
#define APU_PMON 0x2d
#define APU_NON 0x3d
#define APU_EON 0x4d
#define APU_DIR 0x5d
#define APU_ESA 0x6d
#define APU_EDL 0x7d

#define APU_C0 0x0f
#define APU_C1 0x1f
#define APU_C2 0x2f
#define APU_C3 0x3f
#define APU_C4 0x4f
#define APU_C5 0x5f
#define APU_C6 0x6f
#define APU_C7 0x7f

#define APU_SOFT_RESET 0x80
#define APU_MUTE 0x40
#define APU_ECHO_DISABLED 0x20

#define FREQUENCY_MASK 0x3fff
#endif

