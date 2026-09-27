# Send log / crash dump to GitHub (issue #80)

Field logs and Luma crash dumps reach the developer today by pulling the
SD card. The emulator already talks HTTPS to api.github.com (updater,
libcurl). Two menu items post the newest log and the newest crash dump
as comments on a GitHub issue, using a token the user keeps on the SD.

## Configuration file (never in the repo)

`sd:/3ds/snes9x_3ds/github.env`, `KEY=VALUE` lines, `#` comments, no
quotes:

```
GITHUB_TOKEN=github_pat_...
GITHUB_COMMENTS_URL=https://api.github.com/repos/<owner>/<repo>/issues/<n>/comments
```

`github.env.example` ships in the repo root with placeholders;
`github.env` is git-ignored. The recommended token is a fine-grained PAT
limited to Issues (read/write) on that one repository. Known limit: the
console has no CA store, so the updater's transport does not verify the
server certificate; a narrow token bounds what a hijacked connection
could do.

## Menu

Emulation tab, right under "Enable Logging": "Send log to GitHub" and
"Send crash dump to GitHub". Both exist only when the file is present
and both keys parse; otherwise nothing is shown. Selecting one opens a
dialog ("Sending ..."), runs the post (no game runs in the menu), and
shows the result: the comment URL on success, or the failure stage and
HTTP status.

## What is sent

One comment per send, Markdown:

```
**snes9x_3ds <version> (<sha>)** · <model> · <mode> · <date>
<file name>, <size> bytes[, last <n> bytes]

```text
...
```
```

- Model: from CFGU (Old 3DS, Old 3DS XL, New 3DS, 2DS, New 3DS XL,
  New 2DS XL). Mode: "New mode (804 MHz + L2)" / "Old mode (268 MHz)"
  from the 3DS Mode setting on New hardware, "Old 3DS" otherwise.
- Log: the current session log (`debug_<version>_session.log`, the only
  one that exists), flushed first. GitHub caps a comment at 65536
  characters, so at most the last 60000 bytes go, cut at a line start,
  and the header says so.
- Crash dump: the newest `*.dmp` in `sd:/luma/dumps/arm11/` by
  modification time, as base64 in the code block (dumps are a few KiB).
  "No crash dump found" when the folder is empty.

## Code

- `github_env.h`: parse the file's text into token + URL (pure, tested).
- `github_report.h`: tail cut at a line boundary, base64, JSON string
  escaping, and the comment body builder into a fixed buffer (pure,
  tested; a body that would not fit is refused, never truncated
  mid-escape).
- `3dsupdatenet`: `update3dsNetPostJson(url, token, json, reply)` beside
  the GET, same setup; returns the HTTP status.
- `3dsgithub.cpp`: the 3DS glue: read the env, pick the file, compose,
  post, format the result line; `github3dsAvailable()` for the menu.
- `3dsmain.cpp`: the two items and their dialogs.

## Out of scope

Older logs (there is only one), multiple comments per send, Gist,
sending from inside gameplay.
