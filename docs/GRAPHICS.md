# Graphics enhancements and renderers

Every enhancement acts **only on the picture shown to the player**. The
emulated console keeps drawing into its 1024×512 VRAM exactly as the hardware
would (this is what the game reads back); in parallel the emulated GPU records
a list of drawing commands (`src/render/rlist.h`) that an enhanced renderer
replays into a high-resolution copy. No game file is read differently or
modified, and no texture or model is replaced.

Verification: with `--internal-res 2 --aa 4` (and the other options) the
console VRAM stays identical, CRC for CRC, to a run without enhancements (the
`[swr]` group of `nrtests` with a synthetic scene, and the "enhanced render"
check of the game-data tests). The exceptions, which **do change** what the
game computes, are marked as experimental (widescreen) or only change the pace
(frame-rate modes and the physics step, see [CONFIG.md](CONFIG.md)).

All options are set in the **launcher** (Graphics and Effects tabs) or in
`nrengine.ini`; they are no longer changed while the game is running.

## Options

The set of options follows the Silent Hill PC port used as a reference
(internal resolution, VSync, frame-rate limit, dithering, texture filtering,
scaling modes, picture adjustment, post-processing, tone mapping, PGXP and
widescreen). The implementation is this engine's own.

| Option (`[video]`) | Values | Effect |
|---|---|---|
| `renderer` | `auto`, `opengl`, `d3d9`, `d3d10`, `d3d11`, `d3d12`, `software` | API used (see below) |
| `internal_resolution` | 1–8 | draws the scene at 320×240 × N |
| `antialiasing` | 1, 2, 4, 8, 16 | real supersampling: 2 = 2×1, 4 = 2×2, 8 = 4×2, 16 = 4×4 samples per pixel, resolved by averaging |
| `true_color` | 0/1 | Gouraud shading, blending and texture modulation in 24-bit instead of 15-bit |
| `dithering` | 0/1 | the console's 4×4 dithering (on by default; off removes the pattern) |
| `texture_filter` | 0, 1, 2 | 0 = nearest texel (console); 1 = bilinear on 3D polygons; 2 = bilinear on sprites/2D too. Honours transparency (texel 0) and texture windows |
| `scaling` | `integer`, `nearest`, `bilinear`, `sharp` | how the picture is fitted to the window |
| `aspect` | `4:3`, `stretch`, `16:9` | picture proportion in the window |
| `post_fx` | `off`, `crt`, `scanlines`, `vignette`, `colorgrade`, `grain`, `sharpen`, `cinematic` | post-processing effect |
| `post_fx_strength` | 0–1 | effect strength |
| `tonemap` | `off`, `reinhard`, `aces`, `filmic` | tone curve applied to luminance in linear light (see "Tone mapping and grading") |
| `brightness`, `contrast`, `saturation`, `gamma` | −1..1, 0.5..1.5, 0..2, 0.5..2 | picture adjustment |
| `vsync` | 0/1 | vertical sync |
| `pgxp` | 0/1 | **experimental**: sub-pixel vertex precision and perspective-correct textures |
| `widescreen` | 0/1 | **experimental**: 16:9 projection |
| `fps` | `original`, 20, 30, 60 | see "Frame rate" |
| `show_fps` | 0/1 | shows the game and display frame rates in a corner |

The grading/tone-mapping formulas are the same in the software renderer and in
the shaders (`src/render/gfx.c`, `src/platform/gl_shaders.h`,
`src/platform/win32/d3d_shaders.h`).

### Tone mapping and grading

The console produces a standard-range picture; it has no highlights above
white for a tone curve to compress. The tone curves are therefore used as
look curves, with these rules (0.3.3):

- the picture is converted from sRGB to linear light, the curve is applied to
  the luminance (BT.709 weights) and the colour is scaled by the luminance
  ratio, so hues do not shift;
- every curve is normalised so black stays black and white stays white;
- the scale is limited so the largest channel never exceeds 1, so saturated
  yellow, orange and cyan keep their saturation instead of clipping;
- exposure: Reinhard 1.0, ACES (Narkowicz fit) 1.2, filmic (Hable) 2.0.

ACES and filmic are S-curves: they darken the darkest tones and brighten the
middle. That is their nature, not a defect. For the console's look keep
`tonemap = off`.

`colorgrade` and `cinematic` apply a warm white balance that keeps the
luminance of every pixel, plus a vibrance boost (up to 15 % × strength) that
is smaller for colours that are already saturated, and are fitted back into
the displayable range by moving towards grey instead of clipping a channel.

Before 0.3.3 the curves were applied per channel to the gamma-encoded picture:
white came out at 75 % (ACES), dark tones were crushed, saturated colours
shifted hue and clipped, and `cinematic` raised saturation by 12 % with hard
clipping.

### Memory limit

Internal resolution × anti-aliasing grid is limited by the detected memory as
well as by the renderer (`src/platform/sysinfo.c`, `nr_gfx_fit`). The engine
and the launcher read the installed RAM, the CPU thread count and, on Windows
(DXGI) and on Linux with `amdgpu`, the dedicated memory of the graphics card.
GPU renderers may use half of the dedicated video memory for the three
high-resolution VRAM copies; the software renderer may use a quarter of the
RAM for its two copies. One copy at factor F per axis takes
1024·F × 512·F × 4 bytes (F = 16 → 512 MiB). The launcher only offers the
combinations that fit; the engine logs the detected hardware and any
reduction. When the video memory cannot be read (NVIDIA or Intel on Linux)
only the renderer limit applies.

### Resolution × anti-aliasing limit

The high-resolution copy is 1024·N·ax × 512·N·ay pixels (N = internal
resolution, ax×ay = AA grid). The product per axis must fit the API's texture
limit:

| Renderer | Maximum per axis (N × grid) | Valid examples |
|---|---|---|
| `opengl` | `GL_MAX_TEXTURE_SIZE` / 1024 (16384 → 16) | 4× + AA 16x; 8× + AA 2x |
| `d3d12`, `d3d11` (feature level 11_0+) | 16 (16384×8192) | 4× + AA 16x; 8× + AA 2x |
| `d3d11` at level 10_x, `d3d10` | 8 (8192×4096) | 4× + AA 4x; 2× + AA 16x |
| `d3d9` | the card's `MaxTextureWidth/Height`, up to 16 | usually 4–16 |
| `software` | 16 (memory: 512 MB at 16) | |

If a combination does not fit, the engine keeps the largest internal
resolution that fits and logs a warning. High combinations need a lot of video
memory and GPU time.

## Renderers

All GPU renderers draw the whole command list on the GPU (polygons, lines,
sprites, fills, VRAM copies and uploads), including 4/8-bit palettes, texture
windows, the console's four semi-transparency modes and dithering. They share
the replay logic in `src/render/gpu_replay.c`.

`auto` tries, in this order: Windows — Direct3D 11 → OpenGL → Direct3D 10 →
Direct3D 9 → software; Linux and other systems — OpenGL → software. When a
renderer chosen explicitly fails, the engine falls back to the next one and
logs why.

| API | Platforms | Features used | Notes |
|---|---|---|---|
| **OpenGL 3.3 core** | Windows, Linux, BSD | GLSL 3.30 with integer textures (exact palette and 15-bit decoding), framebuffer objects up to `GL_MAX_TEXTURE_SIZE`, blend equations with reverse subtract; functions loaded with `SDL_GL_GetProcAddress` (no GLEW/GLAD needed) | default on Linux; validated with Mesa (llvmpipe) |
| **Direct3D 12** | Windows 10+ | pre-built PSOs for every blend combination, root constants, 64 MB upload ring, explicit barriers, flip-discard swap chain | **never executed**, see validation |
| **Direct3D 11** | Windows 7+ | Shader Model 4/5 with integers, 16384² render targets, staging copies | default on Windows |
| **Direct3D 10/10.1** | Windows Vista+ | same SM4 shaders as D3D11 | 8192² limit |
| **Direct3D 9 (Ex)** | Windows Vista+ | Shader Model 3 (equivalent floating-point arithmetic), dynamic BGRA textures, `StretchRect` for VRAM reads | for old cards |
| **software** | all | multithreaded C renderer | presented through SDL |

The Direct3D libraries and the shader compiler (`d3dcompiler_47.dll`, part of
Windows 8.1+ and of an updated Windows 7) are loaded at run time. OpenGL needs
a driver that offers a 3.3 core profile (any GPU from about 2010 onwards;
Mesa 10+ on Linux).

**Lighting and shading.** In NASCAR Rumble lighting is computed by the game
itself (CPU + GTE) and reaches the GPU as per-vertex colours; the console has
no shaders. The engine **does not invent** new lights or shadows (that would
change the original look and would need data the game does not provide). What
each API improves is how that shading is drawn: 24-bit Gouraud interpolation,
no dithering, perspective (PGXP), higher resolution and real AA. Tone mapping
and colour grading act on the final picture.

**Texture loading.** Textures come from the console VRAM (uploaded by the
game). The GPU renderers batch the VRAM uploads and transfer them once per
frame; reads of the current frame (effects that use the screen as a texture)
use a second VRAM copy that is updated only in the affected region.

**Pixel centres.** The console samples polygons at integer pixel
coordinates. The high-resolution renderers map each output sample back to the
console grid with the same convention (sprites and polygons have different
rules on the real GPU), so edges and texture coordinates line up with the 1×
picture. At 1× the software renderer is pixel-identical to the console in the
unit tests.

## Validation status

Development happens on Linux; the Windows binaries are cross-compiled
(llvm-mingw). Validation done for version 0.3.0:

| Renderer | How it was tested | Result |
|---|---|---|
| software | native Linux and Wine | race at 4×, 24-bit, bilinear, CRT; 1× identical to the console in the synthetic test |
| opengl | native Linux, Mesa llvmpipe (OpenGL 4.5 core) | intro video, menus and race at 2×/4× with AA; started from the launcher |
| d3d11 | Wine (wined3d on OpenGL/llvmpipe) | race at 2× + AA 4x, 24-bit, filter; started from the launcher |
| d3d10 | Wine | race at 2× |
| d3d9 | Wine | race at 2× + CRT |
| d3d12 | Wine + vkd3d | **not executed**: the test machine has no Vulkan device usable by vkd3d. A creation failure is handled and the engine falls back automatically (verified) |

No renderer was executed on real Windows with a real graphics card for this
release. Wine is a different Direct3D implementation; results on Windows may
differ (please report with the log file).

Known limitations of the enhanced renderers:

- the mask bit ("do not draw over marked pixels") is not honoured by the GPU
  renderers (the software renderer honours it). The game was not seen using
  it, but it was not proven that it never does;
- 24-bit with dithering off gives smoother gradients than the console — that
  is the intended enhancement, but it is not the original picture;
- PGXP: entries of the precision cache are valid for two display flips; a
  screen position computed twice in the same frame with depths that differ by
  more than 2 % (or 1 unit) is marked ambiguous and not used, because the
  cache cannot tell which polygon the game meant. Before 0.3.3 stale entries
  from earlier frames and the last of several conflicting depths were used,
  which gave wrong perspective to some polygons (textures flickering or
  sliding over each other). The log shows hits, misses, conflicts and expired
  entries on exit. In a 5 600-frame race at 8× the old cache would have used
  about 111 000 conflicting and 17 500 stale entries;
- PGXP: when a vertex is not found in the precision cache the polygon uses the
  console's integer position (no cracks) and no perspective;
- widescreen compresses the GTE's horizontal projection. The game still culls
  objects with its 4:3 field of view, so objects can pop in at the edges. It
  changes what the game computes (do not use it for fidelity tests).

## Frame rate (`video.fps`)

| Mode | What it does |
|---|---|
| `original` | console pace (≈59.82 Hz video; the game decides how many frames it draws) |
| `30`, `20` | limits **presentation** to 30/20 frames per second; the game keeps its original pace (sound and gameplay unchanged) |
| `60` | unlocked: turns off the GPU timing model and runs the emulated CPU 3× faster so the game can produce up to 60 frames per second. Combine it with the 60 Hz physics step (`engine.physics_step = 60`) for constant handling |

## Intro and FMV playback speed

Version 0.3.0 fixes the slow intro video reported for 0.2.0 (audio and picture
were in sync but both ran slowly). The cause: the native MDEC decoder was
charged as one indivisible block of CPU time (about 600 000 cycles), so CD-ROM
and timer interrupts that fell inside it were serviced late; the game's
streaming loop then lost sectors and waited for the next disc revolution. The
native routine's time is now consumed cycle by cycle, with interrupts serviced
in the middle as on the console. The intro now plays at the disc's 15 frames
per second. The approach is the same idea the REDRIVER2 project uses for its
FMV player (pace by the stream's own frame time, never by the decoding cost).

## In-game keys

| Key | Function |
|---|---|
| Esc | pauses the game and asks "Do you want to close the Engine?" (Yes/No) |
| F11 | toggle full screen |
| F12 | save a PNG screenshot (in the user data folder) |
