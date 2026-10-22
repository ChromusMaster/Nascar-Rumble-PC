# Code reference

This document describes public functions, types and constants. The source
files carry no comments; rationale and hardware notes live here and in the
documents referenced from each entry. Entries were added starting with 0.3.3;
APIs older than that are described in ARCHITECTURE.md, GRAPHICS.md and
NATIVE.md until they are moved here.

## render/gfx.h — picture enhancement settings

### `float nr_gfx_srgb_to_linear(float c)` / `float nr_gfx_linear_to_srgb(float c)`

- Purpose: exact IEC 61966-2-1 sRGB transfer functions.
- Input: one channel; values outside 0..1 are clamped first. Output 0..1.
- Side effects: none. Pure.
- Hardware: none; presentation only. GRAPHICS.md "Tone mapping and grading".

### `float nr_gfx_tonemap_exposure(nr_gfx_tonemap t)`

- Purpose: exposure used by each tone curve (Reinhard 1.0, ACES 1.2, filmic 2.0).
- Output: 1.0 for `GFX_TM_OFF` and unknown values.

### `void nr_gfx_tonemap_pixel(nr_gfx_tonemap t, float rgb[3])`

- Purpose: applies the tone curve to one sRGB pixel in place.
- Method: sRGB → linear, curve on BT.709 luminance, normalised so that
  `curve(e) = 1`, colour scaled by the luminance ratio, scale limited so the
  largest channel stays ≤ 1, linear → sRGB.
- Invariants: black → black, white → white, monotonic on greys, hue of
  in-range colours preserved. `GFX_TM_OFF` leaves the pixel unchanged.
- Kept identical to `tonemap()` in `src/platform/gl_shaders.h` and
  `src/platform/win32/d3d_shaders.h` (SM 3.0 and SM 4.0+).
- Tests: `color` group in `nrtests`. STATUS.md "Emulation fixes recorded".

### `void nr_gfx_fit_gamut(float linear_rgb[3], float luminance)`

- Purpose: brings a linear colour whose largest channel exceeds 1 back into
  range by moving it towards the grey of the given luminance, then clamps
  every channel to 0..1.
- Precondition: `luminance` is the target luminance of the colour (0..1).

### `void nr_gfx_color_grade(float rgb[3], float strength)`

- Purpose: the `colorgrade` effect, also used by `cinematic`.
- Method: linear light; warm white balance (red × (1 + 0.05·k), blue ×
  (1 − 0.06·k)) renormalised to the original luminance; vibrance
  `1 + 0.15·k·(1 − saturation)`; gamut fit towards grey.
- Invariants: strength 0 is the identity; the luminance of the pixel is
  unchanged before the gamut fit.
- Kept identical to `grade_fx()` in the shaders. Tests: `color`.

### `void nr_gfx_grade_pixel(const nr_gfx_settings *s, float rgb[3])`

- Purpose: final adjustment chain of the software path: tone curve, then
  brightness, contrast, saturation and gamma.

### `u64 nr_gfx_render_budget(u64 ram_total, u64 gpu_memory, bool gpu_renderer)`

- Purpose: memory the high-resolution VRAM copies may use.
- Output: half of `gpu_memory` for GPU renderers, a quarter of `ram_total`
  for the software renderer; 0 means unknown.

### `int nr_gfx_memory_scale_limit(u64 budget_bytes, unsigned copies)`

- Purpose: largest per-axis factor F (1..16) such that `copies` copies of
  1024·F × 512·F 32-bit pixels fit in `budget_bytes`.
- Output: 16 when the budget or the number of copies is 0 (unknown).

### `void nr_gfx_set_memory_scale_limit(int max_scale)` / `int nr_gfx_get_memory_scale_limit(void)`

- Purpose: process-wide upper bound applied by `nr_gfx_fit` in addition to the
  renderer's own limit. Clamped to 1..16; default 16.
- Side effects: global state of the render module. Set by the frontend before
  each renderer is initialised. Does not influence console VRAM, so
  determinism is unaffected.

### `const nr_gfx_settings *nr_gfx_fit(const nr_gfx_settings *s, int max_scale, nr_gfx_settings *tmp)`

- Purpose: returns `s`, or a reduced copy in `*tmp`, so that
  `internal_scale × AA grid` fits `min(max_scale, memory limit)` per axis.
  Anti-aliasing is lowered first, then the internal resolution. Logs a
  warning when it reduced anything.
- Tests: `gfx budget`.

## render/pgxp.h — sub-pixel vertex cache (experimental)

### `nr_pgxp`

- Fields: per slot `key` (packed screen coordinate as sent to the GPU),
  precise `x`, `y`, depth `w`, generation `gen`, `valid`, `ambiguous`;
  current generation `now`; statistics `hits`, `misses`, `conflicts`,
  `expired`.
- `NR_PGXP_SIZE` 8192 slots; `NR_PGXP_MAX_AGE` 2 display flips.

### `void nr_pgxp_tick(nr_pgxp *c)`

- Purpose: advances the generation. Called by the GPU on every display flip
  (`src/psx/gpu.c`, display area change).

### `bool nr_pgxp_same_depth(float a, float b)`

- Purpose: depths within 1 unit or 2 % are treated as the same vertex.

### `void nr_pgxp_store(nr_pgxp *c, u32 key, float x, float y, float w)`

- Purpose: records the unrounded projection of a GTE RTPS/RTPT result.
- Behaviour: a second store of the same key in the same generation with a
  different depth marks the slot ambiguous (counted in `conflicts`) and keeps
  it ambiguous until the next generation.

### `bool nr_pgxp_lookup(nr_pgxp *c, u32 key, float *x, float *y, float *w)`

- Purpose: returns the precise values for a vertex sent to the GPU.
- Fails (miss) for unknown keys, ambiguous slots and entries older than
  `NR_PGXP_MAX_AGE` generations (also counted in `expired`).
- Invariant: only the enhanced renderers use the result; console VRAM is
  produced from the integer coordinates. GRAPHICS.md "Known limitations".
- Tests: `pgxp`.

## platform/sysinfo.h — host hardware

### `nr_sysinfo`

- `ram_total` bytes of physical memory, `cpu_threads` logical processors,
  `gpu_name` UTF-8 adapter name, `gpu_vram` dedicated video memory in bytes
  (0 when unknown).

### `bool nr_sysinfo_query(nr_sysinfo *si)`

- Windows: `GlobalMemoryStatusEx`, `GetSystemInfo`, DXGI 1.1
  (`dxgi.dll` loaded at run time; first hardware adapter). Windows 7 or later.
- Linux: `sysconf` for memory and processors,
  `/sys/class/drm/card*/device/mem_info_vram_total` (amdgpu) for video memory.
- Output: false when the memory size could not be read; the structure is
  always initialised.

### `int nr_sysinfo_scale_limit(const nr_sysinfo *si, bool software_renderer)`

- Purpose: per-axis limit for the renderer type (3 copies for GPU renderers,
  2 for the software renderer). Used by the engine before initialising a
  renderer and by the launcher to restrict the internal resolution and
  anti-aliasing lists.

### `void nr_sysinfo_describe(const nr_sysinfo *si, char *out, size_t n)`

- Purpose: one-line description for the log and the launcher's Advanced tab.

## platform/nr_sdl.h — SDL2/SDL3 adapter

### `const char *nr_sdl_audio_device_name(nr_sdl_audio a)`

- Purpose: name of the physical device behind an opened audio stream (SDL3);
  "system default" with SDL2. Logged when audio starts.
