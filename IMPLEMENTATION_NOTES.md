# Implementation: No-Internet Toast + Ease-In Block Dissolve Transition

## Build Status
✅ **SUCCESS** — Compiled and flashed to ESP32 on 2026-04-04 (revised 2026-04-04 with ease-in quadratic timing)

## Summary of Changes

### 1. No-Internet Toast Notification

**Goal**: Show a visual indicator when HTTP downloads fail, displaying a countdown to retry.

**Location**: `src/gif_player.h` (4 new private members) + `src/gif_player.cpp` (new methods + integration)

**Changes**:
- Added private members to `GifPlayer` class:
  - `_toastShownMs`, `_toastVisible`, `drawToast()`, `clearToast()`
- Implemented `drawToast()`: maroon background, white border, yellow `[!]` icon, white countdown text at y=185-218 (above status bar)
- Implemented `clearToast()`: erases toast rect with black fill
- Integrated into `fetchAndPlay()`: calls `drawToast()` immediately after setting `_retryAfterMs = millis() + 10000` on both failure paths (API failure and download failure)
- Integrated into `tick()`: refreshes toast countdown every ~1000ms for smooth animation
- Integrated into `openAndPlayFile()`: calls `clearToast()` after successful GIF load

**Visual Result**: When WiFi is down or a GIF URL fails, a maroon banner appears above the status bar showing `[!] No internet  10s`, counting down to next retry attempt. Banner automatically dismisses on successful download.

---

### 2. Block Dissolve Transition with Ease-In Quadratic Timing

**Goal**: Random block-by-block GIF transitions that start slowly (a few random squares) then accelerate — like pixelated video glitch/buffering effects where blocks fill in faster and faster.

**Location**: `src/gif_player.cpp` (lines 31–52, 369–386, 286–305, 576–578, 863–900)

**Changes**:

#### 2a. Static State (lines 31–52)
- **Restored**: `s_blockOrder[900]`, `s_blockRevealed[113]`, `DISSOLVE_*` constants, `blockSetRevealed()`/`blockIsRevealed()` inline functions
- **Constants**:
  - `DISSOLVE_DURATION_MS = 2000` (2-second ease-in transition)
  - `DISSOLVE_BLOCKS = 900` (30×30 grid of 8×8 pixel blocks)
  - `DISSOLVE_BLOCK_SIZE = 8`
- **State**:
  - `s_blockOrder[900]` — Fisher-Yates shuffled reveal order (randomizes which blocks appear first)
  - `s_blockRevealed[113]` — bitmask of which blocks are currently revealed
  - `s_transitionStep` — current count of revealed blocks (0 to 900)
- **RAM**: ~1.9 KB (blocks + bitmask structures)

#### 2b. `startTransition()` (lines 369–386)
- Initializes block order 0..899 sequentially
- Fisher-Yates shuffles using hardware RNG for randomization
- Clears bitmask (no blocks revealed initially)
- Arms `s_inTransition`, records start time
- No pre-reveal; starts from 0 blocks for clean ease-in curve from old GIF

#### 2c. `tick()` advancement (lines 286–305)
- **Ease-in quadratic timing**: `targetStep = DISSOLVE_BLOCKS * (t/2000ms)²`
- Timeline:
  - t=0ms: 0 blocks → old GIF fully visible
  - t=500ms: ~62 blocks (~7%) → scattered random squares of new GIF
  - t=1000ms: ~225 blocks (~25%) → more coverage, accelerating
  - t=1500ms: ~506 blocks (~56%) → dominant new GIF, old fading fast
  - t=2000ms: all 900 blocks → new GIF complete
- Uses fixed-point math: `t256 = (elapsed * 256) / 2000`, `targetStep = DISSOLVE_BLOCKS * t256² >> 16`
- Reveals blocks from `s_blockOrder` in shuffled order as targetStep increases

#### 2d. `pushScanlineWithTransition()` (lines 863–900)
- **Logic**: For each scanline dy, iterate 30 column blocks (8px wide each)
  - Revealed blocks: push new GIF content via `pushImage`
  - Unrevealed blocks: skip (old TFT content remains)
- **Optimization**: Batch contiguous revealed segments into single SPI `pushImage()` calls to minimize transaction overhead
- **Result**: Random scattered 8×8 squares of new GIF appear, starting sparse, flooding in faster as ease-in accelerates

#### 2e. `stop()` cleanup (lines 576–578)
- Resets `s_inTransition` and `s_transitionStep` to 0

**Visual Result** (with `gif_dissolve_enabled = true`):
- **First 500ms**: Sparse random squares scattered across screen (looks like video buffering glitch artifacts)
- **500-1500ms**: Blocks accelerate, coverage increases quadratically — feels like pixels flooding in faster
- **1500-2000ms**: Final sweep, blocks fill remaining gaps rapidly
- **After 2000ms**: Fully transitioned, smooth new GIF content

This matches your reference images — pixelated block-style replacement with slow-to-fast acceleration, resembling digital/video glitch effects.

---

## Files Modified

| File | Changes |
|------|---------|
| `src/gif_player.h` | Added 4 private members for toast state (`_toastShownMs`, `_toastVisible`, `drawToast()`, `clearToast()`) |
| `src/gif_player.cpp` | Restored block dissolve state vars with ease-in timing in `startTransition()`, `tick()`, `pushScanlineWithTransition()`, `stop()`; integrated toast calls in `fetchAndPlay()` (2 sites), `openAndPlayFile()`, `tick()`; added `drawToast()` / `clearToast()` methods |

---

## Testing Checklist

1. ✅ **Build**: `pio run -t clean && pio run` succeeds, no compiler errors
2. ✅ **Flash**: `pio run -t upload` succeeds to ESP32 on COM7
3. ✅ **Startup**: Device boots normally, connects to WiFi, logs show GIF begin + cache/config load
4. **Toast test** (manual):
   - Disable WiFi or set invalid GIF URL
   - Should see maroon "[!] No internet  10s" banner above status bar
   - Countdown ticks down every ~1 second
   - On reconnect/valid URL, banner clears at next successful fetch
5. **Glitch test** (manual):
   - Confirm `gif_dissolve_enabled` is true in web config
   - Watch GIF transitions
   - Should see top-to-bottom sweep starting slow, accelerating
   - Horizontal jitter bands visible near frontier for ~2 seconds
   - Transition fully complete after 2 seconds
6. **CRT mode** (if enabled in config):
   - Odd scanlines should still be blanked (black phosphor gaps)
7. **RAM**: Compare `ESP.getFreeHeap()` before/after — should be ~1.7 KB higher

---

## Configuration Notes

- `gif_dissolve_enabled` (in web config) controls whether block dissolve transition runs on GIF changes
  - `true` → ease-in quadratic block reveal (2-second transition)
  - `false` → instant black fill, no transition
- **Tuning parameters** (in `src/gif_player.cpp`):
  - `DISSOLVE_DURATION_MS = 2000` — total transition time in milliseconds; reduce for faster fade, increase for slower
  - `DISSOLVE_BLOCK_SIZE = 8` — pixel size of each block; larger blocks = chunkier glitch effect, smaller = finer granularity
  - `DISSOLVE_BLOCKS = 900` — total block count (auto-calculated as 30×30 grid for 240×240 display)

---

## Known Limitations / Design Notes

- **Toast**: Non-interactive and non-dismissible; clears automatically on next successful download or after ~10 second retry window
- **Toast positioning**: y=185-218 (above status bar, within GIF clip area) — new GIF transition will naturally overwrite it
- **Block order**: Shuffled once per GIF via Fisher-Yates; same GIF source will have different random reveal order each time (uses hardware RNG)
- **Transition timing**: Always completes in exactly `DISSOLVE_DURATION_MS` (2000ms by default), independent of GIF frame rate
- **CRT mode**: Works alongside ease-in dissolve — odd scanlines are still blanked when enabled
- **Performance**: Transition uses block-based segmentation (min SPI transaction overhead); 240×240 screen → 30×30 grid is optimal for this display size
