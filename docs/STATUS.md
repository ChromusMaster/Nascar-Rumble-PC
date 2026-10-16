# Implementation status

This document records what is implemented, what was validated and how, the
known differences from the original hardware/game and what is missing.
**Full compatibility with the original game is not claimed.**

Reference used for all validation: NASCAR Rumble (USA), SLUS-01068, CUE/BIN
image of 111 075 sectors, executable SHA-1
`2726ac3e6f3d756f78982f7c66e507fc3aaf7aff`.

## 1. Validated in practice

| Item | How it was validated |
|---|---|
| Image identification and error messages | automated tests with a synthetic image (missing file, CUE pointing at a missing BIN, unsupported track, disc without the executable) and with the real image (status OK) |
| Boot without BIOS (HLE kernel) | the original executable runs from its entry point to the race |
| Legal screen, EA logos, intro video (MDEC/WVE) | screenshots inspected; MDEC input identical between the original and native decoders |
| Title, main menu, mode/car/track selection, loading screens (LSC) | screenshots; scripted input of the tests |
| Race: countdown, 3D, AI cars, HUD, minimap, speedometer | screenshots at frame 5 600 (lap 1/4, position 6/6, accelerator pressed) |
| Audio | SPU output (music, effects) recorded to WAV and checked by amplitude; **not** compared with a hardware recording |
| Memory card | automatic creation and formatting of the `.mcd` image; Game Options > Load and Save: "Save Not Found" on an empty card, "Saved Successfully", the directory entry `BASLUS-01068NASCRMBL` written to the image, "Save Found" and "Loaded Successfully" in a new session (0.3.2; automated test for the save) |
| Native BS decoder | 498/498 calls bit-exact over the logos and intro (`--verify-natives`), see [NATIVE.md](NATIVE.md) |
| Determinism | two identical runs produce the same VRAM (automated test) |
| Unimplemented features | none triggered in 6 000 frames from boot to the race (`nr_unimplemented` report) |
| Performance | ~214 frames/s without a window (6 000 frames, boot to race, in 28 s on one x86-64 core of the build machine) — about 3.6× real time. Slower than 0.2.0 (~290 frames/s) because of the memory-timing and instruction-cache model |

Not validated in this release: **full races** (several laps up to the
results), championship, 2-player modes (port 2 implemented, not exercised),
every track and car, options, help screens and credits, saving/loading a
championship. These paths use the same core and did not trigger missing
features, but they were not walked through systematically.

### Validated in 0.3.0

| Item | How it was validated |
|---|---|
| Launcher → engine | Linux (Xvfb, OpenGL via Mesa llvmpipe) and Wine (Direct3D 11): "Play!" starts the engine with a session token; starting `nrengine` directly opens the launcher; settings saved and read back |
| Launcher languages | screenshots in English, Portuguese, Spanish (Argentina), Chinese, Japanese and Korean with system fonts (Noto CJK); missing font → English with a message |
| Esc dialog | the simulation stops (no frames advance, audio paused); Yes is pre-selected with an arrow; No resumes; scripted test hook |
| OpenGL renderer | Linux, Mesa llvmpipe (OpenGL 4.5 core profile): logos, intro, menus and race at 1×–4× with AA |
| Intro speed | before: ~10.4 decoded frames/s; after: 14.8 frames/s (median 4.0 fields between frames; stream is 15 fps) |
| GTE | 1 150 hardware vectors of ps1-tests `gte/test-all` (all commands, flags, saturation): 0 differences |
| CPU memory timing | load costs calibrated from the hardware log of ps1-tests `cpu/access-time`; instruction cache modelled (256 lines × 4 words) |
| Physics step | from one race snapshot, the original step gives different results for CPU 1×, 2× and 4× (speedometer at field 5 600: 118, 131 and 128); with the steady 30 Hz step the pictures at fields 5 300 and 5 600 are **pixel-identical** for 1×, 2× and 4×, and the game keeps real time (150 game frames per 300 fields) |
| Generic controllers | HID descriptor parser and report decoding with synthetic descriptors (unit tests); device listing and binding capture in the launcher with SDL joysticks. **No physical controller was available** |
| `.deb` package | built with CPack (Ubuntu 24.04, x86-64), metadata and contents inspected with `dpkg-deb`, installed with `dpkg`, launcher started from `/usr/games` with settings in `~/.config/nrengine` and data in `~/.local/share/nrengine`, game started from it (OpenGL) and closed with the Esc dialog, package removed. The build machine's SDL2 was built from source, so the package was built with the fixed dependency list (`-DNR_DEB_SHLIBDEPS=OFF`) and installed with `--force-depends`; installing with `apt` on a clean Debian/Ubuntu system was not possible here (no package mirror reachable) |
| Windows build | cross-compiled with llvm-mingw; launcher and engine run under Wine |

### Validated in 0.2.0

| Item | How it was validated |
|---|---|
| Videos without audio drift | video frames of 566 k cycles (= 33 868 800 / 59.817); 0 audio blocks dropped in the window |
| Frame-rate modes | original; 20/30 limit only presentation; 60: ≈59 fps measured in the race with CPU ×3 and instant GPU |
| Silent Hill-style disc selection | automated tests (`[discsel/overlay]`) |
| Anti-aliasing up to 16x | real supersampling (4×4 samples); automated test of edge coverage and exact interior colour |
| 1× renderer = console | automated test: replay at 1×/15-bit gives VRAM identical to the console rasteriser |
| Enhancements do not change the game | console VRAM after 5 600 frames identical with and without 2× + AA 16x + 24-bit + bilinear + PGXP + CRT + ACES (disc test) |
| Translation (mods/Language) | menus shown with translated texts (screenshot); RAM range 0x6000–0xBFFF stays zero without mods (disc test) |
| User image never changed | automated test: after replacing a file with a larger one (virtual sectors), the image file is byte-identical |

## 2. Physics and collision: what was found

The reported symptoms — cars too heavy at some points and too light at
others, collisions where nothing is visible, falling where there is still
road — were investigated in 0.3.0:

1. **The vector unit is exact.** All geometry, projection and lighting maths
   of the game goes through the GTE, which now matches the hardware on 1 150
   recorded test vectors. Collision and physics code is the game's own MIPS
   code, executed as is.
2. **The game's simulation step depends on how long each frame took.** The
   race measures every frame in 1/300 s ticks, averages the last four values
   and passes the result (up to 25 ticks) to the vehicle, camera and collision
   code. Measured from the same race snapshot, changing only the emulated CPU
   speed changed the car's speed by more than 10 %. On the console the step
   varies with scene complexity, so the car's behaviour varies along the track,
   and a long step lets the collision probes jump further between tests —
   consistent with phantom contacts and falling through thin road sections.
3. **The emulated CPU was faster than the console.** Before 0.3.0 every
   instruction cost one cycle; memory wait states and instruction-cache misses
   are now modelled from hardware measurements, so frame times (and therefore
   steps) are closer to the console's.
4. **Steady physics step (new, on by default).** The engine makes the game
   wait inside its own frame-timing routine until a fixed step has passed
   (1/30 s, or 1/60 s in the 60 fps mode) and hands it exactly that step. The
   result is the same handling at every point of the track and on every
   computer. `engine.physics_step = original` restores the console behaviour.

What was **not** done: the game's vehicle and collision routines were not
rewritten (they are original code and would need full verification as in
[NATIVE.md](NATIVE.md)), and no comparison with a recording from a real console
was possible. If a specific spot still misbehaves, a bug report with the track,
the lap position and `nrengine.log` helps.

## 3. Known differences and approximations

Each item may produce behaviour different from the real hardware.

| # | Component | Difference | Expected effect |
|---|---|---|---|
| D1 | CPU | memory costs per access type and an instruction-cache model are calibrated from hardware measurements, but the write buffer, data-cache (scratchpad) contention and multiply/divide latencies are approximated | frame times close to, not identical with, the console |
| D2 | CPU/DMA | the CPU is not paused during block DMA transfers | CPU×DMA order may differ in edge cases |
| D3 | GPU | estimated drawing time (no public per-primitive measurements); no texture-cache model | frame pace close, not cycle-identical |
| D4 | GPU | 16.16 interpolation by plane equations; edge rules checked, but no bit-exact guarantee on every triangle | 1-pixel/1-level differences possible |
| D5 | SPU | reverb processed at 22.05 kHz **without** the 39-tap FIR resampling filters | slightly different reverb timbre |
| D6 | Timers | the 1–2-cycle hold at 0000h after reset/overflow is not modelled; timer 0 HBlank sync approximated per line | deviations of a few cycles |
| D7 | CD-ROM | estimated seek times; no CD-DA and XA-ADPCM (the disc uses neither; such commands are logged as "not implemented") | approximate loading times |
| D8 | HLE kernel | reimplemented from documentation; the original BIOS `SysDeqIntRP` bug is not reproduced; minimal `DefInt`/threads semantics | only affects software relying on these details (not seen in this game) |
| D9 | Memory card | served at sector level by the HLE kernel with the retail event sequence, one sector per video field; on the SIO bus the card answers "not connected" | card access is faster than on the console; games accessing the card directly would fail (this one uses the BIOS) |
| D10 | Controller | digital pad only; no DualShock analog/vibration; the 2-player path was **not** validated | — |
| D11 | Natives | native BS decoder time modelled linearly (+0.3 % aggregate; individual calls vary) | slightly different video decoding pace |
| D12 | MDEC/DMA | data transferred at once; completion delayed by a model | — |
| D13 | Video | 4:3 presentation by default; interlacing not emulated (optional CRT effect) | presentation only |
| D14 | Enhanced renderers | mask bit not honoured by the GPU renderers; PGXP and widescreen experimental (widescreen changes what the game computes); 60 fps mode changes the pace | only with enhancements on |
| D15 | Physics step | the steady step (default) changes the game's timing on purpose: heavy scenes run slightly slower instead of taking longer steps | `original` restores the console behaviour |

### Original behaviour reproduced on purpose

- **"VSync: timeout" at boot**: the executable disables interrupts
  (`SR &= ~0x401`, routine at `8009C1C0`) and then calls `VSync(1)`; with
  interrupts off the counter never advances and the library prints the
  warning. It is the original code's behaviour, not an engine defect.
- The native BS decoder reproduces the original's quirks (see
  [NATIVE.md](NATIVE.md)).

### Emulation fixes recorded

- *Memory card not detected (0.3.2)*: the game reported "No MEMORY CARD"
  even with a card image. Its card driver relies on the retail BIOS event
  sequence: `_card_load` reads 16 directory sectors and produces a HwCARD
  event for each one, the high-level SwCARD event comes before the HwCARD
  event of the last step, `_card_info` produces both, and calls made while
  the port is busy return 0. After starting a read the game ignores the first
  HwCARD event (the stale one from `_card_load`). The HLE kernel delivered a
  single event per operation, so the game kept retrying. The sequence now
  follows the BIOS behaviour documented by the OpenBIOS reimplementation
  (PCSX-Redux).
- *Seam in full-screen backgrounds above 1x (0.3.2)*: two 160-pixel textured
  polygons form the menu background; at high resolution the samples just left
  of x = 160 read texel 160 (outside the picture), giving a black or coloured
  line. Texture coordinates are now clamped to the last texel the console
  draws (`max - 1`) in the software, OpenGL and Direct3D renderers above 1x.
- *Launcher drop-down crash (0.3.1)*: opening any drop-down list closed the
  launcher on Windows. The open list drew item texts through pointers into the
  stack of the tab function that had already returned; the texts are now
  copied into the UI state. Reproduced and confirmed fixed with
  AddressSanitizer (stack-use-after-return) using `NRLAUNCHER_TEST_CLICKS`; an
  ASan run clicking every tab and control and an ASan run of the launcher plus
  1 200 frames of the engine report no errors.
- *Interruptible native time (0.3.0)*: the native MDEC decoder's modelled time
  (≈600 000 cycles) was added in one block, so CD-ROM interrupts falling inside
  it were serviced late and the game's streaming loop lost sectors; the intro
  ran at ~10 fps. The time is now consumed while interrupts are serviced.
- *CPU memory timing and instruction cache (0.3.0)*: this changed the game's
  timeline; the regression references were regenerated and the native decoder
  cost was recalibrated.
- *Video clock and periodic scheduling (0.2.0)*: the GPU/CPU clock ratio was
  PAL's; it is now the exact NTSC one (33 868 800 / 53 693 175). Periodic
  events are scheduled from their due time.
- *DMA restarted before the modelled completion (0.2.0)*: a new start write to
  CHCR while the previous completion was pending was ignored, leaving the MDEC
  waiting; the previous transfer is now completed first.
- *INI updates (0.3.0)*: saving could corrupt `[section]` headers; fixed and
  covered by a test.

## 4. Not implemented

Logged at run time by `nr_unimplemented` (one warning per feature + count on
exit). None of these was triggered by the game in the tests.

| Component | Feature |
|---|---|
| CD-ROM | CD-DA (Play/Forward/Backward), XA-ADPCM, `Test` sub-functions beyond the used ones |
| DMA | linked lists outside the GPU channel; directions not supported by the hardware |
| GTE | undefined opcodes |
| CPU | unused COP0/COP2 variants |
| HLE kernel | A/B/C functions not used by the game (each unknown call is logged with its number) |
| Peripherals | SIO1 serial port, parallel/expansion port, DualShock, multitap, light gun |

## 5. Pending (next steps)

1. Walk systematically through every game mode (full race, championship,
   2 players, every track) with input scripts and regression references.
2. Compare with a real console: screenshots, audio and frame timing recorded
   on hardware for the same inputs.
3. Reimplement the vehicle and collision routines natively, each verified as
   in [NATIVE.md](NATIVE.md), which would allow a sub-stepped collision test.
4. CPU paused during DMA (D2); reverb FIR filters (D5).
5. Test generic USB controllers with physical devices (DragonRise, PS-to-USB
   adapters, Logitech) on Windows and Linux.
6. Run on real Windows with a graphics card, especially Direct3D 12; build
   with MSVC.
7. Mask bit in the GPU renderers; widescreen without edge culling.
8. Document the CTRL/SHOC resource types and car/track formats for mods.
