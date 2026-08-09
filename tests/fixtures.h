// tests/fixtures.h
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace fixtures {

// Raw bytes in a seekable temp FILE*, rewound to offset 0.
inline FILE* make_raw_file(const char* bytes, size_t len)
{
    FILE* f = tmpfile();
    if (f == nullptr) { return nullptr; }
    fwrite(bytes, 1, len, f);
    rewind(f);
    return f;
}

// Valid PCM file: "MSU1" + loop_point (u32-LE) + sample_count stereo frames.
// Sample data pattern: frame i = { L=i, R=-(int)i } so tests can verify content.
inline FILE* make_pcm_file(uint32_t loop_point, uint32_t sample_count)
{
    FILE* f = tmpfile();
    if (f == nullptr) { return nullptr; }
    fwrite("MSU1", 1, 4, f);
    fwrite(&loop_point, 4, 1, f);   // host is little-endian (x86/ARM): matches format
    for (uint32_t i = 0; i < sample_count; i++) {
        int16_t l = (int16_t)i;
        int16_t r = (int16_t)-(int32_t)i;
        fwrite(&l, 2, 1, f);
        fwrite(&r, 2, 1, f);
    }
    rewind(f);
    return f;
}

} // namespace fixtures
