#include "doctest.h"
#include "log_blackbox.h"

TEST_CASE("log black box: an empty log has nothing to keep") {
    CHECK(log_closed_cleanly("", 0));
    CHECK(log_closed_cleanly(NULL, 0));
}

TEST_CASE("log black box: the close marker on the last line means a clean exit") {
    const char* t = "[12.345] something\n[12.400] " LOG_CLOSE_MARKER "\n";
    CHECK(log_closed_cleanly(t, strlen(t)));
}

TEST_CASE("log black box: a log cut by a freeze is not clean") {
    const char* t = "[12.345] [mark] pause: backdrop pass 0\n";
    CHECK_FALSE(log_closed_cleanly(t, strlen(t)));
    CHECK_FALSE(log_closed_cleanly("abc", 3));          // shorter than the marker
    CHECK_FALSE(log_closed_cleanly(NULL, 10));
}

TEST_CASE("log black box: a marker cut short does not count") {
    const char* t = "[12.400] [log] close";
    CHECK_FALSE(log_closed_cleanly(t, strlen(t)));
}
