#include "doctest.h"
#include "sram_writer.h"

static SramWriterQueue q;   // 128 KiB: static, not on the test stack

TEST_CASE("sram writer: a request copies the data and queues one job") {
    q.reset();
    uint8_t sram[16]; for (int i = 0; i < 16; i++) sram[i] = (uint8_t)(i * 3);
    CHECK(q.request("sdmc:/saves/game.srm", sram, sizeof(sram)));
    CHECK(q.state == SramWriterQueue::QUEUED);
    CHECK(q.size == 16);
    CHECK(q.data[5] == 15);
    CHECK(q.busy());
    sram[5] = 0xFF;                       // the caller's SRAM may change right after
    CHECK(q.data[5] == 15);               // the copy is what gets written
    CHECK(q.jobs == 1);
}

TEST_CASE("sram writer: a second request while a job is in flight is refused, not queued") {
    q.reset();
    uint8_t sram[4] = {1, 2, 3, 4};
    CHECK(q.request("a.srm", sram, 4));
    CHECK_FALSE(q.request("b.srm", sram, 4));   // queued, not taken yet
    CHECK(q.take());
    CHECK_FALSE(q.request("b.srm", sram, 4));   // writing
    q.complete(true);
    CHECK_FALSE(q.request("b.srm", sram, 4));   // done but not polled: data must stay stable
    CHECK(q.refused == 3);
    uint8_t dirty = 0;
    CHECK(q.poll(dirty));
    CHECK(dirty == 0);
    CHECK(q.request("b.srm", sram, 4));          // idle again
}

TEST_CASE("sram writer: a failed write re-raises dirty so the autosave timer retries") {
    q.reset();
    uint8_t sram[4] = {0};
    uint8_t dirty = 0;
    CHECK(q.request("a.srm", sram, 4));
    CHECK(q.take());
    q.complete(false);
    CHECK(q.poll(dirty));
    CHECK(dirty == 1);
    CHECK(q.state == SramWriterQueue::IDLE);
    CHECK_FALSE(q.poll(dirty));                  // nothing more to consume
}

TEST_CASE("sram writer: take and complete are no-ops outside their state") {
    q.reset();
    CHECK_FALSE(q.take());                       // idle: nothing queued
    q.complete(true);                            // idle: ignored
    CHECK(q.state == SramWriterQueue::IDLE);
    uint8_t dirty = 0;
    CHECK_FALSE(q.poll(dirty));
}

TEST_CASE("sram writer: invalid requests are refused without touching the state") {
    q.reset();
    uint8_t sram[4] = {0};
    CHECK_FALSE(q.request(nullptr, sram, 4));
    CHECK_FALSE(q.request("", sram, 4));
    CHECK_FALSE(q.request("a.srm", nullptr, 4));
    CHECK_FALSE(q.request("a.srm", sram, 0));
    CHECK_FALSE(q.request("a.srm", sram, SRAM_SAVE_MAX_SIZE + 1));
    char longPath[SRAM_WRITER_PATH_MAX + 8]; memset(longPath, 'x', sizeof(longPath) - 1); longPath[sizeof(longPath) - 1] = '\0';
    CHECK_FALSE(q.request(longPath, sram, 4));
    CHECK(q.state == SramWriterQueue::IDLE);
    CHECK(q.jobs == 0);
    CHECK(q.refused == 0);                       // invalid input is not "busy"
}
