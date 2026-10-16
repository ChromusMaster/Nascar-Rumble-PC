# Configuration and use

## 1. Starting the game: the launcher

Players start **`nrlauncher`** (Windows: `nrlauncher.exe`; Linux menu: "NR
Engine"). The launcher is the only place where settings are changed:

| Tab | Settings |
|---|---|
| General | disc image, window/full screen, window size, frame rate, FPS counter, game language (translation mods), launcher language, memory cards |
| Graphics | renderer (OpenGL, Direct3D 9/10/11/12 on Windows, software), VSync, internal resolution, anti-aliasing, colour depth, dithering, texture filtering, scaling, aspect ratio, PGXP, widescreen — see [GRAPHICS.md](GRAPHICS.md) |
| Effects | post-processing, strength, tone mapping, brightness, contrast, saturation, gamma, with a live preview |
| Controls | device, bindings and port 2 for each player — see [CONTROLLERS.md](CONTROLLERS.md) |
| Advanced | native routines, GPU timing model, emulated CPU speed, physics step, renderer threads, log detail, disc check, folders |

**Play!** saves the settings to `nrengine.ini` and starts the engine
(`nrengine`), then the launcher waits; when the game closes, the launcher comes
back (and shows the error, if the engine stopped with one).

The engine window **cannot be started on its own**: the launcher passes it a
one-time session token. Opening `nrengine` directly starts the launcher
instead. While the game runs there are no settings menus; the only keys are:

| Key | Action |
|---|---|
| **Esc** | pauses the game and shows *"Do you want to close the Engine?"*. **Yes** (selected by default, marked with an arrow) closes the engine; **No** resumes. Arrow keys / D-pad choose, Enter / ✕ confirm, Esc again = No |
| **F11** | toggle full screen |
| **F12** | PNG screenshot (in `screenshots/` in the data folder) |

The launcher language (English US by default; English UK/Canada, Portuguese
Brazil/Portugal, Spanish Spain/Mexico/Latin America/Argentina/US, Chinese
Simplified China/Singapore, Chinese Traditional Taiwan/Hong Kong/Macau,
Japanese, Korean) changes only the launcher's own texts. The game's texts
change only through a translation in `mods/Language` ([MODS.md](MODS.md)).
Chinese, Japanese and Korean need a system font with those characters
(Windows has them; on Linux install `fonts-noto-cjk`).

## 2. The disc image

The engine reads **your** image of the NASCAR Rumble disc (USA, SLUS-01068)
directly. Accepted formats:

| Format | Notes |
|---|---|
| `.cue` + `.bin` (MODE2/2352) | recommended (1:1 copy of the disc) |
| `.bin` alone (2352 bytes/sector) | detected by size/sync pattern |
| `.iso` (2048 bytes/sector) | accepted; sector headers are synthesised |

Nothing is extracted to your computer: files are read from the image while
the game runs.

At start-up the image is identified:

- the ISO9660 file system and `SYSTEM.CNF` are read;
- the SHA-1 of the executable `SLUS_010.68` is compared with the reference
  `2726ac3e6f3d756f78982f7c66e507fc3aaf7aff`;
- names and sizes of the 108 files on the disc are checked (the "Check every
  disc file at start" option also checks the CRC32 of each one).

| Situation | Result |
|---|---|
| path does not exist | `File not found: ...` |
| not a CD image / too small | an error that explains why |
| another PlayStation game | refused: it is not NASCAR Rumble |
| NASCAR Rumble with different files (other region, modified) | refused; `--allow-modified` (headless) runs it anyway at your own risk (native routines are enabled only if the original code matches) |

The launcher shows the result in its status line. To check an image without
running the game:

```sh
nrtool info   "NASCAR Rumble (USA).cue"
nrtool verify "NASCAR Rumble (USA).cue"
```

### Where the image is looked for

As in the Silent Hill PC port: a **`gamedata/`** folder where you put your own
image, detected automatically. Order:

1. the image chosen in the launcher (`game.disc`, full path);
2. `game.disc_image`: a file name inside `gamedata/`;
3. the expected names `NASCAR Rumble (USA).cue` / `.bin` inside `gamedata/`;
4. any `.cue`/`.bin`/`.iso`/`.img` in `gamedata/` whose `SYSTEM.CNF` boots
   `SLUS_010.68` — identified by **content**, the file name does not matter.

`gamedata/` is searched in the data folder (section 3), next to the
executables and in the current folder. The launcher lists everything it found
(with the reason an image was refused) and has a **Browse...** button.

## 3. Where files are kept

| Mode | When | Settings (`nrengine.ini`) | Memory cards, screenshots, logs, `gamedata/`, `mods/` |
|---|---|---|---|
| portable | the executables' folder is writable and not under `/usr` or `/opt` (a zip extracted anywhere, a build folder) | next to the executables | next to the executables |
| installed (Linux) | `.deb` or `cmake --install` | `~/.config/nrengine/` (`$XDG_CONFIG_HOME`) | `~/.local/share/nrengine/` (`$XDG_DATA_HOME`); example mods also from `/usr/share/nrengine/mods` |
| installed (Windows) | executables in a read-only folder such as Program Files | `%APPDATA%\NREngine` | `%APPDATA%\NREngine` |

The launcher's Advanced tab shows the folders in use and opens them.

## 4. `nrengine.ini`

Written by the launcher; a commented template is in `nrengine.ini.example`.
Unknown keys and comments are kept when the launcher saves. Keys:

| Section.key | Default | Description |
|---|---|---|
| `game.disc` | — | image path |
| `game.disc_image` | — | image name inside `gamedata/` |
| `game.gamedata` | — | another `gamedata` folder |
| `game.language` | — | language folder in `mods/Language` ([MODS.md](MODS.md)) |
| `game.verify` | 0 | 1 = check the CRC32 of every disc file at start |
| `mods.load` | — | other folders of `mods/`, comma separated |
| `memcard.slot1` | `memcard1.mcd` | memory card in slot 1 (empty = no card) |
| `memcard.slot2` | — | memory card in slot 2 |
| `video.renderer` | `auto` | `auto`, `opengl`, `d3d12`, `d3d11`, `d3d10`, `d3d9`, `software` |
| `video.scale` | 3 | initial window size (320×240 × N) |
| `video.fullscreen` | 0 | 1 = full screen |
| `video.fps` | `original` | `original`, `20`, `30`, `60` |
| `video.show_fps` | 0 | 1 = frame-rate counter |
| `video.internal_resolution` … `video.widescreen` | — | picture enhancements: see [GRAPHICS.md](GRAPHICS.md) |
| `input.<button>` | see [CONTROLLERS.md](CONTROLLERS.md) | player 1 key (SDL key name) |
| `input2.<button>` | — | player 2 keys |
| `input.port2` | 0 | 1 = a controller is plugged into port 2 (2 players) |
| `pad1.device`, `pad2.device` | `auto` | `auto`, `none`, `sdl:...`, `hid:...` |
| `pad1.<button>`, `pad2.<button>` | — | controller bindings (empty = default) |
| `pad1.left_stick_dpad` | 1 | left stick also moves the D-pad |
| `engine.native_overrides` | 1 | 0 = run only the original code |
| `engine.gpu_timing_model` | 1 | 0 = instant GPU (diagnostics; changes the game's pace) |
| `engine.cpu_overclock` | 0 | emulated CPU multiplier (0 = automatic: 1, or 3–4 in the 60 fps mode) |
| `engine.physics_step` | `auto` | `auto`, `original`, `30`, `60` — see section 5 |
| `engine.threads` | 0 | software renderer threads (0 = automatic) |
| `debug.log_level` | 2 | 0 error, 1 warning, 2 info, 3 debug, 4 trace |
| `debug.log_file` | — | log file; the launcher always writes the engine log to `nrengine.log` in the settings folder |
| `launcher.language` | `en-US` | launcher language (BCP 47 code) |

Buttons: `up`, `down`, `left`, `right`, `cross`, `circle`, `square`,
`triangle`, `l1`, `r1`, `l2`, `r2`, `start`, `select`. Comments start with `;`
or `#`.

## 5. Physics step (handling and collisions)

NASCAR Rumble measures how long each game frame took (in 1/300 s ticks),
averages the last four frames and moves the cars, the camera and the collision
tests by that amount. On the console the frame time keeps changing (from about
1/60 s to 1/30 s or longer, depending on how much is on screen), and the
original simulation is not fully independent of that step: the same input
produced different speeds when only the frame timing changed (measured from
one race snapshot, see [STATUS.md](STATUS.md)). Longer steps also let the
collision probes move further between two tests, which matches the reported
symptoms (cars feeling heavier or lighter at certain points, colliding with
something that is not visibly there, or falling where there is still road).

`engine.physics_step` makes the step constant:

| Value | Behaviour |
|---|---|
| `auto` (default) | 30 Hz steps; 60 Hz steps in the 60 fps mode |
| `30` | every game frame lasts exactly 1/30 s (two video fields). The game can still show fewer frames when the scene is heavy; it then slows down slightly instead of taking a longer step |
| `60` | every game frame lasts 1/60 s. Needs `video.fps = 60` (the engine raises the emulated CPU to 4× for it); otherwise 30 is used |
| `original` | the console behaviour (variable step) |

With a constant step, a race replayed from the same snapshot with the
emulated CPU at 1×, 2× and 4× gives pixel-identical pictures — the handling no
longer depends on the frame rate. The game's code and data are not changed:
the engine waits inside the game's own frame-timing loop (the same way the
game's replay mode already does) and hands the game a fixed step. It is an
enhancement: choose `original` for console-exact timing.

## 6. Memory cards

128 KB raw images (`.mcd`/`.mcr`, the format used by other emulators). A
missing file is created and formatted. Saves are written to the file at once.

## 7. Command line (diagnostics)

The window engine takes no user options (the launcher writes everything to
`nrengine.ini`). `nrengine_headless` runs the same engine without a window and
accepts the settings as options, for tests and bug reports:

```
nrengine_headless [options] [disc image .cue/.bin/.iso]
```

| Option | Description |
|---|---|
| `--config FILE` | configuration file (default: `nrengine.ini` in the user folder) |
| `--memcard1 FILE`, `--memcard2 FILE` | memory cards (`""` = none) |
| `--pad2` | controller in port 2 |
| `--gamedata DIR`, `--list-discs` | `gamedata` folder / list the images found |
| `--language NAME`, `--mod LIST` | language and other mods ([MODS.md](MODS.md)) |
| `--renderer R` | renderer name (headless always draws with the software renderer) |
| `--internal-res N`, `--aa N`, `--true-color`, `--no-dither`, `--filter N`, `--scaling M`, `--postfx M`, `--tonemap M`, `--pgxp`, `--widescreen` | picture enhancements |
| `--fps MODE` | `original`, `20`, `30`, `60` |
| `--overclock N`, `--physics-step S`, `--threads N` | emulated CPU, physics step, renderer threads |
| `--verify`, `--allow-modified` | disc checks |
| `--no-native`, `--verify-natives`, `--gpu-instant` | engine internals ([NATIVE.md](NATIVE.md)) |
| `--log-level N`, `--log-file FILE` | logging |
| `--frames N` | stop after N video frames |
| `--screenshot-every N`, `--screenshot-at A,B`, `--screenshot-dir DIR` | PNG captures |
| `--vram-at N`, `--ram-at A,B` | VRAM (PNG) / main RAM (binary) dumps |
| `--input-script FILE` | scripted input (below) |
| `--wav FILE` | write the SPU output (44.1 kHz stereo) |
| `--save-state-at N:FILE`, `--load-state FILE` | machine snapshots for local analysis (they contain game memory: never share them) |
| `--trace`, `--dump-hle`, `--peek ADDR`, `--watch-pc ADDR`, `--watch-write ADDR`, `--profile-pc A,B` | debugging |

### Scripted input

Text, one line per change: `frame buttons`, where buttons is `none` or a
comma-separated list of
`select,start,up,right,down,left,l2,r2,l1,r1,triangle,circle,cross,square`.
For port 2 add `| buttons` (e.g. `100 cross | start`). The state holds until
the next line. Example (skips screens and accelerates):

```
3610 cross
3618 none
4700 cross
```
