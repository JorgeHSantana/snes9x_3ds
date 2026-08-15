# Code Audit — Defects, Redundancy and Dead Code

[← Back to index](README.md)

Source-level audit of `snes9x_3ds` at commit `3eafe3b`, covering the 3DS platform layer (`source/3ds*`, ~30,450 lines). Distinct from [`KNOWN_ISSUES.md`](../KNOWN_ISSUES.md), which catalogues *game compatibility*; this document is about the code.

Every finding below was verified by reading the surrounding code, not inferred from a pattern match. Findings that a scan flagged but that turned out to be correctly guarded are listed in [§5](#5-checked-and-clean) so the same ground is not re-walked.

**Headline:** the platform layer is in good shape. There is **one** genuine logic bug, **two** resource-ownership violations of the project's own coding standard, and **two** worthwhile deduplication targets. No memory-safety defect was found in code written by this fork.

---

## 1. Logic bugs

### 1.1 Cheat-name prettifying is computed and thrown away

**`source/3dsmain.cpp:1599-1608`** · severity: low (cosmetic + wasted work) · confidence: certain

`makeCheatMenu()` builds a title-cased copy of an ALL-CAPS cheat name, then formats the **original** into the menu label:

```cpp
std::string name = Cheat.c[i].name;
if (utils3dsIsAllUppercase(Cheat.c[i].name)) {
    for (size_t j = 1; j < name.length(); j++) {
        if (std::isalpha(name[j])) {
            name[j] = std::tolower(name[j]);      // writes into `name`
        }
    }
}

snprintf(buffer, sizeof(buffer), "  %s", Cheat.c[i].name);   // ← reads the ORIGINAL
```

`name` is never read after the loop. Effects:

* The feature silently does nothing — `INFINITE LIVES` still renders as `INFINITE LIVES` rather than `Infinite lives`.
* A `std::string` heap allocation and a full character scan are performed per cheat, per menu rebuild, for no result.

**Fix:** `snprintf(buffer, sizeof(buffer), "  %s", name.c_str());`

`-Wunused-variable` does not catch this because `name` *is* used — it is assigned to. `-Wunused-but-set-variable` does not fire for non-trivial class types.

---

## 2. Resource ownership

Both findings below violate [`CODING_STANDARD.md`](CODING_STANDARD.md) §4:

> *"Each `FILE*` (or similar resource) has exactly one owning struct; every teardown path (ROM switch, reset, exit, error) closes it. **A leak is a test failure.**"*

Neither is a *runtime* leak — both are allocate-once, and the OS reclaims them at exit. They are structural: the memory cannot be reclaimed by the process, and there is no teardown path to extend if a future feature needs the space back.

### 2.1 The rewind ring is never freed — up to 24 MB

**`source/3dsrewind.cpp:31-50`** · severity: medium (structural) · confidence: certain

```cpp
static void rewind3dsAllocate()
{
    int slots = settings3DS.isNew3DS ? REWIND_SLOTS_NEW3DS : REWIND_SLOTS_OLD3DS;   // 48 : 8
    while (slots >= 2) {
        uint8_t *pool = (uint8_t *)malloc((size_t)slots * REWIND_SLOT_SIZE);        // 512 KB/slot
        if (pool) {
            uint32_t *lens = (uint32_t *)malloc(slots * sizeof(uint32_t));
            …
            s_ring.init(pool, lens, slots, REWIND_SLOT_SIZE);
            return;
        }
        slots /= 2;
    }
}
```

`48 × 512 KB = 24 MB` on New 3DS, 4 MB on Old 3DS. Neither `pool` nor `lens` is ever freed — **there is no `rewind3dsFinalize()`**; [`3dsrewind.h`](../source/3dsrewind.h) declares only `rewind3dsFrameTick()` and `rewind3dsReset()`, and `rewind3dsReset()` only calls `s_ring.clear()`.

This matters more than the byte count suggests: 24 MB is a large fraction of the 3DS heap, and the upstream `enhanced-resolution` work (512-wide targets, 800 px wide mode) competes for exactly that budget.

**Fix:** add `void rewind3dsFinalize()` that frees both blocks, resets `s_allocTried`, and call it from `emulatorFinalize()` next to the other teardown calls.

### 2.2 The MSU-1 prefetch ring is never freed — 512 KB

**`source/3dsmsu_ndsp.cpp:99-107` vs `:116-121`** · severity: low · confidence: certain

`msu3dsNdspInstall()` allocates three blocks; `msu3dsNdspUninstall()` frees two:

```cpp
bool msu3dsNdspInstall(void) {
    if (g_pcm_block == nullptr)     g_pcm_block     = (uint8_t*)linearAlloc(…);   // freed ✓
    if (g_staging == nullptr)       g_staging       = (int16_t*)linearAlloc(…);   // freed ✓
    if (g_prefetch_ring == nullptr) g_prefetch_ring = (uint8_t*)malloc(PREFETCH_RING_BYTES);  // 512 KB — never freed ✗
    …
}

void msu3dsNdspUninstall(void) {
    msu3dsFinalize();
    if (g_staging   != nullptr) { linearFree(g_staging);   g_staging   = nullptr; }
    if (g_pcm_block != nullptr) { linearFree(g_pcm_block); g_pcm_block = nullptr; }
    // g_prefetch_ring is missing
}
```

The asymmetry is easy to miss because the two `linearAlloc` blocks *are* correctly paired — the odd one out is the `malloc`.

**Fix:**
```cpp
if (g_prefetch_ring != nullptr) { free(g_prefetch_ring); g_prefetch_ring = nullptr; }
```
Note this must run *after* `msu3dsFinalize()`, since the prefetch thread holds the pointer.

---

## 3. Duplicated code

### 3.1 Menu cursor movement — UP and DOWN are copy-paste twins

**`source/3dsmenu.cpp:1051-1082` and `:1085-1114`** · ~30 lines duplicated

The two blocks differ only in direction and wrap behaviour. The 6-line skip predicate is **byte-for-byte identical** in both:

```cpp
while (
    (currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Disabled ||
     currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Header1 ||
     currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Header2 ||
     currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Textarea
    ) &&
    moveCursorTimes < currentTab->MenuItems.size());
```

**Refactor:** extract the predicate, which is the part most likely to drift:

```cpp
static inline bool menu3dsIsSelectable(const SMenuItem& item) {
    return item.Type != MenuItemType::Disabled
        && item.Type != MenuItemType::Header1
        && item.Type != MenuItemType::Header2
        && item.Type != MenuItemType::Textarea;
}
```

Then a single `menu3dsMoveCursor(SMenuTab*, int delta, bool pageJump)` covers both directions. Adding a fifth non-selectable item type currently means remembering to edit two places.

One asymmetry worth preserving deliberately if you refactor: the DOWN block also resets `currentTab->FirstItemIndex = 0` when it wraps to the top; the UP block does not reset it when wrapping to the bottom. That is not obviously intentional — `MakeSureSelectionIsOnScreen()` presumably corrects it, but it is the kind of difference that a merge would silently erase.

### 3.2 Stereo profile rename — the same swkbd block twice

**`source/3dsmain.cpp:1144-1152` and `:1163-1171`** · ~8 lines duplicated

The same software-keyboard sequence appears in the profile picker's change callback and again in the standalone "Rename Profile" action:

```cpp
SwkbdState swkbd;
char buf[16];
swkbdInit(&swkbd, SWKBD_TYPE_QWERTY, 1, sizeof(buf) - 1);
swkbdSetInitialText(&swkbd, p->Name);
swkbdSetHintText(&swkbd, "Profile name");
if (swkbdInputText(&swkbd, buf, sizeof(buf)) == SWKBD_BUTTON_CONFIRM && buf[0] != '\0') {
    snprintf(p->Name, sizeof(p->Name), "%s", buf);
}
```

The copies have already diverged: the second sets `settings3DS.isDirty = true` and the first does not. Whether that is intentional is not obvious from the code — which is exactly the problem.

**Refactor:** `static bool promptProfileName(S9xSettings3DS::SStereoProfile *p)` returning whether the name changed, with the caller deciding about `isDirty`.

---

## 4. Latent risks

Not bugs today; each is a trap with an unguarded edge.

### 4.1 `_splitpath` / `_makepath` are unbounded

**`source/3dsimpl.cpp:1350-1407`** · inherited from upstream Snes9x

Six `strcpy` and four `strcat` calls with no length parameter:

```cpp
void _splitpath(const char *path, char *drive, char *dir, char *fname, char *ext) {
    …
    strcpy(dir, path);
    strcpy(fname, slash + 1);
    strcpy(ext, dot + 1);
}
```

Safe **only** because every caller passes `_MAX_PATH`-sized buffers and the input originates from the SD card path, itself bounded by the filesystem. But nothing in the signature says so, and a future caller with a smaller buffer gets no warning. If you touch this file, converting to `snprintf`-based helpers with explicit sizes is cheap insurance.

### 4.2 `RewindRing::valid()` does not check `lengths`

**`source/3dsrewindring.h:35`**

```cpp
bool valid() const { return pool != NULL && slots > 0; }
```

`pushCommit()` writes `lengths[head]` and `popPeek()` reads `lengths[idx]` without any null check. The sole caller allocates both together and frees `pool` if `lens` fails ([`3dsrewind.cpp:40`](../source/3dsrewind.cpp#L40)), so the invariant holds today. Given the header's own comment says it is *"shared with the host test suite"*, a test could construct the invalid state:

```cpp
bool valid() const { return pool != NULL && lengths != NULL && slots > 0; }
```

### 4.3 Rewind capture cadence drifts after a rewind burst

**`source/3dsrewind.cpp:72, 90-91, 126`**

`s_frameCounter` serves double duty: the capture cadence (`REWIND_CAPTURE_FRAMES = 30`) and the pop cadence while the hotkey is held (`REWIND_STEP_FRAMES = 10`). Releasing the hotkey leaves the counter anywhere in `0..9`, so the first snapshot after a rewind burst is taken up to 10 frames early. Cosmetic — the ring self-corrects — but if snapshot spacing ever becomes load-bearing (say, for a rewind timeline UI), separate the counters.

---

## 5. Checked and clean

Scans flagged these; reading the code showed each is correctly guarded. Recorded so the audit is not repeated.

| Area | Concern | Why it is fine |
|---|---|---|
| `3dsmain.cpp:2492-2500` | `RandomGame` computes `minIndex = entries.size() - romCount`; with 0 ROMs `min > max`, and `utils3dsGetRandomInt` returns `min` → out-of-bounds `MenuItems[min]` | The option is only offered when `file3dsGetCurrentDirRomCount() > 1` ([`:287`](../source/3dsmain.cpp#L287)), which guarantees `min <= max - 1` |
| `3dstimer.cpp:76` | `totalMs / totalFrames` | `if (totalFrames <= 0) totalFrames = 1;` immediately above; the `calls == 0` case branches away earlier |
| `3dsconfig.cpp` | positional `fscanf` parsing | Checks the return value, logs a mismatch once, keeps defaults for the remainder, and the file is **versioned** (`config3dsGetVersionFromFile`). Contrast [emus3ds, which does none of this](../../emus3ds/docs/developer-gotchas.md#3dsconfig-format-string-and-off-by-one) |
| `3dsmenu.cpp:1068, 1094` | `MenuItems.size() - 1` on a `size_t` would wrap if the tab were empty | No tab can be empty — `makeCheatMenu()` always emplaces at least a `Textarea` ([`3dsmain.cpp:1640`](../source/3dsmain.cpp#L1640)), and the other builders always add headers |
| `3dsmain.cpp:1146, 1164` | `char buf[16]` for profile names | `swkbdInit(…, sizeof(buf) - 1)` and `swkbdInputText(…, sizeof(buf))` are both bounded; `snprintf` into `p->Name` uses `sizeof` |
| all `memset`/`memcpy` | `sizeof` misuse | Every call in the platform layer uses `sizeof(<real array or struct>)`. None uses `sizeof` of an expression |
| `linearAlloc` pairing | leaks | Balanced in `3dsgpu`, `3dsimpl_gpu`, `3dssound`, `3dsui`, `3dsui_img`. Only `3dsmsu_ndsp` is unbalanced — see [§2.2](#22-the-msu-1-prefetch-ring-is-never-freed--512-kb) |
| `fopen`/`fclose` pairing | leaks | Balanced in every platform-layer file |
| pointer-vs-literal comparison | `if (str == "literal")` | None in this codebase. (emus3ds [has one](../../emus3ds/docs/developer-gotchas.md#setlanguage-compares-pointers)) |
| assignment inside `if` | `if (x = y)` | None found |

---

## 6. Known dead code

Catalogued in [Developer Gotchas](developer-gotchas.md#dead-code-and-misleading-declarations); repeated here in one place because dead code is what an audit is expected to list.

| Item | Location | Note |
|---|---|---|
| The entire software rasteriser | `Snes9x/gfx.cpp` — `RenderScreen`, `DrawBackground*`, `DrawOBJS`, software Mode 7 | Only `gfxhw.cpp` renders |
| Software tile blitters | `Snes9x/tile.cpp` | `ConvertTile` is live; the blitters are not |
| `S9xUpdateScreenSoftware` | declared, **never defined** | |
| `settings3dsApplyScreenLayout()` | declared in `3dssettings.h`, never defined | the real one is `ui3dsSetScreenLayout()` in `3dsui.cpp` |
| Gold Finger cheat decoder | `Snes9x/cheats.cpp` | implemented, not wired into any loader |
| Compile-time features never defined | `CPU_SHUTDOWN`, `SPC700_SHUTDOWN`, `DEBUGGER`, `ZSNES_FX`, `MK_APU`, `CORRECT_VRAM_READS`, `UNZIP_SUPPORT` | their branches are dead |
| `source/problems.txt` | 2016-era upstream bug journal | not a build input |
| `3dstimer` profiling | compiled to nothing unless `PROFILING_DISABLED` is undefined | intentional; remember to re-enable before profiling |

---

## 7. Suggested order

1. **[§1.1](#11-cheat-name-prettifying-is-computed-and-thrown-away)** — one-line fix, restores an intended feature.
2. **[§2.1](#21-the-rewind-ring-is-never-freed--up-to-24-mb)** and **[§2.2](#22-the-msu-1-prefetch-ring-is-never-freed--512-kb)** — add the two teardown paths. Small, and they bring the code back in line with its own standard.
3. **[§4.2](#42-rewindringvalid-does-not-check-lengths)** — one-token change.
4. **[§3.1](#31-menu-cursor-movement--up-and-down-are-copy-paste-twins)** — the refactor with the best ratio of risk reduction to effort; do it when you next touch the menu.
5. **[§3.2](#32-stereo-profile-rename--the-same-swkbd-block-twice)**, **[§4.1](#41-_splitpath--_makepath-are-unbounded)**, **[§4.3](#43-rewind-capture-cadence-drifts-after-a-rewind-burst)** — opportunistic.

Per [`CODING_STANDARD.md`](CODING_STANDARD.md) §8, each fix in §1 and §2 should ship with a host test: a cheat-name formatting case, and an init→teardown cycle asserting every allocation is released.
