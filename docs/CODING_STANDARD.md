# Coding Standard — New Code

This standard applies to **all new code** written in this fork from 2026-08 onward (starting with the MSU-1 subsystem), and to code substantially modified. Legacy code is not retrofitted; minimal edits inside legacy files follow the local style of that file.

The spirit: embedded-systems discipline — deterministic memory, defensive interfaces, zero hidden costs. Not MISRA, but MISRA-adjacent where it pays off.

## 1. Naming

| Entity | Convention | Example |
|---|---|---|
| Class / struct / enum type | PascalCase | `Msu1State`, `AudioBackend` |
| Function / method | snake_case | `bool open_track(...)` |
| Local variable / parameter | snake_case | `track_number` |
| Private member variable | snake_case with trailing `_` | `audio_file_` |
| Compile-time constant / static global | SCREAMING_SNAKE_CASE | `MSU1_PCM_HEADER_SIZE` |
| Enum class values | PascalCase | `Msu1Result::TrackMissing` |
| File names | snake_case | `msu1.cpp`, `3dsmsu.cpp` |

**Legacy-boundary exception**: symbols that plug into an existing subsystem's pattern keep that pattern's naming, so call sites stay consistent (e.g., core hooks named `S9xMSU1ReadPort` to match `S9xGetPPU`, platform entry points prefixed `msu3ds*` to match `snd3ds*`/`gpu3ds*`). Everything *behind* those entry points follows this standard.

## 2. Types

* Fixed-width types always: `uint8_t`, `int32_t`, `size_t` — never bare `int`/`long` in interfaces.
* `enum class` always, with explicit underlying type when stored or serialized: `enum class Msu1Event : uint8_t { ... }`. (Same runtime cost as plain `enum`; the safety is free.)
* `nullptr`, never `NULL` or `0` for pointers.
* `static_assert` invariants that matter (struct sizes for serialized data, buffer-size relationships).
* No magic numbers — named constants.

## 3. Functions and parameters

* **Primitives by value** (`uint32_t offset`) — a `const uint8_t&` is a pointer in disguise and costs more than the copy.
* **Non-trivial types by `const T&`** when read-only; by `T&` when the function writes to them. West-const style: `const T&` (equivalent to `T const&`; pick one — we use west).
* **Pointers only where absence is legitimate** (`FILE*` of a not-yet-opened track). Every pointer parameter is null-checked at function entry before use. Every index/offset/size parameter is range-checked.
* **Fallible functions return a status** — `bool` for simple success/failure, `enum class Msu1Result : uint8_t` when the caller needs the reason. Outputs go through reference parameters. Never rely on the caller having validated anything.
* Failure behavior: return the error, set the correct status bit, never crash, never leave a resource half-open.
* Early return on validation failure; keep the happy path unindented.

## 4. Memory

* **No dynamic allocation at runtime.** All buffers are allocated once at subsystem init (static storage or one `linearAlloc`, matching the existing `snd3dsInitialize` pattern) and freed once at teardown. No `new`/`malloc` per frame, per track, or per call.
* No STL containers in hot paths; fixed-size arrays with explicit capacity constants.
* No smart pointers — with no dynamic allocation there is nothing to manage. Each `FILE*` (or similar resource) has exactly one owning struct; every teardown path (ROM switch, reset, exit, error) closes it. A leak is a test failure.

### Bounded functions only

**The principle: two independent stopping conditions, not one.**

`strcpy(dst, src)` has exactly one — a `\0` somewhere in `src`. If that byte is not where it should be, the copy keeps going until it finds a zero somewhere else in memory. And it is genuinely easy for it not to be there: a truncated file read, a field that filled its array exactly, a hand-edited file on the SD card, an earlier `strncpy` that did not terminate. There is no second line of defence, because there is no second mechanism.

`snprintf(dst, sizeof(dst), "%s", src)` has two, and they are **independent**: the terminator *and* the destination's capacity. Either one alone stops the write. That is the whole value of the length-taking family — you stop depending on the data being well-formed.

The corollary is the rule in the next block: the bound must describe the **destination**. A bound copied from somewhere else is a second mechanism that is *wrong*, which is worse than one that is right — it reads as safe and is not.

**Always the length-taking variant.** An unbounded string or buffer call is a defect on sight, not a style preference.

| Never | Always | Note |
|---|---|---|
| `strcpy` | `snprintf(dst, sizeof(dst), "%s", src)` | see below — `strncpy` is *not* the safe default |
| `strcat` | `snprintf` into a cursor, or an explicit length check | |
| `sprintf`, `vsprintf` | `snprintf`, `vsnprintf` | |
| `gets` | `fgets(buf, sizeof(buf), fp)` | |
| `scanf("%s")` | `scanf("%<N>s")` with an explicit width | |
| `strcmp` on fixed fields | `strncmp` with the field size | |
| `atoi`, `atol` | `strtol` with `errno`/`endptr` checked | no error signal otherwise |
| `memcpy(dst, src, n)` where `n` is not derived from `dst` | derive `n` from the destination, then copy | |

**The bound must come from the destination, never from a literal.** This is the part that actually prevents overflows, and the part that is usually skipped:

```cpp
strncpy(cheat.code, code, 99);                        // WRONG - code[] is 50 bytes
strncpy(cheat.code, code, sizeof(cheat.code) - 1);    // right
```

The first line is a real overflow that shipped in a sibling project ([emus3ds#1](https://github.com/JorgeHSantana/emus3ds/issues/1)) — it *uses* the "n" variant and still writes 49 bytes past the end, because `99` was copied from the neighbouring `name[100]` field. `sizeof(dst)` survives someone resizing the field; a literal does not.

`sizeof` only works on a real array. Once the destination has decayed to a pointer, pass its capacity explicitly alongside it.

**Know what each bounded function does *not* do:**

* **`strncpy` does not null-terminate** when `strlen(src) >= n`. It also pads the whole remaining destination with `\0`, which is wasted work on large buffers. If you use it, terminate explicitly:
  ```cpp
  strncpy(dst, src, sizeof(dst) - 1);
  dst[sizeof(dst) - 1] = '\0';
  ```
  Prefer `snprintf(dst, sizeof(dst), "%s", src)` — it always terminates and states its intent.
* **`snprintf` returns the length it *wanted* to write**, not what it wrote. Truncation is silent unless you check:
  ```cpp
  int n = snprintf(buf, sizeof(buf), fmt, ...);
  if (n < 0 || n >= (int)sizeof(buf)) { /* truncated - handle it */ }
  ```
* **`strncat`'s bound is the space remaining**, not the buffer size — an easy off-by-one. Prefer building with `snprintf` and a running offset.

**Never pass a runtime string as a format string.** `fprintf(fp, buffer)` is a defect, not a shortcut — use `fputs(buffer, fp)`. This includes strings that arrive from files, translations or SD-card paths.

Comparison bounds get the same treatment: `if (used + needed >= sizeof(dst))`, never `> 4096` against a `char[4096]`. The literal form is off by one and does not survive a resize.

### Do not re-derive what you already know — avoid `strlen`

`strlen` walks the whole string on every call, and it inherits the *same* fragility we just designed away: it trusts a terminator to be there. Calling it in a loop turns an O(n) job into O(n²) for no gain. Prefer, in this order:

1. **Compile-time.** `sizeof(arr)` on a real array in scope; `sizeof("literal") - 1` for a string literal. Free — the compiler folds it.
2. **Carried.** Keep the length next to the buffer and update it when you write. See the buffer type below.
3. **Returned.** `snprintf` already tells you how much it wrote — use its return instead of measuring afterwards:
   ```cpp
   int n = snprintf(buf, sizeof(buf), "%s", src);        // n is the length, for free
   if (n < 0 || n >= (int)sizeof(buf)) { /* truncated */ }
   ```
   Building a path or a message in several steps means carrying a running offset, not re-measuring:
   ```cpp
   size_t used = 0;
   used += snprintf(path + used, sizeof(path) - used, "%s", dir);
   used += snprintf(path + used, sizeof(path) - used, "/%s", name);   // check `used` each step
   ```
4. **`strlen` last**, only when the length genuinely is not known and the string is trusted-terminated (a literal, or a buffer this code just terminated itself).

**The `sizeof` trap.** It only works on an array *in scope*. Once the buffer has decayed to a pointer, `sizeof` silently gives you the pointer size:

```cpp
void f(char *dst)    { snprintf(dst, sizeof(dst), ...); }   // WRONG — 4 bytes
void f(char (&dst)[N]) { ... }                              // array reference keeps the size
void f(char *dst, size_t capacity) { ... }                  // or pass it explicitly
```

If a function takes a buffer, it takes its capacity too — always, in the same signature.

### Carrying the size with the buffer

The cleanest way to stop re-deriving anything is to make the size travel with the data. A small fixed-capacity POD is fully in-idiom here — no heap, no STL, and the project already uses this shape for `ButtonMapping<N>`:

```cpp
template <size_t N>
struct FixedString {
    static constexpr size_t Capacity = N - 1;   // usable characters, terminator excluded
    char   data[N];
    size_t length = 0;                          // maintained on write, never re-derived

    bool assign(const char *src);               // false if it would truncate
    bool append(const char *src);               // false if it would truncate
    void clear() { data[0] = '\0'; length = 0; }
    const char *c_str() const { return data; }
};
```

What this buys:

* The capacity cannot be got wrong at a call site — it is not a number anyone types.
* `length` is O(1) and always current, so no `strlen`.
* `Capacity` is a compile-time constant, so `static_assert` can enforce relationships between buffers (for example, that a parsed field always fits its destination field).
* Truncation becomes a return value instead of silent corruption.

**Do not retrofit it everywhere.** Introduce it where new code owns a buffer; at legacy boundaries keep passing `data` and `Capacity` explicitly. The goal is that new code never types a length literal, not a project-wide rewrite.

## 5. Polymorphism and coupling

* **Function-pointer tables instead of virtual methods** (matches the codebase idiom: `SOpcodes`, `GetDSP`/`SetDSP`). A backend is a struct of function pointers installed at init; production installs the real implementation, tests install fakes. No vtables, no RTTI (the project builds with `-fno-rtti -fno-exceptions`).
* New modules depend on narrow interfaces, never on platform headers directly, so their logic compiles on the host for unit tests.
* One module = one responsibility = one pair of `.h`/`.cpp`. If a file needs a section-comment table of contents, split it.

## 6. Error handling

* No exceptions (disabled project-wide).
* Status enums propagate up; the boundary with legacy code translates to whatever the legacy call site expects (status bits, bool8).
* Log unexpected-but-survivable conditions through the existing `log3ds*` facility; never printf in core paths.

### Check every return value

§3 requires fallible functions to *return* a status. This is the other half: **at the call site, every fallible call is checked.** With exceptions disabled, an ignored return is the only way an error can disappear — and it disappears silently, surfacing later as corrupted data or a crash with no connection to its cause.

Ones that bite in this codebase:

| Call | What an unchecked return hides |
|---|---|
| `fopen` | every subsequent `fread`/`fwrite` on a `NULL` handle |
| `fread` / `fwrite` | short read/write — a truncated savestate that loads as garbage |
| `fseek` / `ftell` | reading from the wrong offset |
| `fscanf` / `sscanf` | items *not* converted; the destination keeps its previous value |
| `snprintf` | silent truncation (it returns the length it *wanted*) |
| `malloc` / `linearAlloc` | a `NULL` dereferenced a line later |
| `svc*`, `APT_*`, `GSPGPU_*`, `ndspInit` | a `Result` code; the operation simply did not happen |

Two consequences worth stating:

* **Check before you use, not after.** A null check placed below the first dereference is not a check. (`3dssound.cpp` in the sibling project does exactly this — [emus3ds#5](https://github.com/JorgeHSantana/emus3ds/issues/5).)
* **An intentionally ignored return is written down.** If a failure genuinely does not matter, say so at the call site — `(void)fclose(f);  // best-effort, already flushed` — so a reader can tell a deliberate decision from an oversight.

`-Wunused-result` catches the subset the toolchain marks `warn_unused_result`; it does not catch most of the table above, so this is a review rule, not a compiler rule.

## 7. Concurrency

* State shared across threads is documented at its declaration (who writes, who reads, under which lock).
* Reuse the existing fences (`snesAccessLock`, `snd3dsDrainMixing()`), do not invent parallel locking schemes.
* `std::atomic` for single-word flags, matching `snd3DS.generateSilence`.

## 8. Testing (mandatory)

* Everything new ships with host-side unit tests (doctest, `make test`).
* Legacy files receive only minimal delegating edits (3–5 lines); the delegated-to function is fully tested with the same inputs the call site passes.
* Every function that validates parameters has at least one test feeding it garbage (null, out-of-range, truncated).
* Resource ownership is tested: init→teardown cycles assert every open resource is closed.

## 9. Build discipline (mandatory)

* **RTTI and exceptions MUST stay disabled** (`-fno-rtti -fno-exceptions`). This applies to the target build (already enforced by the Makefile) **and to production sources when compiled for host tests** — the test harness may enable exceptions for the framework's own use, but production code must never rely on `throw`, `try`, `typeid`, or `dynamic_cast`. In `tests/Makefile`, production objects (`CORE_SRCS`) are compiled with `-fno-rtti -fno-exceptions`; only the `test_*.cpp` files use the framework defaults.
* New third-party dependencies require explicit maintainer approval.

## 10. Dependencies — decisions on record

* **ETL (Embedded Template Library, etlcpp/etl)** — evaluated 2026-08-08, **deferred (YAGNI)**. Our fixed arrays + parameter validation already provide determinism without heap; ETL adds no performance and one dependency. It is the **pre-approved candidate** if a future feature genuinely needs fixed-capacity container semantics (bounded queues/maps, `etl::state_machine`): header-only, works with `-fno-exceptions`. Prefer a small hand-rolled, tested structure first.
* **Pure C conversion** — evaluated 2026-08-08, **rejected**: the enforced C++ subset (no exceptions/RTTI/virtuals/heap) compiles to the same machine code as C; converting would lose compile-time safety for zero runtime gain.

## 11. Formatting

* Braces always, even for single-statement `if`s.
* 4-space indent, no tabs (matches existing platform code).
* One declaration per line.
* Comments state constraints the code can't (`// guarded by snesAccessLock`), not what the next line does. English only.

## 12. Documentation (mandatory)

* Document as you go, in markdown, alongside the code — never "later".
* **Architecture**: a change that adds or reshapes a subsystem (new module, new data flow, new uniform/format/protocol) updates the matching `docs/*.md` — or creates one — in the same commit series. Architecture docs describe the system as it **is**, not its history.
* **Dev journal (diário de bordo)**: every working session appends an entry to `docs/journal/YYYY-MM.md` — date, what was attempted, what shipped, dead ends and *why* they failed, and the validation evidence (game/scene/hardware tested). The journal is append-only; it is where the history lives.
* A bug whose root cause took real investigation gets its mechanism written down: a journal entry at minimum, and a note in the relevant `docs/*.md` when the constraint is permanent (e.g. "the tile TEV replaces RGB from the texture — vertex RGB never reaches the pixel").
