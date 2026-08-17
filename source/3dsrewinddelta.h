#ifndef _3DSREWINDDELTA_H
#define _3DSREWINDDELTA_H

#include <stdint.h>
#include <string.h>

// Page deltas against a keyframe (issue #37, spec section B budget): a
// snapshot is stored as the set of pages that differ from its keyframe,
// so a ~450KB state whose frame-to-frame churn is small costs tens of KB.
// No chains - every delta depends only on its keyframe (restore = copy
// keyframe, overwrite dirty pages), so a corrupted slot can never poison
// its neighbours. Pure functions over caller buffers, host-tested.
//
// Layout: [u32 stateLength][u32 pageCount][pageCount x (u32 pageIndex +
// pageSize bytes)]. The last page may be short (stateLength remainder).

namespace RewindDelta
{

inline uint32_t pageBytes(uint32_t stateLength, uint32_t pageSize, uint32_t pageIndex)
{
    uint32_t start = pageIndex * pageSize;
    uint32_t left = stateLength - start;
    return left < pageSize ? left : pageSize;
}

// returns bytes written into out, or 0 when the delta would not fit in
// outCapacity or the lengths differ (caller stores the full state instead)
inline uint32_t encode(const uint8_t *keyframe, uint32_t keyframeLength,
                       const uint8_t *state, uint32_t stateLength,
                       uint32_t pageSize,
                       uint8_t *out, uint32_t outCapacity)
{
    if (keyframe == nullptr || state == nullptr || out == nullptr) return 0;
    if (pageSize == 0 || stateLength == 0) return 0;
    if (keyframeLength != stateLength) return 0;   // no partial-length deltas
    if (outCapacity < 8) return 0;

    uint32_t pages = (stateLength + pageSize - 1) / pageSize;
    uint32_t used = 8;
    uint32_t count = 0;

    for (uint32_t p = 0; p < pages; p++) {
        uint32_t start = p * pageSize;
        uint32_t bytes = pageBytes(stateLength, pageSize, p);
        if (memcmp(keyframe + start, state + start, bytes) == 0) continue;
        if (used + 4 + bytes > outCapacity) return 0;
        memcpy(out + used, &p, 4);
        memcpy(out + used + 4, state + start, bytes);
        used += 4 + bytes;
        count++;
    }

    memcpy(out, &stateLength, 4);
    memcpy(out + 4, &count, 4);
    return used;
}

// rebuilds the state into out (capacity >= the encoded stateLength);
// returns the state length, or 0 on any inconsistency
inline uint32_t decode(const uint8_t *keyframe, uint32_t keyframeLength,
                       const uint8_t *delta, uint32_t deltaLength,
                       uint32_t pageSize,
                       uint8_t *out, uint32_t outCapacity)
{
    if (keyframe == nullptr || delta == nullptr || out == nullptr) return 0;
    if (pageSize == 0 || deltaLength < 8) return 0;

    uint32_t stateLength = 0, count = 0;
    memcpy(&stateLength, delta, 4);
    memcpy(&count, delta + 4, 4);
    if (stateLength == 0 || stateLength != keyframeLength) return 0;
    if (outCapacity < stateLength) return 0;

    memcpy(out, keyframe, stateLength);

    uint32_t pages = (stateLength + pageSize - 1) / pageSize;
    uint32_t used = 8;
    for (uint32_t i = 0; i < count; i++) {
        if (used + 4 > deltaLength) return 0;
        uint32_t p = 0;
        memcpy(&p, delta + used, 4);
        if (p >= pages) return 0;
        uint32_t bytes = pageBytes(stateLength, pageSize, p);
        if (used + 4 + bytes > deltaLength) return 0;
        memcpy(out + p * pageSize, delta + used + 4, bytes);
        used += 4 + bytes;
    }
    return stateLength;
}

} // namespace RewindDelta

#endif
