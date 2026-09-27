#ifndef GITHUB_ENV_H
#define GITHUB_ENV_H

#include <stddef.h>
#include <string.h>

// sd:/3ds/snes9x_3ds/github.env (issue #80): KEY=VALUE lines, '#'
// comments, no quotes; CR/LF tolerated. Both keys must be present and
// non-empty for the menu items to exist. Pure text parsing, tested.
struct GithubEnv
{
    static const size_t TOKEN_MAX = 128;
    static const size_t URL_MAX = 256;
    char token[TOKEN_MAX];
    char url[URL_MAX];

    bool valid() const { return token[0] != '\0' && url[0] != '\0'; }

    // text: the file's contents (NUL-terminated). Returns valid().
    bool parse(const char* text)
    {
        token[0] = '\0'; url[0] = '\0';
        if (text == nullptr) return false;
        const char* p = text;
        while (*p) {
            const char* eol = strchr(p, '\n');
            size_t n = eol ? (size_t)(eol - p) : strlen(p);
            line(p, n);
            p += n + (eol ? 1 : 0);
        }
        return valid();
    }

private:
    static void trim(const char*& s, size_t& n)
    {
        while (n && (s[0] == ' ' || s[0] == '\t' || s[0] == '\r')) { s++; n--; }
        while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) n--;
    }
    static void copy(char* dst, size_t cap, const char* s, size_t n)
    {
        if (n >= cap) { dst[0] = '\0'; return; }   // too long: as good as missing
        memcpy(dst, s, n); dst[n] = '\0';
    }
    void line(const char* s, size_t n)
    {
        trim(s, n);
        if (n == 0 || s[0] == '#') return;
        const char* eq = (const char*)memchr(s, '=', n);
        if (eq == nullptr) return;
        const char* k = s; size_t kn = (size_t)(eq - s); trim(k, kn);
        const char* v = eq + 1; size_t vn = n - kn - 1 - (size_t)(k - s); v = eq + 1; vn = (size_t)(s + n - v); trim(v, vn);
        if (kn == 12 && memcmp(k, "GITHUB_TOKEN", 12) == 0) copy(token, TOKEN_MAX, v, vn);
        else if (kn == 19 && memcmp(k, "GITHUB_COMMENTS_URL", 19) == 0) copy(url, URL_MAX, v, vn);
    }
};

#endif
