# Legal and distribution

## What this project contains

Only source code, build scripts, packaging files, tests and documentation
written for this project, under the MIT licence (`LICENSE`), plus the
third-party files listed in `THIRD_PARTY_NOTICES.md` (`stb_truetype.h`,
public domain/MIT). The icon in `packaging/linux/nrengine.svg` is original
artwork made for this project (CC0).

## What this project never contains

- the game's executable (`SLUS_010.68`) or any part of its code;
- disc images (`.bin`, `.cue`, `.iso`, `.chd`, …);
- files from the disc (`.TRK`, `.LSC`, `.WVE`, `.AV`, `GLBLDATA.PSX`…) or data
  extracted from them (textures, models, sounds, videos, screenshots, texts);
- a PlayStation BIOS (the engine needs none: the kernel is reimplemented at
  high level from public documentation);
- saves / memory cards, machine snapshots.

This applies to the source archive, the Windows zip and the Debian package
alike. The user supplies their **own legal copy** of the game by selecting the
image in the launcher (or placing it in `gamedata/`). Files are read from that
image while the game runs; the engine writes no copy of them and **never
modifies any game asset** — every enhancement acts on the picture shown, on
timing, or on data the user supplies (mods), in memory only. Tools such as
`nrtool lsc`/`swvr-wav`/`export-text` and the diagnostic options
`--ram-at`/`--save-state-at` only write files where the user asks, for
personal use — those files must not be redistributed.

## Identification data included

To recognise the right image and protect the native routines, the code holds
only **non-expressive metadata**:

- names, sizes and CRC32 of the files on the disc
  (`src/disc/manifest_slus01068.c`);
- the SHA-1 of the executable;
- routine addresses and a few instruction words per routine as a signature
  (`src/native/native.c`, `src/native/pace.c`, `src/mods/mods.c`), used only
  to check that the code in memory is the expected one before replacing or
  observing it;
- VRAM CRC32 values for regression (`tests/golden_slus01068.txt`).

None of these values allows rebuilding game content.

## Reverse engineering

The documented formats and routines were found by observing the original
program running on the engine, from the user's copy, for interoperability.
The hardware tables (SPU Gaussian interpolation, GTE division, BS Huffman
codes, MDEC tables) follow public technical documentation of the PlayStation
(see `THIRD_PARTY_NOTICES.md`).

## Package check

`scripts/check_package.py` walks a folder (or a ZIP) and fails if it finds
disc image extensions, PS-X executables, file names from the game disc, binary
files or captures. It is run on every source archive before delivery. The
binary packages are checked by listing their contents (only the engine's own
programs, `SDL2.dll` on Windows, documentation and the example language pack
with translations written for the project).

## Trademarks

NASCAR, NASCAR Rumble, EA SPORTS and PlayStation are trademarks of their
respective owners. This project is independent and is not affiliated with,
sponsored or endorsed by them.

*This document describes the project's practices and is not legal advice.*
