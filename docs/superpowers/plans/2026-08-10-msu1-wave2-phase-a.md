# MSU-1 Wave 2 Phase A Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** MSU-1 FMV playback: DMA transfer modes 0-7 for the data port plus bulk `fread` reads.

**Architecture:** Two pure additions to the msu1 core (`msu1_read_data_bulk`, `msu1_dma_b_offset`), then rewire the existing MSU-1 branch in `S9xDoDMA` to use them. Spec: `docs/superpowers/specs/2026-08-10-msu1-wave2-phase-a-design.md`.

**Tech Stack:** C++ (no RTTI/exceptions), doctest host suite (`make -C tests`), devkitARM Docker build.

## Global Constraints

- RTTI and exceptions stay disabled; follow `docs/CODING_STANDARD.md`.
- Host tests for everything new; suite must stay green: `make -C tests`.
- No threads, ring buffers, precache, or watchdogs (Phase B only).
- Wave-1 behavior unchanged: port-1 CPU reads, seeks, audio, savestates, mode-0 DMA.
- Commits end with `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

---

### Task 1: msu1_read_data_bulk

**Files:**
- Modify: `source/Snes9x/msu1.h` (API section, near `msu1_read_port`)
- Modify: `source/Snes9x/msu1.cpp` (next to `msu1_read_port`)
- Test: `tests/test_msu1_data_bulk.cpp` (new; register in `tests/Makefile` TEST_SRCS)

**Interfaces:**
- Consumes: `Msu1State` fields `data_file`, `data_pos`, `data_size`, `enabled` (existing).
- Produces: `uint32_t msu1_read_data_bulk(Msu1State& state, uint8_t* dst, uint32_t count);` — used by Task 3.

- [ ] **Step 1: Write the failing test**

```cpp
#include "doctest.h"
#include "msu1.h"
#include "fixtures.h"
#include <cstring>

using fixtures::put_file;
using fixtures::make_tmpdir;

// Builds a state with a data file of `n` sequential bytes 0..n-1.
static Msu1State make_data_state(const std::string& dir, int n)
{
    std::string path = dir + "/game.msu";
    std::string bytes;
    for (int i = 0; i < n; i++) { bytes.push_back((char)(i & 0xFF)); }
    put_file(path, bytes);
    Msu1State state = {};
    state.enabled   = true;
    state.data_file = fopen(path.c_str(), "rb");
    state.data_size = (uint32_t)n;
    state.data_pos  = 0;
    REQUIRE(state.data_file != nullptr);
    return state;
}

TEST_CASE("bulk read equals successive port-1 reads")
{
    std::string dir = make_tmpdir();
    Msu1State a = make_data_state(dir, 300);
    Msu1State b = make_data_state(dir, 300);

    uint8_t bulk[300];
    CHECK(msu1_read_data_bulk(a, bulk, 300) == 300);
    CHECK(a.data_pos == 300);
    for (int i = 0; i < 300; i++) {
        CHECK(bulk[i] == msu1_read_port(b, 1));
    }
    fclose(a.data_file); fclose(b.data_file);
}

TEST_CASE("bulk read is short at EOF and zero afterwards")
{
    std::string dir = make_tmpdir();
    Msu1State s = make_data_state(dir, 10);
    uint8_t buf[32];
    memset(buf, 0xAA, sizeof(buf));
    CHECK(msu1_read_data_bulk(s, buf, 32) == 10);
    CHECK(s.data_pos == 10);
    CHECK(msu1_read_data_bulk(s, buf, 32) == 0);
    fclose(s.data_file);
}

TEST_CASE("bulk read after a port-3 seek starts at the new position")
{
    std::string dir = make_tmpdir();
    Msu1State s = make_data_state(dir, 300);
    msu1_write_port(s, 0, 0x00);  // seek latch = 256
    msu1_write_port(s, 1, 0x01);
    msu1_write_port(s, 2, 0x00);
    msu1_write_port(s, 3, 0x00);
    uint8_t buf[8];
    CHECK(msu1_read_data_bulk(s, buf, 8) == 8);
    CHECK(buf[0] == (uint8_t)(256 & 0xFF));
    CHECK(s.data_pos == 264);
    fclose(s.data_file);
}

TEST_CASE("bulk read: disabled or missing file reads nothing")
{
    Msu1State s = {};
    uint8_t buf[4];
    CHECK(msu1_read_data_bulk(s, buf, 4) == 0);       // enabled=false, no file
    s.enabled = true;
    CHECK(msu1_read_data_bulk(s, buf, 4) == 0);       // no file
}
```

- [ ] **Step 2: Run to verify failure** — `make -C tests` → link/compile error: `msu1_read_data_bulk` undeclared.

- [ ] **Step 3: Implement**

`msu1.h` (below `msu1_read_port` declaration):

```cpp
// Reads up to count bytes from the data track at data_pos with one fread.
// Returns bytes actually read (short at EOF, 0 when disabled or no data
// file); advances data_pos by the return value. No status flags change.
uint32_t msu1_read_data_bulk(Msu1State& state, uint8_t* dst, uint32_t count);
```

`msu1.cpp` (below `msu1_read_port`):

```cpp
uint32_t msu1_read_data_bulk(Msu1State& state, uint8_t* dst, uint32_t count)
{
    if (!state.enabled) { return 0; }
    if (state.data_file == nullptr) { return 0; }
    if (state.data_pos >= state.data_size) { return 0; }
    uint32_t remaining = state.data_size - state.data_pos;
    if (count > remaining) { count = remaining; }
    size_t got = fread(dst, 1, count, state.data_file);
    state.data_pos += (uint32_t)got;
    return (uint32_t)got;
}
```

- [ ] **Step 4: Run to verify pass** — `make -C tests` → all green.
- [ ] **Step 5: Commit** — `git add source/Snes9x/msu1.h source/Snes9x/msu1.cpp tests/test_msu1_data_bulk.cpp tests/Makefile && git commit -m "msu1: bulk data-track read (one fread per span)"` + trailer.

### Task 2: msu1_dma_b_offset

**Files:**
- Modify: `source/Snes9x/msu1.h`, `source/Snes9x/msu1.cpp`
- Test: `tests/test_msu1_dma_offsets.cpp` (new; register in `tests/Makefile`)

**Interfaces:**
- Produces: `uint8_t msu1_dma_b_offset(uint8_t transfer_mode, uint32_t byte_index);` — used by Task 3.

- [ ] **Step 1: Write the failing test**

```cpp
#include "doctest.h"
#include "msu1.h"

TEST_CASE("DMA B-bus offset table, modes 0-7 over 8 indexes")
{
    // SNES DMA write patterns (snes.nesdev.org/wiki/DMA):
    // 0: B          1: B,B+1      2: B,B        3: B,B,B+1,B+1
    // 4: B..B+3     5=1, 6=2, 7=3 (mirrors)
    static const uint8_t expected[8][8] = {
        {0,0,0,0,0,0,0,0},          // mode 0
        {0,1,0,1,0,1,0,1},          // mode 1
        {0,0,0,0,0,0,0,0},          // mode 2
        {0,0,1,1,0,0,1,1},          // mode 3
        {0,1,2,3,0,1,2,3},          // mode 4
        {0,1,0,1,0,1,0,1},          // mode 5 = 1
        {0,0,0,0,0,0,0,0},          // mode 6 = 2
        {0,0,1,1,0,0,1,1},          // mode 7 = 3
    };
    for (int mode = 0; mode < 8; mode++) {
        for (uint32_t i = 0; i < 8; i++) {
            CAPTURE(mode); CAPTURE(i);
            CHECK(msu1_dma_b_offset((uint8_t)mode, i) == expected[mode][i]);
        }
    }
}
```

- [ ] **Step 2: Run to verify failure** — `make -C tests` → `msu1_dma_b_offset` undeclared.

- [ ] **Step 3: Implement**

`msu1.h`:

```cpp
// B-bus register offset of the i-th byte of a DMA transfer for a given
// transfer mode (0-7; 5-7 mirror 1-3). Matches the SNES DMA write patterns.
uint8_t msu1_dma_b_offset(uint8_t transfer_mode, uint32_t byte_index);
```

`msu1.cpp`:

```cpp
uint8_t msu1_dma_b_offset(uint8_t transfer_mode, uint32_t byte_index)
{
    switch (transfer_mode & 0x7) {
        case 1: case 5: return (uint8_t)(byte_index & 1);
        case 3: case 7: return (uint8_t)((byte_index >> 1) & 1);
        case 4:         return (uint8_t)(byte_index & 3);
        default:        return 0;   // modes 0, 2, 6 write one register
    }
}
```

- [ ] **Step 4: Run to verify pass** — `make -C tests`.
- [ ] **Step 5: Commit** — `git add source/Snes9x/msu1.h source/Snes9x/msu1.cpp tests/test_msu1_dma_offsets.cpp tests/Makefile && git commit -m "msu1: DMA B-bus offset helper for transfer modes 0-7"` + trailer.

### Task 3: DMA integration

**Files:**
- Modify: `source/Snes9x/dma.cpp:121-135` (the existing MSU-1 branch in `S9xDoDMA`)

**Interfaces:**
- Consumes: `msu1_read_data_bulk`, `msu1_dma_b_offset` (Tasks 1-2), existing `msu1_is_dma_source`, `S9xMSU1ReadPort`, `S9xSetPPU`, `MSU1` global.

- [ ] **Step 1: Replace the branch body**

```cpp
	// MSU-1: DMA with a fixed A-bus source in $2000-$2007 (e.g. $2001 -> VRAM).
	// The generic path below resolves the A-address through memory-map base
	// pointers, which do not exist for register space. The data port gets a
	// bulk fread per span; other ports keep the per-byte register path. The
	// B-bus follows the transfer-mode write pattern (FMV uses mode 1).
	if (Settings.MSU1 && !d->TransferDirection &&
		msu1_is_dma_source ((uint8_t) d->ABank, (uint16_t) d->AAddress, (bool) d->AAddressFixed))
	{
		if ((d->AAddress & 0x7) == 1)
		{
			uint8 span[512];
			int done = 0;
			while (done < count)
			{
				int chunk = count - done;
				if (chunk > (int) sizeof(span)) chunk = (int) sizeof(span);
				uint32 got = msu1_read_data_bulk (MSU1, span, (uint32) chunk);
				if ((int) got < chunk)
					memset (span + got, 0, chunk - got);   // past-EOF reads are 0x00
				for (int i = 0; i < chunk; i++)
					S9xSetPPU (span[i], 0x2100 + d->BAddress +
					           msu1_dma_b_offset ((uint8) d->TransferMode, (uint32) (done + i)));
				done += chunk;
			}
		}
		else
		{
			for (int i = 0; i < count; i++)
			{
				Work = S9xMSU1ReadPort ((uint8) (d->AAddress & 0x7));
				S9xSetPPU (Work, 0x2100 + d->BAddress +
				           msu1_dma_b_offset ((uint8) d->TransferMode, (uint32) i));
			}
		}
		CPU.Cycles += (count + 1) * SLOW_ONE_CYCLE;
		S9xUpdateAPUTimer();
		goto update_address;
	}
```

- [ ] **Step 2: Host suite still green** — `make -C tests` (dma.cpp is not host-compiled; this checks Tasks 1-2 didn't regress).
- [ ] **Step 3: Docker build** — `docker run --rm --platform linux/amd64 -v "$PWD":/snes9x_3ds -w /snes9x_3ds devkitpro/devkitarm make release` → `.3dsx`/`.cia` built clean.
- [ ] **Step 4: Commit** — `git add source/Snes9x/dma.cpp && git commit -m "msu1: honor DMA transfer modes + bulk reads for the data port"` + trailer.

### Task 4: Azahar validation

**Files:** none (validation).

- [ ] **Step 1: Baseline** — launch `/Applications/Azahar.app/Contents/MacOS/azahar output/snes9x_3ds.3dsx`; load Super Road Blaster from `sdmc/snes/`; confirm boot.
- [ ] **Step 2: FMV check** — start a level: video must animate (pre-fix symptom: black/garbage video, music alone). Note audio sync.
- [ ] **Step 3: Regression** — MMX3 MSU-1: music still plays; Chrono Trigger MSU-1 boots.
- [ ] **Step 4: Push** — `git push origin feature/msu1`; hand `.cia` to user for New 3DS hardware validation.
