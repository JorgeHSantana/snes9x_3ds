# Hardware-Accelerated Rendering Pipeline

[← Back to index](README.md)

This is the project's defining subsystem: the SNES PPU compositing model is reproduced with PICA200 GPU features via citro3d. The core side lives in `source/Snes9x/gfxhw.cpp`; the platform side in `source/3dsgpu.cpp`, `source/3dsimpl_gpu.cpp`, `source/3dsimpl_tilecache.cpp` and the `.pica` shaders.

## Render-state cache (`3dsgpu`)

* **`SGPURenderState`** — a 64-bit packed union of the full render state (stencil 32b, texture bind 5b, target 3b, blending 4b, texenv 4b, alpha test 4b, texture offset 2b, depth test 2b, shader 2b).
* `gpu3dsApplyRenderState()` XORs current vs applied and reconfigures **only the changed sub-states** — the central draw-call batching optimization introduced in v1.60.
* `C3D_Init` with a 1 MB command buffer (heavy HDMA/window scenes need it).
* Screen targets: the top-left target is allocated at 240×800 (wide-mode size); the **right-eye target aliases the second half of the same buffer** (3D and wide mode are mutually exclusive by construction — saves ~281 KB of VRAM).
* Emulator detection: `svcGetSystemInfo(0x20000, 0)` distinguishes real hardware from Citra/Azahar (`GPU3DS.isReal3DS`); several workarounds key off this.
* Blending modes map SNES color math directly: additive, subtractive (`REVERSE_SUBTRACT`), and half-luminance `_DIV2` variants using the blending color and `ONE_MINUS_DST_ALPHA`.
* Double-buffered VBOs: each vertex list flips between two halves per frame (`gpu3dsPrepareListForNextFrame`) so the CPU writes frame N+1 while the GPU reads frame N.

## Textures and vertex buffers

| Resource | Size / format | Purpose |
|---|---|---|
| `SNES_MAIN` | 512×256 RGBA8 (VRAM) | main screen render target |
| `SNES_SUB` | 512×256 RGBA8 (VRAM) | sub screen render target |
| `SNES_DEPTH` | 512×256 (VRAM) | **shared** depth/stencil for MAIN+SUB |
| `SNES_TILE_CACHE` | 1024×1024 RGBA5551 (linear RAM) | decoded 8×8 SNES tiles (CPU-written atlas) |
| `SNES_MODE7_FULL` | 1024×1024 RGBA5551/RGBA4 (VRAM) | the fully baked Mode 7 playfield |
| `SNES_MODE7_TILE_CACHE` | 128×128 (linear RAM) | decoded Mode 7 characters |
| `SNES_MODE7_TILE_0` | 16×16, `GPU_REPEAT` | tile-0 repeat mode (`Mode7Repeat == 3`) |

VBOs: `VBO_SCENE_TILE` (tiles: `s16` pos ×3 + `s16` tex ×2), `VBO_SCENE_RECT` (backdrop/color-math/brightness/window rectangles), `VBO_SCENE_MODE7_LINE` (one line-quad per Mode 7 scanline), `VBO_MODE7_TILE` (Mode 7 playfield baking), `VBO_SCREEN` (UI/final blit quads). All double-buffered.

## Tile cache (`3dsimpl_tilecache`)

* Hash: `(vramAddr << 4) + palette` → direct-mapped position in the 1024×1024 atlas.
  * 16383 slots total; the top 4096 are reserved for **HDMA palette variants**.
  * Slots are allocated in pairs because slot `p^1` is the **alt-frame slot**, used when a game rewrites tile bitmaps mid-frame — fixes DKC2 sprite flicker.
* `cache3dsCacheSnesTileToTexturePosition()` writes the 64 pixels of a tile **directly in the PICA Morton/swizzled order** (fully unrolled), so no post-pass tiling is needed. Palette index 0 always writes 0 (transparent).
* Invalidation: VRAM writes clear `IPPU.TileCached[]` bits; palette changes bump `GFX.PaletteFrame*` counters compared against per-tile stamps (`GFX.VRAMPaletteFrame`, 0.5 MB). On eviction the previous occupant's palette frame is zeroed to force re-decode (fixes long-session tile corruption).

## Shaders

Assembled by picasso (see [Build System](build-system.md)); uniforms `projection`, `textureScale`, `textureOffset`, `updateFrame` shared across programs.

### `shader_screen` (vertex only)

Trivial projected textured quad — used for all UI quads and the final screen blit from `VBO_SCREEN`.

### `shader_tiles` (vertex + geometry, stride 6)

The workhorse: a SNES 8×8 tile costs **2 vertices**; the geometry shader expands the pair into a quad (4-vertex strip). Vertex attributes are bit-packed:

```
x = signed X position (−256..511)
y = 0aa0dddd yyyyyyyy   (2 bits alpha class, 4 bits depth, 8 bits Y)
z = vhtttttt tttttttt   (V-flip 0x8000, H-flip 0x4000, 14-bit tile atlas slot)
```

The vertex shader decodes flips, the atlas slot → pixel coordinates in the 1024×1024 tile cache, the alpha class (1 / 255 / 128) and the depth.

The same program also draws **Mode 7 scanline spans**: a sentinel `y1 = -16384` from `gfxhw.cpp` routes the geometry shader to a 1-scanline-tall emission path (height 0.00395 — exactly 1/256 would make the scanline disappear).

### `shader_mode7` (vertex + geometry, stride 3)

Bakes the 1024×1024 `SNES_MODE7_FULL` playfield from the 128×128 Mode 7 character cache. One vertex per tile; the **dirty test runs on the GPU** — the vertex shader kills any vertex whose `Position.w` stamp is older than the `updateFrame` uniform, so all ~16 K vertices are submitted every bake but only changed tiles rasterize. The playfield is baked in 4× 512×512 sections (PICA can't target render viewports wider than 512); the render target's color buffer pointer is repointed per section.

## Layer batching and draw order (`3dsimpl_gpu` + `gfxhw`)

`gfxhw.cpp` emits geometry into 9 layers, each with sections per target (0 = MAIN, 1 = SUB):

```
LAYER_BG0..BG3, LAYER_OBJ, LAYER_BACKDROP, LAYER_COLOR_MATH, LAYER_BRIGHTNESS, LAYER_WINDOW_LR
```

Draw order per frame (`gpu3dsDrawSnesScreen`): `WINDOW_LR` rendered into the **stencil buffer** first, then the SUB target, then MAIN, in `{BACKDROP, OBJ, BG0..BG3, COLOR_MATH, BRIGHTNESS}` order.

SNES semantics → GPU features:

* **Priorities → depth**: priority becomes a Y-attribute offset encoding `depth × 256 + alpha class`; the depth test is enabled for BG layers.
* **Windows → stencil**: `cliphw.cpp` converts the two SNES window ranges into ≤5 x-sections with masks (`WIN1`, `WIN2`, `WIN1^2`); a 128-entry lookup maps the window enable/invert/logic register combinations to `{GPU_TESTFUNC, ref, mask}`. `$2130` clip-to-black / prevent-color-math modes fold into the backdrop's stencil function.
* **Color math → blending**: add / subtract / halved variants against destination alpha.
* **Mosaic → sampler**: one quad per S×S block with a single-texel UV span (the GPU sampler replicates the pixel). Offset-per-tile modes (2/4/6) and Mode 7 are not mosaicked.

Other mechanics:

* Per-layer section budgets (128 for BGs/OBJ, 1-2 for rect layers) are redistributed adaptively when a frame overflows (`gpu3dsAdjustLayerSectionLimits`); an overflowing frame is dropped rather than partially drawn.
* Batches break only when the masked render-state diff demands it; OBJ/BG2/BG3 sections only need stencil changes after the first section.
* A hardware bug forces an all-or-nothing choice between `DrawArrays` and `DrawElements` within the OBJ/BG pass — mixing them **freezes real hardware** (observed in Super Mario Kart).
* Hi-res modes 5/6 have dedicated draw paths (with a texture-offset downsample trick); brightness-0 vertical sections trim entire render segments (`S9xTrimBlackScanlines`).
* Mode 7: one textured line-quad per scanline with per-line matrix math from `LineMatrixData[]`; EXTBG draws BG1 twice at two depths with an alpha-test priority split; a Citra workaround issues a dummy 16-byte texture copy to flush the baked surface.

## Final composition

`impl3dsSceneRender()` (in `3dsimpl.cpp`, see [Platform Layer](platform-layer.md)) draws `SNES_MAIN` to the top screen with stretch/crop/overscan/filter settings, plus the background image, scanline texture, bezel overlay and notifications — twice with ±IOD offsets when stereoscopic 3D is active. The menu system draws the bottom screen in software (see [Menu and UI](menu-ui.md)).
