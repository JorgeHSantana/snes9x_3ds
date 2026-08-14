#ifndef _3DSSTEREOSIG_H_
#define _3DSSTEREOSIG_H_

#include <stdint.h>

// Pure helpers for the per-scene 3D profile matcher (issue #23).
// Kept free of emulator types so the host test suite can exercise them.

// Signature words are 7 packed register bytes (bit 56+ unused):
//   word1: b0=2105 b1=212C b2=212D b3=2130 b4=2131 b5=2106 b6=420C
//   word2: b0=2101 b1..b6=2107..210C
#define STEREO_SIG_BYTES 0x00FFFFFFFFFFFFFFULL

// Capture mask learning: a register byte participates in the fingerprint
// only if it never changed during the observation window (OR == AND for
// every bit of that byte). Blinking color math, layer toggles etc. get
// masked out automatically.
static inline uint64_t stereoSigCapMask(uint64_t orv, uint64_t andv)
{
    uint64_t mask = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t byteMask = 0xFFULL << (i * 8);
        if ((orv & byteMask) == (andv & byteMask))
            mask |= byteMask;
    }
    return mask & STEREO_SIG_BYTES;
}

// A bind hits when both masked signature words match and, if the bind
// carries a WRAM watch value (>= 0), the current watch byte equals it.
// watch < 0 means "no watch configured/available this frame".
static inline bool stereoSigBindMatches(
    uint64_t sig, uint64_t sig2, int watch,
    uint64_t bSig, uint64_t bMask, uint64_t bSig2, uint64_t bMask2, int bWatchVal)
{
    if (((sig ^ bSig) & bMask) != 0)
        return false;
    if (((sig2 ^ bSig2) & bMask2) != 0)
        return false;
    if (bWatchVal >= 0 && watch != bWatchVal)
        return false;
    return true;
}

#endif
