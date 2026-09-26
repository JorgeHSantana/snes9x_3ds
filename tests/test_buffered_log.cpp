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
static BufferedLog log;   // 64 KiB spool inside: static, not on the test stack
}

TEST_CASE("log batches lines until the interval, then a tick hands them to the writer and drain writes them") {
    LogFixture file;
    REQUIRE(log.open(file.path, 0));
    for (uint32_t i = 0; i < 999; ++i) {
        REQUIRE(log.write("entry\n"));
        CHECK_FALSE(log.tick(i));          // nothing handed over yet
        CHECK(log.drain());                // the writer finds nothing
    }
    CHECK(file.size() == 0);
    CHECK(log.tick(1000));
    CHECK(file.size() == 0);               // handed over, not written: no I/O on the producer
    CHECK(log.drain());
    CHECK(file.size() == 999 * 6);
    CHECK_FALSE(log.tick(2000));           // nothing new
    CHECK(log.close());
    CHECK(file.size() == 999 * 6);
}

TEST_CASE("log close drains the last lines and reopen starts a clean session") {
    LogFixture file;
    CHECK_FALSE(log.open(nullptr, 0));
    CHECK_FALSE(log.open("", 0));
    CHECK_FALSE(log.write("closed"));
    CHECK_FALSE(log.tick(1000));
    REQUIRE(log.open(file.path, 9000));
    CHECK_FALSE(log.open(file.path, 9000));
    CHECK_FALSE(log.write(nullptr));
    CHECK(log.write("%s %u\n", "value", 42u));
    CHECK_FALSE(log.tick(100));           // clock rollback rebases the deadline
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

TEST_CASE("log with a writer that keeps up streams 128 KiB through the 64 KiB spool without dropping") {
    LogFixture file;
    REQUIRE(log.open(file.path, 0));
    char line[257];
    memset(line, 'x', sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    for (uint32_t i = 0; i < 512; ++i) {
        REQUIRE(log.write("%s", line));
        CHECK(log.drain());                // the writer thread, modelled inline
    }
    CHECK(log.dropped_lines() == 0);
    CHECK(log.close());
    CHECK(file.size() == 512 * 256);
}

TEST_CASE("log with a stalled writer drops lines, counts them, and announces them once it moves again") {
    LogFixture file;
    REQUIRE(log.open(file.path, 0));
    char line[257];
    memset(line, 'x', sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    for (uint32_t i = 0; i < 300; ++i) REQUIRE(log.write("%s", line));   // 75 KiB into 64 KiB
    CHECK(log.dropped_lines() > 0);
    const uint32_t dropped = log.dropped_lines();
    CHECK(log.drain());                    // the writer wakes up: chunk 0 out
    CHECK(log.write("after\n"));           // the notice goes in before this line
    CHECK(log.close());
    FILE* saved = fopen(file.path, "rb");
    REQUIRE(saved != nullptr);
    static char all[80 * 1024]; size_t n = fread(all, 1, sizeof(all) - 1, saved); all[n] = '\0';
    CHECK(fclose(saved) == 0);
    char expect[64]; snprintf(expect, sizeof(expect), "[log] %u lines dropped", (unsigned)dropped);
    CHECK(strstr(all, expect) != nullptr);
    CHECK(strstr(all, "after\n") != nullptr);
    CHECK(n == (300 - dropped) * 256 + strlen(expect) + strlen(" (SD writer behind)\n") + 6);
}

#ifdef __linux__
TEST_CASE("log write errors surface at drain, stay failures through close, and a new session recovers") {
    LogFixture file;
    REQUIRE(log.open("/dev/full", 0));
    CHECK(log.write("failure\n"));         // buffered: no error yet
    CHECK(log.tick(1000));
    CHECK_FALSE(log.drain());
    CHECK_FALSE(log.write("retry\n"));
    CHECK_FALSE(log.close());
    CHECK_FALSE(log.close());
    REQUIRE(log.open(file.path, 0));
    CHECK(log.write("recovered\n"));
    CHECK(log.close());
    CHECK(file.size() == 10);
}
#endif

TEST_CASE("log drain in three steps writes the chunk once and refuses a second taker meanwhile") {
    LogFixture file;
    REQUIRE(log.open(file.path, 0));
    CHECK(log.write("one\n"));
    CHECK(log.tick(1000));
    const int c = log.drain_take();
    CHECK(c >= 0);
    CHECK(log.drain_take() == -1);         // in flight
    CHECK(log.drain());                    // the synchronous path steps aside
    CHECK(file.size() == 0);
    CHECK(log.drain_write(c));
    CHECK(file.size() == 4);
    log.drain_release(c, true);
    CHECK(log.drain_take() == -1);         // nothing left
    CHECK(log.close());
    CHECK(file.size() == 4);
}
