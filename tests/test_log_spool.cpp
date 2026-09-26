#include "doctest.h"
#include "log_spool.h"

static LogSpool s;   // 64 KiB: static, not on the test stack

TEST_CASE("log spool: lines go to the front chunk in order and rotate hands them over") {
    s.reset();
    CHECK(s.append("a\n", 2));
    CHECK(s.append("bb\n", 3));
    CHECK(s.take() == -1);
    CHECK(s.rotate());
    int c = s.take();
    CHECK(c == 0);
    CHECK(s.len[c] == 5);
    CHECK(memcmp(s.data[c], "a\nbb\n", 5) == 0);
    CHECK(s.append("c\n", 2));            // lands in the other chunk meanwhile
    CHECK(s.front == 1);
    CHECK_FALSE(s.rotate());               // writer still holds chunk 0
    s.release(c);
    CHECK(s.rotate());
    CHECK(s.take() == 1);
    CHECK(s.len[1] == 2);
    s.release(1);
    CHECK(s.empty());
    CHECK_FALSE(s.rotate());               // nothing to hand over
}

TEST_CASE("log spool: a full front rotates by itself when the writer is free, drops when it is not") {
    s.reset();
    static char line[1024]; memset(line, 'x', sizeof(line));
    for (int i = 0; i < 32; i++) CHECK(s.append(line, 1024));   // exactly one chunk
    CHECK(s.len[0] == (size_t)LogSpool::CHUNK);
    CHECK(s.append(line, 1024));           // rotated: chunk 0 ready, chunk 1 front
    CHECK(s.take() == 0);
    CHECK(s.len[1] == 1024);
    for (int i = 0; i < 31; i++) CHECK(s.append(line, 1024));
    CHECK_FALSE(s.append(line, 1024));     // both chunks busy: dropped
    CHECK(s.droppedLines == 1);
    CHECK(s.droppedBytes == 1024);
    s.release(0);
    CHECK(s.append(line, 1024));           // rotates onto the freed chunk
    CHECK(s.take() == 1);
    CHECK(s.droppedLines == 1);
}

TEST_CASE("log spool: an oversized line is dropped and counted, an empty one is a no-op") {
    s.reset();
    static char big[LogSpool::CHUNK + 1];
    CHECK_FALSE(s.append(big, sizeof(big)));
    CHECK(s.droppedLines == 1);
    CHECK(s.append("", 0));
    CHECK(s.empty());
    s.release(0);                          // releasing a chunk that is not ready changes nothing
    CHECK(s.empty());
}
