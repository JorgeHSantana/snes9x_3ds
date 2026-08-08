# Build System

[← Back to index](README.md)

## Toolchain

* **devkitPro / devkitARM** (`DEVKITARM` env var is a hard requirement), libctru, citro3d, libpng, zlib.
* External tools in PATH: `tex3ds`, `smdhtool`, `3dsxtool`, `picasso` (implicit via `3ds_rules`). `makerom` is **bundled** in `makerom/` (v0.18.4, per-platform binaries with documented SHA-256 provenance in `makerom/BINARY_SOURCES.md`).
* **Custom citro3d**: by default (`USE_CUSTOM_CITRO3D=1`) the Makefile clones citro3d **v1.7.1** into `libs/citro3d`, applies `patches/citro3d-uniforms-maxdirty.patch` (tracks the high-water mark of dirty float uniforms so `C3D_UpdateUniforms` scans only up to that bound — a per-draw upload optimization), and builds it locally. Delete `libs/citro3d` to force a rebuild.

## Compiler flags

```
ARCH      = -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
COMMON    = -O3 -Wall -Wextra … -mword-relocations -fomit-frame-pointer -ffunction-sections
            -DVERSION_MAJOR/MINOR/MICRO -D__3DS__
CXXFLAGS += -fno-rtti -fno-exceptions -std=gnu++17
```

`-Werror` is on by default (`STRICT_WARNINGS=1`; set `STRICT_WARNINGS=0` to disable). `OPT_FLAGS ?= -g -O3` for dev builds; the `release` target rebuilds with plain `-O3`.

Version defines come from `resources/AppInfo` and are consumed at `source/3dssettings.h`.

## Source list

`CPPFILES` in the Makefile is an **explicit hand-maintained list** (~50 files), not a wildcard. **Adding a new `.cpp` requires editing the Makefile.** Some core files (`sa1cpuops.cpp`, `c4emu.cpp`…) are pulled in via `#include` rather than compiled standalone.

Auto-globbed: `source/*.s`, `source/*.v.pica`, `gfx/*.t3s`.

## Shaders and assets

* `source/*.v.pica` (+ matching `.g.pica` geometry shaders) are assembled by picasso via devkitARM's `3ds_rules` into `*_shbin.h` headers, `#include`d by `3dsimpl.cpp` and loaded with geometry-shader strides 0 (screen), 6 (tiles), 3 (mode7).
* `gfx/splash.t3s` → `tex3ds` → `romfs/gfx/splash.t3x` (ETC1 atlas of the 4 parallax splash layers; the index order in the `.t3s` is load-bearing). Note: `splash.t3x` is committed to git but `make clean` deletes it.
* romfs is attached via `--romfs` (3dsx) and `app.rsf`'s `RomFs:` (CIA). It carries `mappings.txt` (ROM-name aliases) and the default overlay/background PNGs.
* SMDH icon built by `smdhtool` from `resources/icon.png`; the CIA banner `resources/banner.bnr` is **prebuilt and committed** (no bannertool rule; `banner.png` is source art for reference).

## Targets

| Target | Produces |
|---|---|
| `make release` | `output/snes9x_3ds.3dsx` + `.cia` with `-O3` (what CI runs) |
| `make 3dsx` / `cia` / `3ds` / `elf` | individual artifacts |
| `make citra` | build + launch in Citra |
| `make 3dslink` | send to console at hardcoded `3DS_IP := 192.168.1.2` |
| `make clean` | removes `build/`, `output/`, `romfs/gfx/*.t3x` |

### Gotchas

* A bare `make` builds **only the citro3d dependency** (the `$(CITRO3D_LIB)` rule is the default goal) — use `make release` or a named target.
* The `release` target does **not** depend on `$(CITRO3D_LIB)`, so CI links the stock container citro3d, not the patched one.
* `TARGET := $(notdir $(CURDIR))` — renaming the checkout directory renames the output binary.
* `make clean` deletes the tracked `romfs/gfx/splash.t3x`.

## CIA packaging

`makerom` is invoked with `resources/app.rsf` as template and `resources/AppInfo` variables:

* `cia` target: `-f cia -DAPP_ENCRYPTED=false`; `3ds` target (CCI): `-f cci -DAPP_ENCRYPTED=true`.
* `app.rsf` highlights: 64 MB "Legacy" system mode, **804 MHz CPU with L2 cache** (New 3DS clock), `CanAccessCore2 true` (audio thread), kernel min 4.5.0, `DirectSdmc` FS access, direct hardware mappings for DSP memory (`1ff00000-1ff7ffff`) and VRAM (`1f000000-1f5fffff:r`), services incl. `dsp::DSP`, `gsp::Gpu`, `csnd:SND`.
