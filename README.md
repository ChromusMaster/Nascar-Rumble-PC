# NASCAR Rumble Reimplementation Engine (NR Engine)

An engine written in C that runs **NASCAR Rumble** (PlayStation, EA Sports,
2000 — North American release SLUS-01068) from the **user's own copy**, with
the goal of reproducing the original behaviour 1:1 on Windows and Linux, plus
optional picture enhancements.

> This repository and its packages **contain no game file**: no executable,
> no disc image, no extracted assets. You supply your own disc image. See
> [docs/LEGAL.md](docs/LEGAL.md).

## Approach: hybrid core

The game's original code (the `SLUS_010.68` executable read from *your* disc)
runs on a core written from scratch:

- interpreted R3000A CPU (delay slots, COP0 exceptions, instruction-cache and
  memory-access timing calibrated against hardware measurements), GTE (COP2)
  bit-exact against 1 150 hardware test vectors, GPU with a software
  rasteriser and a drawing-time model, 24-voice SPU with reverb, MDEC, CD-ROM
  controller, DMA, timers and controller port;
- **kernel/BIOS reimplemented at high level (HLE)** — no Sony BIOS is used or
  needed;
- game routines are **progressively replaced by native C code**, each one
  verified bit for bit against the original (`--verify-natives`). Today: the
  game's BS video/picture decoder.

This keeps the behaviour faithful from day one (including the original's
quirks) while the reimplementation advances function by function. See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/NATIVE.md](docs/NATIVE.md).

## What is new in 0.3.2

- Fixed: the game said "No MEMORY CARD" even with `memcard1.mcd` present.
  The HLE kernel now delivers the memory-card events in the same order and
  number as the retail BIOS (a low-level event for every sector, the
  high-level event before the low-level one, "busy" while an operation runs).
  Saving and loading from Game Options > Load and Save work and are covered by
  a test.
- Fixed: a thin vertical line in the middle of full-screen backgrounds at
  internal resolutions above 1x (texture coordinate one texel past the
  picture at the polygon edge).

## What is new in 0.3.1

- Fixed: the launcher closed (crashed) on Windows when a drop-down list was
  opened. The list kept pointers to item texts that only existed while the tab
  was being drawn; the texts are now copied. Found with AddressSanitizer, which
  now runs over every tab and drop-down list with a scripted-click test hook.

## What is new in 0.3.0

- **Launcher** (`nrlauncher`): every setting is chosen before playing; the
  engine only runs when started from it. Launcher texts in English (US, UK,
  Canada), Portuguese (Brazil, Portugal), Spanish (Spain, Mexico, Latin
  America, Argentina, US), Chinese (Simplified: China, Singapore; Traditional:
  Taiwan, Hong Kong, Macau), Japanese and Korean. English (US) is the default.
- **OpenGL 3.3 renderer** (Windows and Linux) next to Direct3D 9/10/11/12 and
  software.
- **Esc pauses** and asks *"Do you want to close the Engine?"* (Yes / No).
- **Intro video at the right speed** (15 fps, audio in sync).
- **Steady physics step**: the game receives a constant time step, so handling
  and collisions no longer change when the frame rate drops.
- **Generic USB controllers** through the system driver *and* by reading the
  USB HID device directly, with button remapping.
- **Debian/Ubuntu `.deb` package** and detailed build instructions.
- All engine, build and documentation texts in English.

## Status (summary)

Validated in practice with the reference image (executable SHA-1
`2726ac3e6f3d756f78982f7c66e507fc3aaf7aff`):

| Stage | Status |
|---|---|
| Disc identification, error messages for missing/invalid files | works |
| Legal screen, EA logos (MDEC video), intro video | works (intro at 15 fps) |
| Title, menus, car and track selection, loading screens | works |
| 3D race (countdown, AI cars, HUD, speedometer, sound) | works |
| Memory card (`.mcd` image, formatted automatically) | works: saving and loading from Game Options > Load and Save verified (0.3.2) |
| Launcher → engine, Esc dialog | verified on Linux (OpenGL) and under Wine (Direct3D 11) |
| OpenGL renderer | verified on Linux (Mesa) up to the race |
| Direct3D 9/10/11 | verified **only under Wine** |
| Direct3D 12 | compiled; **never executed** (no test device); falls back automatically |
| Generic USB controllers | parser covered by unit tests; **not tested with physical controllers** |
| Windows | cross-compiled and run **only under Wine**; not run on real Windows |

Full compatibility with the original game is **not** claimed. Known
differences and pending items are in [docs/STATUS.md](docs/STATUS.md).

## Quick start

**Debian / Ubuntu** (package built with `scripts/build_deb.sh`):

```sh
sudo apt install ./nrengine_0.3.2_amd64.deb
nrlauncher        # or "NR Engine" in the applications menu
```

**Windows** (zip): extract the folder, put your disc image in `gamedata\`
(any name: `.cue`+`.bin`, `.bin` or `.iso`) and start `nrlauncher.exe`.

**From source** (Linux):

```sh
sudo apt install build-essential cmake libsdl2-dev
scripts/build.sh
./build/nrlauncher
```

In the launcher choose your disc image (it is found automatically in
`gamedata/`), adjust graphics and controls, and press **Play!**.

Default keys: arrows = D-pad, **Z** = ✕ (accelerate/confirm), **X** = ◯,
**A** = ▢, **S** = △, **Q/W/E/R** = L1/R1/L2/R2, **Enter** = Start,
**Space** = Select; **Esc** pause/quit dialog, **F11** full screen, **F12**
screenshot. Controllers work through the system driver or directly over USB
([docs/CONTROLLERS.md](docs/CONTROLLERS.md)).

## Documentation

| Document | Contents |
|---|---|
| [docs/BUILD.md](docs/BUILD.md) | dependencies; building on Linux, Windows (MinGW/Visual Studio), cross-compiling; `.deb` package |
| [docs/CONFIG.md](docs/CONFIG.md) | launcher, disc selection (`gamedata/`), file locations, `nrengine.ini`, physics step, command line |
| [docs/GRAPHICS.md](docs/GRAPHICS.md) | OpenGL/Direct3D/software renderers, resolution, AA, filters, effects, frame rate |
| [docs/CONTROLLERS.md](docs/CONTROLLERS.md) | keyboard, system-driver and direct-USB controllers, remapping, Linux udev rule |
| [docs/MODS.md](docs/MODS.md) | `mods/` folder, language packs, file replacement |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | modules, core × platform, execution flow |
| [docs/NATIVE.md](docs/NATIVE.md) | replacing original functions with C and how it is verified |
| [docs/FORMATS.md](docs/FORMATS.md) | game file formats found (CTRL/SHOC, LSC, WVE, AV) |
| [docs/STATUS.md](docs/STATUS.md) | implemented, limitations, differences, pending work |
| [docs/TESTS.md](docs/TESTS.md) | automated tests and diagnostics |
| [docs/LEGAL.md](docs/LEGAL.md) | what is never distributed and why |
| [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) | third-party code and references |

## Programs

- `nrlauncher` — the launcher (settings, disc selection, starts the game).
- `nrengine` — the engine window (started by the launcher).
- `nrengine_headless` — the same engine without a window, for tests and
  diagnostics (PNG captures, WAV output, scripted input).
- `nrtool` — inspection of your image: identification, CRC of every file,
  listing, disassembly, CTRL containers, LSC screens → PNG, AV audio → WAV,
  translation template (`export-text`). Generated files stay where you put
  them and must not be redistributed.
- `nrtests` — unit tests plus regression tests with your disc.

## Licence

Own code under the MIT licence ([LICENSE](LICENSE)). NASCAR Rumble is a
trademark and work of its respective owners; this project is not affiliated
with them.
