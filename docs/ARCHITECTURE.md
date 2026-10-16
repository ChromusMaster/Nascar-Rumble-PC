# Architecture

## Overview

```
  ┌──────── launcher ────────┐        ┌──────────── platform ─────────────┐
  │ src/launcher (nrlauncher)│ starts │ src/platform/sdl2/main_sdl.c       │
  │ settings UI, l10n, fonts,│───────►│  window, audio, input, pause/quit, │
  │ disc check, controller   │ token  │  59.817 Hz pacing (nrengine)       │
  │ setup → nrengine.ini     │        │ src/app/main_headless.c (no window)│
  └──────────────────────────┘        └─────────────────┬──────────────────┘
                                                        │ nr_app_* (frame by frame)
                                      ┌─────────────────▼──────────────────┐
                                      │ src/app/app.c  options, INI, disc  │
                                      │  identification, EXE load, scripted│
                                      │  input, PNG/WAV, snapshots         │
                                      └───┬───────────────┬──────────────┬─┘
                                          │               │              │
                       ┌──────────────────▼─┐  ┌──────────▼─────────┐  ┌─▼──────────────────┐
                       │ src/disc           │  │ src/psx (core)     │  │ src/native         │
                       │ CUE/BIN/ISO,       │◄─┤ CPU, GTE, GPU,     │◄─┤ game routines in C │
                       │ ISO9660, identify, │  │ SPU, MDEC, CD, DMA,│  │ (registry with     │
                       │ gamedata selection │  │ timers, SIO, HLE   │  │ code signatures),  │
                       └────────────────────┘  └─────────┬──────────┘  │ steady physics step│
                                                         │             └────────────────────┘
                                               ┌─────────▼───────────┐
                                               │ src/render  command │
                                               │ list, software and  │
                                               │ GPU replay, postfx  │
                                               └─────────────────────┘
```

The **core** (`nrcore`, a static library) depends on no external library and
no operating-system API beyond the C standard library; the isolated exceptions
are UTF-8 paths and user folders (`src/common/common.c`, `paths.c`) and
process start-up (`proc.c`). Everything that touches the user (video, audio,
input, real time) lives in the platform layer and the launcher, which can be
replaced without touching the core.

## Modules

| Folder / file | Role |
|---|---|
| `src/common` | types, log, "not implemented" registry, CRC32, SHA-1, PNG, INI (with comment-preserving update), UTF-8 files, portable/XDG/AppData paths (`paths.c`), process start and launcher session token (`proc.c`) |
| `src/disc/disc.c` | CUE/BIN (2352) and ISO (2048) images, sector reads, ISO9660; virtual CD layer for mods |
| `src/disc/gameid.c`, `manifest_slus01068.c` | identification: EXE SHA-1, name+size+CRC32 of the 108 files (metadata/hashes only) |
| `src/disc/discsel.c` | Silent Hill-style disc selection (`gamedata/`, `disc_image`, detection by content) |
| `src/psx/cpu.c` | R3000A interpreter: load and branch delay slots, exceptions, COP0, COP2 transfers; memory-access costs and instruction-cache model; interruptible native time (`stall`) |
| `src/psx/bus.c` | memory map (2 MB RAM mirrored, scratchpad, I/O), memory/cache control registers |
| `src/psx/gte.c` | complete GTE: all commands, 44-bit MAC, saturation, FLAG, UNR division |
| `src/psx/gpu.c` | GP0/GP1, rasteriser (triangles, rectangles, lines; Gouraud, 4/8/15 bpp textures, CLUT, texture window, semi-transparency, dithering, mask), VRAM transfers, NTSC video timing and **drawing-time model** |
| `src/psx/spu.c` | 24 ADPCM voices, Gaussian interpolation, ADSR, volume sweeps, pitch modulation, noise, IRQ, capture, reverb |
| `src/psx/mdec.c` | MDEC: quantisation/scale tables, RLE, IDCT, 4/8/15/24 bpp output |
| `src/psx/cdrom.c` | CD controller (commands, INT1..5, data FIFO, 1x/2x sector timing) |
| `src/psx/dma.c`, `timers.c`, `sio.c` | DMA (block, request, linked list, OTC), root counters, controller port |
| `src/psx/hle_bios.c` | HLE kernel: A/B/C tables, exception chains with 4 priorities, events, minimal threads, HookEntryInt/ReturnFromException, pads (InitPAD/StartPAD), file-level memory cards, TTY |
| `src/psx/overrides.c` | native replacement mechanism, observers and verification mode |
| `src/psx/psx.c` | event scheduler, interrupt controller, frame loop, machine snapshots |
| `src/native/native.c` | registry of SLUS-01068 native routines |
| `src/native/pace.c` | steady physics step (two observers in the game's frame-timing routine) |
| `src/formats` | decoders of the game's formats, used by native routines and `nrtool` |
| `src/app` | integration: options, identification, loading, captures, WAV audio, audio rate control |
| `src/mods/mods.c` | `mods/Language` and other mods: texts (native GetText, EXE messages in RAM) and replacement files |
| `src/render/rlist.c` | drawing command list recorded by the emulated GPU (side channel; does not affect emulation) |
| `src/render/swr.c`, `postfx.c`, `gfx.c`, `pgxp.h` | software enhanced renderer (multithreaded), post-processing, adjustments, vertex precision cache |
| `src/render/gpu_replay.c` | command-list replay shared by the OpenGL and Direct3D renderers |
| `src/platform/video.h`, `sdl2/vb_software.c`, `sdl2/vb_opengl.c`, `gl_shaders.h`, `win32/vb_d3d9/10/11/12.c`, `win32/d3d_common.c`, `win32/d3d_shaders.h` | presentation renderers |
| `src/input` | controllers: SDL game controller/joystick, direct HID (`hidraw.c`: Windows SetupAPI + `hid.dll`, Linux hidraw with our own descriptor parser `hidparse.c`), bindings and port configuration |
| `src/ui` | in-game overlay: pause/quit dialog, FPS counter, messages (public-domain 8×8 font) |
| `src/launcher` | launcher: immediate-mode UI (`lui.c`), TrueType text with system fonts (`ltext.c`, `stb_truetype.h`), translations (`l10n.c`, `l10n_tables.inc`), tabs and engine start (`launcher.c`) |
| `src/tools/nrtool.c` | disc inspection tool and `export-text` |
| `tests/test_main.c` | unit and regression tests |
| `packaging/` | `.desktop` entry, icon, udev rule, Debian `postinst` |

## Start-up

1. The launcher writes `nrengine.ini`, creates a random 128-bit session token
   and starts `nrengine --launcher-session <token>` with the same token in the
   environment. The engine refuses to run without a matching pair and starts
   the launcher instead.
2. `nr_app_start`: opens the image, mounts ISO9660, identifies the game and
   refuses invalid images with a clear message.
3. Creates the machine (`psx_create`) and builds the HLE kernel in kernel RAM
   (0x0000–0xFFFF: vectors, A/B/C tables, stubs, exception handler in MIPS
   code, control blocks).
4. Reads `SYSTEM.CNF` and the executable **from the user's image**, copies the
   text segment to 0x80010000 and starts at its entry point with the state the
   BIOS would leave (stack, GP, SR with interrupts enabled, CD interrupts
   enabled).
5. Registers the native routines and observers whose code signatures match.

## Execution

- `nr_app_frame` runs the machine up to the next VBlank (one NTSC field of
  263 lines × 3413 video cycles × 33 868 800 / 53 693 175 ≈ 566 204 CPU
  cycles, i.e. 59.817 Hz).
- Periodic events (GPU line, SPU, CD sectors) are rescheduled from their **due
  time** (`psx_schedule_periodic`), not from when they were serviced.
- The CPU runs in blocks until the next scheduled event (VBlank, line, timer,
  CD sector, DMA end, SPU audio); peripherals are synchronised on demand.
- Kernel instructions with the reserved opcode `0x1B` (only at addresses
  < 0x10000) branch to the HLE functions in C.
- Calls to addresses registered as native run the C function and return like
  `jr ra`. Their modelled execution time is **consumed as stall cycles** while
  interrupts keep being serviced, exactly as if the original code were still
  running; this is what keeps CD streaming (and so the intro video) at the
  right speed.
- Observers run C code at an address and then let the original instruction
  execute (used by the steady physics step and by the BS table capture).

## Timing

Pace fidelity (the race running at ~40 fps, the countdown at 60) depends on:

- **CPU**: base cost of one cycle per instruction, plus memory-access costs
  calibrated from the hardware log of the `cpu/access-time` test of
  ps1-tests (RAM ≈ 5 cycles per load, scratchpad 1, I/O 3, CD-ROM 8/14/26,
  SPU 18/18/39, BIOS 8/13/25, EXP1 7/14/26 for 8/16/32-bit), and an
  **instruction-cache model** (256 lines of 4 words; a miss costs
  3 + remaining words) for cached RAM fetches. Uncached and BIOS fetches cost
  the uncached access time.
- **GPU**: every command accumulates an estimated time (16 set-up cycles + 1
  or 2 per pixel, +1 with semi-transparency; fills 0.5/pixel; VRAM copies
  2/pixel). Until it elapses GPUSTAT reports busy and DMA2 completion is
  delayed — the game then waits as on the hardware.
- **DMA / MDEC / CD**: data is transferred at once; completion (busy bit and
  IRQ) is delayed by a time proportional to the amount.

Details and uncertainties in [STATUS.md](STATUS.md).

## Compatibility × enhancements

The default is **compatibility**. The picture enhancements
([GRAPHICS.md](GRAPHICS.md)) work like this:

```
emulated GPU ──► console VRAM 1024×512 (what the game reads; always the same)
     │
     └─ side channel: nr_rlist (polygons, sprites, uploads, copies…)
              │
              ├─► software: src/render/swr.c (high resolution + AA) ─► postfx ─► window
              └─► OpenGL / D3D9/10/11/12: gpu_replay.c ─► shaders ─► window
```

The side channel only reads what the emulated GPU already decided to draw;
nothing flows back into the machine. Documented exceptions: widescreen
(changes the GTE projection), 60 fps mode (faster CPU, instant GPU), the
steady physics step (on by default; `engine.physics_step = original` turns it
off) and the diagnostic options `--gpu-instant`, `--no-native`. Language mods
change texts in memory and files served by the virtual CD — never the image.
