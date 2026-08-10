# MSU-1 Wave 2 Phase B — Data-Track Read-Ahead Design

Date: 2026-08-10. Trigger (per phase-A spec): hardware showed the need — Road Blaster scenes run ~14% slow because data-port freads execute on the emulation thread; SD latency stretches frames, audio (real-time 44.1kHz) finishes early, leaving multi-second silent tails (New 3DS log, session 19:22: track 5 = 29.9s of audio, scene took 34.9s).

## Goal

Move SD latency for MSU-1 data reads off the emulation thread: a prefetch producer on the existing syscore mixing thread fills a ring buffer; the emulation thread's DMA read becomes a memcpy on hit, falling back to direct fread on miss.

## Design

**Consumer hook in the msu1 core** (keeps core thread-free and host-testable):
- `msu1_set_data_prefetch(uint32_t (*read)(uint32_t pos, uint8_t* dst, uint32_t count))` — null default.
- `msu1_read_data_bulk`: clamp to remaining → try hook at `data_pos` → advance by hit; shortfall → `fseek(data_file, data_pos)` only when the shadow `data_file_pos` (new runtime field) diverges, then fread.
- Port-1 CPU reads route through `msu1_read_data_bulk(state, &b, 1)` so ring hits can never leave the FILE* position silently stale.
- Port-3 seeks keep both `data_pos` and `data_file_pos` in sync (unchanged behavior when no hook).

**Ring buffer in the bridge** (`3dsmsu.cpp`, host-testable):
- `msu3dsDataPrefetchInit(uint8_t* storage, uint32_t capacity)` — caller-provided storage (mirrors the staging pattern); null/0 disables.
- `msu3dsDataPrefetchLocks(lock, unlock)` — leaf lock hooks (LightLock on 3DS, null = no-op on host). Consumer never touches the mixer's long-held snesAccessLock.
- `msu3dsDataPrefetchRead(pos, dst, n)` — the consumer hook: serves from the ring when `pos` falls inside the buffered window; on miss, resets the window base to `pos` (generation bump) and returns 0.
- `msu3dsDataPrefetchFill()` — producer tick, called from `msu3dsFillAudio` (mixer thread): keeps its OWN `FILE*` on `<base>.msu`; on generation change, reseeks; freads into the free span OUTSIDE the ring lock (≤32 KB per tick), commits under it, discarding on generation mismatch.
- Events: `RomUnload` closes the producer file and resets; `SavestateLoaded`/`ConsoleReset` reset the window.

**Platform glue** (`3dsmsu_ndsp.cpp`): allocate 512 KB (≈1.7 s of Road Blaster data) + a dedicated LightLock at install; wire `msu3dsDataPrefetchLocks` + `Init`.

## Correctness invariants

- `data_pos` remains the single source of truth (savestates untouched).
- A ring hit is byte-identical to an fread at the same offset (producer reads the same file sequentially).
- Any seek/miss/reset degrades to phase-A behavior (direct fread) — never corruption, only latency.
- No new lock ordering: the ring lock is a leaf; nothing is acquired while holding it.

## Testing

Host doctest: ring window hit/miss/wraparound/reset-on-miss; producer fill from a fixture file incl. generation-mismatch discard; end-to-end через msu1 core — hook hit equals plain fread byte-for-byte, port-3 seek then read stays consistent; port-1 single reads after ring-served DMA see the right bytes (the stale-FILE*-position trap).

## Validation

Azahar: Road Blaster autoboot — video plays, log clean (no stalls/underruns). Hardware (user): silent tails between scenes should shrink or vanish; log timing of "track N ended" vs next "control write 01" measures residual slowdown.
