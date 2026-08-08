# Menu and UI System

[← Back to index](README.md)

## Split-screen model

* **Bottom (second) screen**: the menu, **software-rendered** into an RGB565 framebuffer by the `3dsui` rasterizer while in the pause menu.
* **Top (game) screen**: simultaneously shows the GPU-drawn animated parallax splash (no ROM loaded) or a dimmed paused game frame.
* Intended pipeline per menu frame: input → splash (GPU, top) → menu/dialog (CPU, bottom) → sync at VBlank.

## Menu framework (`3dsmenu`)

* Data model: `SMenuTab` → `SMenuItem` with types `Header1/2`, `Textarea`, `Action`, `Checkbox`, `Radio`, `Gauge`, `Picker` (nested picker items), each with a `std::function<void(int)>` change callback. 14 visible rows.
* Menu definitions live in `3dsmain.cpp` (see [Platform Layer](platform-layer.md)); tabs rebuild lazily via `settings3DS.menuTabDirty[]`.
* Input loop (`menu3dsMenuSelectItem`): START resumes the game, B = cancel/parent-dir/tab-left, X = file context menu, L/R or D-pad = tab switch / gauge adjust, Y+Up/Down = page jump, key-repeat after 15 frames. Battery state is queried once per menu entry (not per frame).
* Chrome: top bar, tab strip with even pixel distribution, cheats-active indicator, hand-drawn battery widget, bottom button hints. Slide-in animations for menus/dialogs and 3-step tab slide transitions.
* Dialogs: message dialogs, confirmation dialogs, and a ROM-loading dialog that can host a thumbnail.
* `menu3dsShowSplashMessage()` draws directly to the framebuffer before the GPU is initialized ("Loading", "Saving & Exiting").

## Software rasterizer (`3dsui`)

* Framebuffer addressing is column-major and vertically flipped (`offset = x * 240 + (239 - y)`).
* Viewport/translate state with a push/pop stack; precomputed alpha tables for RGB565 blending; checkerboard fill (RetroArch theme).
* Text rendering to framebuffer (menus) or to texture staging (notifications), with word-wrapping variants.
* Three baked bitmap fonts in `3dsfont.cpp` (Tempesta, Ronda, Arial), 13 px height, 256-entry width tables + 64 KB bitmaps, selected by `settings3DS.Font`.
* A shared 512 KB `g_texUploadBuffer` in linear memory is used (non-concurrently) by UI image and notification uploads.

## Themes (`3dsthemes`)

Three themes — **Dark mode** (default), **RetroArch-style** (checkerboard background, `>` cursor), **Original** (blue/white) — each a table of 20 colors + 2 alphas.

## Images, splash, thumbnails (`3dsui_img`)

* UI textures: `UI_OVERLAY` (512×256 RGBA8), `UI_BG_GAME` / `UI_BG_SECOND` (512×256 RGB565), `UI_SPLASH` (t3x atlas, 4 subtextures), `UI_SCANLINE` (64×64 RGBA4, generated).
* Defaults come from romfs; users can override with `sdmc:/3ds/snes9x_3ds/{overlays,backgrounds/*}/_default.png` and per-game PNGs. An `AssetMode` policy (None / Default / Adaptive / CustomOnly) plus a `customLoadFailed` flag prevent re-probing the SD card every frame.
* PNG loading: libpng decode into `g_fileBuffer`, format conversion into `g_texUploadBuffer`, then a `C3D_SyncDisplayTransfer` with tiled output — the GPU does the swizzle.
* **Splash screen**: two parallax layers + logo with gradient drop shadows and sinusoidal logo bob; animation is time-based (`osGetTime()` delta) so the stereo right-eye pass doesn't double the speed; layer scale/speed react to the 3D slider.
* **Bezel overlay** auto-fit scales against a 320×239 inner window; **scanlines** are an odd-row alpha texture (intensity 1-8) drawn as a repeating quad.
* **Thumbnails**: read on demand from single `.cache` pack files (format in [Settings and Storage](settings-and-storage.md)); pixels are pre-swizzled so drawing is a per-column `memcpy` into the framebuffer. The second screen shows boxart/title/gameplay for the selected ROM, or the save-state PNG preview on the Emulator tab. Thumbnail loads are deferred until the slide animation finishes to avoid stutter.

## Notifications (`3dsui_notif`)

* Small GPU-drawn toasts: save/load state, saving-in-progress, slot changed, controller swapped, screenshot, fast-forward, broken-audio warning, paused, plus an FPS overlay (top-left, re-uploaded only when the text changes).
* Text is rasterized into a dedicated RGBA4 texture; a 2×2 white block planted in the corner lets the background rectangle and the text draw from the same texture in one batch.
* Types (Success/Error/Warning/Info) map to fixed background colors at 85% alpha. "Paused" is a persistent full-width overlay drawn together with a dim rectangle over the game.
