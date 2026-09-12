#ifndef SNAPSHOT_WORKSPACE_H
#define SNAPSHOT_WORKSPACE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// A single core-owned scratch block, reused by sequential field serializers.
// Zeroing the entire requested span preserves the legacy format's padding.
template <size_t Capacity>
class SnapshotWorkspace {
    uint8_t bytes_[Capacity];
public:
    uint8_t* prepare(size_t size)
    {
        if (size == 0 || size > Capacity) return nullptr;
        memset(bytes_, 0, size);
        return bytes_;
    }
};

#endif
