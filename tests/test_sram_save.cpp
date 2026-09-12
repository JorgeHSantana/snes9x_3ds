#include "doctest.h"
#include "bufferedfilewriter.h"
#include "sram_save.h"
#include <limits>
#include <initializer_list>
#include <stdlib.h>
#include <unistd.h>

static uint8_t test_file_buffer[MAX_IO_BUFFER_SIZE];
uint8_t* g_fileBuffer = test_file_buffer;
uint8_t g_streamBuffer[32 * 1024];
FILE* g_streamBufferOwner = nullptr;

struct FakeSramWriter {
    bool open_ok = true;
    bool write_ok = true;
    int close_result = 0;
    uint32_t opens = 0;
    uint32_t closes = 0;
    bool open(const char*, const char*) { ++opens; return open_ok; }
    size_t write(const void*, size_t count) { return write_ok ? count : count - 1; }
    int close() { ++closes; return close_result; }
};

TEST_CASE("SRAM success requires complete write and successful close") {
    const uint8_t data[4] = {1, 2, 3, 4};
    FakeSramWriter writer;
    SUBCASE("success") { CHECK(write_sram_file(writer, "save.srm", data, sizeof(data))); }
    SUBCASE("short write") { writer.write_ok = false; CHECK_FALSE(write_sram_file(writer, "save.srm", data, sizeof(data))); }
    SUBCASE("close failure") { writer.close_result = EOF; CHECK_FALSE(write_sram_file(writer, "save.srm", data, sizeof(data))); }
    CHECK(writer.opens == 1);
    CHECK(writer.closes == 1);
}

TEST_CASE("SRAM invalid input and open failure never close an unopened writer") {
    FakeSramWriter writer;
    const uint8_t data = 0;
    CHECK_FALSE(write_sram_file(writer, nullptr, &data, 1));
    CHECK_FALSE(write_sram_file(writer, "", &data, 1));
    CHECK_FALSE(write_sram_file(writer, "save.srm", nullptr, 1));
    CHECK_FALSE(write_sram_file(writer, "save.srm", &data, 0));
    CHECK_FALSE(write_sram_file(writer, "save.srm", &data, 0x20001));
    CHECK(writer.opens == 0);
    writer.open_ok = false;
    CHECK_FALSE(write_sram_file(writer, "save.srm", &data, 1));
    CHECK(writer.opens == 1);
    CHECK(writer.closes == 0);
}

TEST_CASE("autosave retries failure and restores previous silence state") {
    for (bool initial_silence : {false, true}) {
        uint8_t dirty = 1;
        std::atomic<bool> silence{initial_silence};
        CHECK_FALSE(attempt_sram_save(dirty, silence, [&]() {
            CHECK(silence.load());
            return false;
        }));
        CHECK(dirty == 1);
        CHECK(silence.load() == initial_silence);
        CHECK(attempt_sram_save(dirty, silence, [&]() {
            CHECK(silence.load());
            return true;
        }));
        CHECK(dirty == 0);
        CHECK(silence.load() == initial_silence);
    }
}

TEST_CASE("memory writer rejects overflow without arithmetic wrap and retains failure") {
    BufferedFileWriter writer;
    uint8_t buffer[8] = {};
    const uint8_t data[4] = {1, 2, 3, 4};
    CHECK_FALSE(writer.openMem(nullptr, sizeof(buffer)));
    REQUIRE(writer.openMem(buffer, sizeof(buffer)));
    CHECK_FALSE(writer.openMem(buffer, sizeof(buffer)));
    CHECK(writer.write(data, sizeof(data)) == sizeof(data));
    CHECK(writer.write(data, std::numeric_limits<size_t>::max()) == 0);
    CHECK(writer.memOverflowed());
    CHECK(writer.write(data, 1) == 0);
    CHECK(writer.memLength() == sizeof(data));
    CHECK(writer.close() == EOF);
    REQUIRE(writer.openMem(buffer, sizeof(buffer)));
    CHECK(writer.write(data, sizeof(data)) == sizeof(data));
    CHECK(writer.close() == 0);
}

TEST_CASE("file writer reports a real stdio write failure on close and can be reused") {
    char path[] = "/tmp/snes9x-writer-XXXXXX";
    const int fd = mkstemp(path);
    REQUIRE(fd >= 0);
    REQUIRE(::close(fd) == 0);
    BufferedFileWriter writer;
    const uint8_t data[4] = {1, 2, 3, 4};
    REQUIRE(writer.open(path, "rb"));
    CHECK(writer.write(data, sizeof(data)) == sizeof(data));
    CHECK(writer.close() == EOF);
    CHECK(g_streamBufferOwner == nullptr);
    REQUIRE(writer.open(path, "wb"));
    CHECK(writer.write(data, sizeof(data)) == sizeof(data));
    CHECK(writer.close() == 0);
    g_fileBuffer = nullptr;
    CHECK_FALSE(writer.open(path, "wb")); // Must fail before truncating the saved file.
    g_fileBuffer = test_file_buffer;
    FILE* saved = fopen(path, "rb");
    REQUIRE(saved != nullptr);
    uint8_t actual[4] = {};
    CHECK(fread(actual, 1, sizeof(actual), saved) == sizeof(actual));
    CHECK(memcmp(actual, data, sizeof(data)) == 0);
    CHECK(fclose(saved) == 0);
    CHECK(unlink(path) == 0);
}
