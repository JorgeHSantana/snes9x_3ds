#include "doctest.h"
#include "buffered_log.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
struct LogFixture {
    char path[64] = "/tmp/snes9x-log-XXXXXX";
    LogFixture()
    {
        const int fd = mkstemp(path);
        REQUIRE(fd >= 0);
        REQUIRE(::close(fd) == 0);
    }
    ~LogFixture() { (void)unlink(path); }
    size_t size() const
    {
        struct stat info = {};
        REQUIRE(stat(path, &info) == 0);
        return static_cast<size_t>(info.st_size);
    }
};
}

TEST_CASE("log batches messages until the flush interval, then flushes without new messages") {
    LogFixture file;
    BufferedLog log;
    REQUIRE(log.open(file.path, 0));
    for (uint32_t i = 0; i < 999; ++i) {
        REQUIRE(log.write("entry\n"));
        REQUIRE(log.tick(i));
    }
    CHECK(file.size() == 0);
    CHECK(log.tick(1000));
    CHECK(file.size() == 999 * 6);
    CHECK(log.tick(2000));
    CHECK(file.size() == 999 * 6);
    CHECK(log.close());
}

TEST_CASE("log close drains the last messages and reopen starts a clean session") {
    LogFixture file;
    BufferedLog log;
    CHECK_FALSE(log.open(nullptr, 0));
    CHECK_FALSE(log.open("", 0));
    CHECK_FALSE(log.write("closed"));
    CHECK_FALSE(log.tick(1000));
    REQUIRE(log.open(file.path, 9000));
    CHECK_FALSE(log.open(file.path, 9000));
    CHECK_FALSE(log.write(nullptr));
    CHECK(log.write("%s %u\n", "value", 42u));
    CHECK(log.tick(100)); // Clock rollback rebases the deadline.
    CHECK(file.size() == 0);
    CHECK(log.tick(1099));
    CHECK(file.size() == 0);
    CHECK(log.close());
    CHECK(file.size() == 9);
    FILE* saved = fopen(file.path, "rb");
    REQUIRE(saved != nullptr);
    char actual[16] = {};
    CHECK(fread(actual, 1, sizeof(actual) - 1, saved) == 9);
    CHECK(strcmp(actual, "value 42\n") == 0);
    CHECK(fclose(saved) == 0);
    REQUIRE(log.open(file.path, 0));
    CHECK(log.write("new\n"));
    CHECK(log.close());
    CHECK(file.size() == 4);
}

TEST_CASE("log capacity forces bounded streaming without dropping records") {
    LogFixture file;
    BufferedLog log;
    REQUIRE(log.open(file.path, 0));
    char line[257];
    memset(line, 'x', sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    for (uint32_t i = 0; i < 512; ++i) {
        REQUIRE(log.write("%s", line));
    }
    CHECK(file.size() > 0);
    CHECK(log.close());
    CHECK(file.size() == 512 * 256);
}

#ifdef __linux__
TEST_CASE("log write errors remain failures through close and a new session can recover") {
    LogFixture file;
    BufferedLog log;
    REQUIRE(log.open("/dev/full", 0));
    (void)log.write("failure\n"); // libc may surface the failure now or on flush.
    CHECK_FALSE(log.tick(1000));
    CHECK_FALSE(log.write("retry\n"));
    CHECK_FALSE(log.close());
    CHECK_FALSE(log.close());
    REQUIRE(log.open(file.path, 0));
    CHECK(log.write("recovered\n"));
    CHECK(log.close());
    CHECK(file.size() == 10);
}
#endif
