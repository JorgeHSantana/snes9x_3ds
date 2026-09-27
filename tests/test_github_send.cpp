#include "doctest.h"
#include "github_env.h"
#include "github_report.h"

TEST_CASE("github env: both keys parse with comments, blank lines, CRLF and spaces around '='") {
    GithubEnv e;
    CHECK(e.parse("# comment\r\n\r\nGITHUB_TOKEN = github_pat_abc \r\nGITHUB_COMMENTS_URL=https://api.github.com/repos/o/r/issues/80/comments\n"));
    CHECK(strcmp(e.token, "github_pat_abc") == 0);
    CHECK(strcmp(e.url, "https://api.github.com/repos/o/r/issues/80/comments") == 0);
}

TEST_CASE("github env: a missing or empty key, an unknown key, or an overlong value leaves it invalid") {
    GithubEnv e;
    CHECK_FALSE(e.parse("GITHUB_TOKEN=abc\n"));
    CHECK_FALSE(e.parse("GITHUB_TOKEN=\nGITHUB_COMMENTS_URL=x\n"));
    CHECK_FALSE(e.parse("TOKEN=abc\nURL=x\n"));
    CHECK_FALSE(e.parse(nullptr));
    char big[400]; memset(big, 'u', sizeof(big) - 1); big[sizeof(big) - 1] = '\0';
    char text[512]; snprintf(text, sizeof(text), "GITHUB_TOKEN=abc\nGITHUB_COMMENTS_URL=%s\n", big);
    CHECK_FALSE(e.parse(text));
    CHECK(e.parse("GITHUB_COMMENTS_URL=u\nGITHUB_TOKEN=t"));   // order free, no trailing newline
}

TEST_CASE("github report: the tail offset lands on a line start and is 0 when the file fits") {
    const char* text = "line one\nline two\nline three\n";
    size_t len = strlen(text);
    CHECK(github_tail_offset(text, len, 100) == 0);
    CHECK(github_tail_offset(text, len, 12) == 18);       // "line three\n" fits, "line two" would be split
    CHECK(github_tail_offset(text, len, 11) == 18);
    CHECK(github_tail_offset(text, len, 5) == len);       // no complete line fits: nothing
}

TEST_CASE("github report: base64 matches the reference vectors and refuses a short buffer") {
    char out[32];
    CHECK(github_base64((const uint8_t*)"", 0, out, sizeof(out)) == 0);
    CHECK(github_base64((const uint8_t*)"f", 1, out, sizeof(out)) == 4);   CHECK(strcmp(out, "Zg==") == 0);
    CHECK(github_base64((const uint8_t*)"fo", 2, out, sizeof(out)) == 4);  CHECK(strcmp(out, "Zm8=") == 0);
    CHECK(github_base64((const uint8_t*)"foobar", 6, out, sizeof(out)) == 8); CHECK(strcmp(out, "Zm9vYmFy") == 0);
    CHECK(github_base64((const uint8_t*)"foobar", 6, out, 8) == 0);
}

TEST_CASE("github report: JSON escaping covers quotes, backslashes, newlines and control bytes, and stops at the buffer") {
    char out[64]; size_t pos = 0;
    CHECK(github_json_append(out, sizeof(out), &pos, "a\"b\\c\nd\te\x01", 10));
    CHECK(strcmp(out, "a\\\"b\\\\c\\nd\\te\\u0001") == 0);
    char small[8]; pos = 0;
    CHECK_FALSE(github_json_append(small, sizeof(small), &pos, "0123456789", 10));
}

TEST_CASE("github report: the comment body carries the header, the fenced payload and the tail note") {
    GithubHeader h = { "v2.3", "effa713", "New 3DS XL", "New mode (804 MHz + L2)", "09/26/26 19:54:00",
                       "debug_v2.3_session.log", 100000, 12, "text" };
    static char body[4096];
    size_t n = github_comment_json(h, "[00.001] hi\n", 12, body, sizeof(body));
    CHECK(n > 0);
    CHECK(strncmp(body, "{\"body\":\"**snes9x_3ds v2.3 (effa713)**", 38) == 0);
    CHECK(strstr(body, "New 3DS XL") != nullptr);
    CHECK(strstr(body, "`debug_v2.3_session.log`, 100000 bytes (the tail)") != nullptr);
    CHECK(strstr(body, "```text\\n[00.001] hi\\n\\n... last 12 bytes shown\\n```\\n\"}") != nullptr);
    h.sentBytes = h.fileSize = 12;
    n = github_comment_json(h, "[00.001] hi\n", 12, body, sizeof(body));
    CHECK(strstr(body, "(the tail)") == nullptr);
    CHECK(strstr(body, "last 12 bytes") == nullptr);
}

TEST_CASE("github report: a payload past GitHub's limit or past the buffer is refused whole") {
    GithubHeader h = { "v", "s", "m", "o", "d", "f", 70000, 70000, "text" };
    static char payload[70001]; memset(payload, 'x', 70000); payload[70000] = '\0';
    static char body[80000];
    CHECK(github_comment_json(h, payload, 70000, body, sizeof(body)) == 0);
    h.fileSize = h.sentBytes = 1000;
    CHECK(github_comment_json(h, payload, 1000, body, 500) == 0);
    CHECK(github_comment_json(h, payload, 1000, body, sizeof(body)) > 0);
}
