// tests/fixtures.h
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <unistd.h>

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

// creates <dir>/<name> with given bytes; returns full path ("" on failure)
inline std::string put_file(const std::string& dir, const char* name,
                            const char* bytes, size_t len)
{
    std::string path = dir + "/" + name;
    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr) { return std::string(); }
    if (len > 0) { fwrite(bytes, 1, len, f); }
    fclose(f);
    return path;
}

inline std::string make_tmpdir()
{
    char tmpl[] = "/tmp/msu1_test_XXXXXX";
    char* d = mkdtemp(tmpl);
    return (d != nullptr) ? std::string(d) : std::string();
}

// Write a valid PCM file to a specific path with given loop_point and sample_count.
// Returns true on success, false if the file could not be created.
inline bool write_pcm_at(const char* path, uint32_t loop_point, uint32_t sample_count)
{
    FILE* f = fopen(path, "wb");
    if (f == nullptr) { return false; }
    fwrite("MSU1", 1, 4, f);
    fwrite(&loop_point, 4, 1, f);
    for (uint32_t i = 0; i < sample_count; i++) {
        int16_t l = (int16_t)i, r = (int16_t)-(int32_t)i;
        fwrite(&l, 2, 1, f); fwrite(&r, 2, 1, f);
    }
    fclose(f);
    return true;
}

} // namespace fixtures
