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

## 5. Polymorphism and coupling

* **Function-pointer tables instead of virtual methods** (matches the codebase idiom: `SOpcodes`, `GetDSP`/`SetDSP`). A backend is a struct of function pointers installed at init; production installs the real implementation, tests install fakes. No vtables, no RTTI (the project builds with `-fno-rtti -fno-exceptions`).
* New modules depend on narrow interfaces, never on platform headers directly, so their logic compiles on the host for unit tests.
* One module = one responsibility = one pair of `.h`/`.cpp`. If a file needs a section-comment table of contents, split it.

## 6. Error handling

* No exceptions (disabled project-wide).
* Status enums propagate up; the boundary with legacy code translates to whatever the legacy call site expects (status bits, bool8).
* Log unexpected-but-survivable conditions through the existing `log3ds*` facility; never printf in core paths.

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
