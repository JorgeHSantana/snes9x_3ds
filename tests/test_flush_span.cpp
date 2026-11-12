#include "doctest.h"
#include "flush_span.h"

TEST_CASE("flush span: a list no draw read owes no flush") {
    FlushSpan s; flush_span_reset(&s);
    unsigned int from = 99, bytes = 99;
    CHECK_FALSE(flush_span_take(&s, 4000, &from, &bytes));
    CHECK(from == 99);
    // once a draw reads it, everything written is owed, from the start
    flush_span_drawn(&s);
    REQUIRE(flush_span_take(&s, 4000, &from, &bytes));
    CHECK(from == 0);
    CHECK(bytes == 4000);
}

TEST_CASE("flush span: a second submission flushes only what was appended") {
    FlushSpan s; flush_span_reset(&s);
    unsigned int from = 0, bytes = 0;
    flush_span_drawn(&s);
    REQUIRE(flush_span_take(&s, 4000, &from, &bytes));
    CHECK_FALSE(flush_span_take(&s, 4000, &from, &bytes));
    REQUIRE(flush_span_take(&s, 4096, &from, &bytes));
    CHECK(from == 4000);
    CHECK(bytes == 96);
    CHECK_FALSE(flush_span_take(&s, 4096, &from, &bytes));
}

TEST_CASE("flush span: an emptied list starts over, drawn or not") {
    FlushSpan s; flush_span_reset(&s);
    unsigned int from = 0, bytes = 0;
    flush_span_drawn(&s);
    REQUIRE(flush_span_take(&s, 800, &from, &bytes));
    flush_span_reset(&s);
    CHECK_FALSE(flush_span_take(&s, 800, &from, &bytes));
    flush_span_drawn(&s);
    REQUIRE(flush_span_take(&s, 800, &from, &bytes));
    CHECK(from == 0);
    CHECK(bytes == 800);
}

TEST_CASE("flush span: a list wound back has its rewritten bytes flushed again") {
    FlushSpan s; flush_span_reset(&s);
    unsigned int from = 0, bytes = 0;
    flush_span_drawn(&s);
    REQUIRE(flush_span_take(&s, 1000, &from, &bytes));
    // two vertices given back, nothing new: no call, the mark follows
    CHECK_FALSE(flush_span_take(&s, 984, &from, &bytes));
    // written again over the same bytes
    REQUIRE(flush_span_take(&s, 1000, &from, &bytes));
    CHECK(from == 984);
    CHECK(bytes == 16);
}

TEST_CASE("flush span: an empty list owes nothing") {
    FlushSpan s; flush_span_reset(&s);
    unsigned int from = 0, bytes = 0;
    flush_span_drawn(&s);
    CHECK_FALSE(flush_span_take(&s, 0, &from, &bytes));
}
