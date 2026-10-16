# NASCAR Rumble file formats (SLUS-01068)

Descriptions obtained by reverse engineering the user's copy and, where
noted, validated against what the original program produces at run time. No
game data accompanies this document — only the structure.
Confidence: **[V]** validated against the running original; **[E]** structure
confirmed on every file of the disc; **[H]** hypothesis not yet confirmed.

## Disc

A single MODE2/2352 data track (111 075 sectors), every file in Form 1. There
are no CD-DA audio tracks and no XA-ADPCM sectors **[E]**.

| Type | Count | Contents |
|---|---|---|
| `SLUS_010.68` | 1 | PS-X executable (entry `800A5440`, loaded at `80010000`, 0x9F800 bytes; PsyQ libraries 1997–98) |
| `SYSTEM.CNF`, `DUMMY.DAT` | 2 | boot configuration; padding |
| `GLBLDATA.PSX` | 1 | CTRL/SHOC container with global data (172 CTRL groups from `0x3C024`, every `0x12000` bytes) **[E]** |
| `*.TRK` | 22 | CTRL/SHOC containers of the tracks (models, textures, paths, embedded audio) **[E]** |
| `*.LSC` | 70 | loading/help screens (MDEC) **[V]** |
| `*.WVE` | 5 | EA videos (logos, intro) **[V]** |
| `*.AV` | 7 | EA SWVR streaming audio (music/speech) **[E]** |

Folders: `CW/ABOUTEA`, `CW/FEND`, `CW/HELPSCRN`, `CW/OPENING` and `CW/LOC*`
(7 locales).

## CTRL/SHOC container (`.TRK`, `GLBLDATA.PSX`) **[E]**

A sequence of blocks. Each block starts with a FourCC stored as a
little-endian `u32` (the bytes appear reversed in the file: `LRTC` = CTRL),
followed by the block's total size (`u32`).

| Block | Structure |
|---|---|
| `CTRL` | group header (0x18 bytes); `+0x14` = group size |
| `SHOC` | `+0x08`, `+0x0C` zero; `+0x10` subtype FourCC |
| `SHOC/SHDR` | resource header: `+0x14` version, `+0x18` type (FourCC), `+0x1C` id, `+0x20` data size |
| `SHOC/SDAT` | data from `+0x14`, appended to the current resource until its size is reached |
| `FILL` | padding up to the next `0x6000`-byte boundary; if fewer than 8 bytes remain, only the 4-byte tag is present |
| `SWVR`, `VAGB`, `VAGM`, `SYNC` | embedded audio (see `.AV`) |

Resource types seen: `RPNS`, `Cshd`, `Cvkh`, `Cvkb`, `Cpag`, `Ctrk`, `Ctos`,
`Cnet`, `Cdcs`, `Cobj`, `Cact`, `Cctr`, `TkFE`, `Cfnt`, `Bmdc`, `Crle`,
`Lmdc`, `Tmdc`, `Gmdc`, `McIc`, `TxtR`, `Csfx`. The meaning of each type is
**not** documented yet (pending; the game interprets them with its own code).

`nrtool ctrl IMAGE FILE` lists the blocks and resources.

## `.LSC` — still screens **[V]**

```
u32 sizeA, u32 sizeB
part A (sizeA bytes): u16 width, u16 height, BS v2 frame (8-byte header + bitstream)
part B (sizeB bytes): same
```

In all 70 files the parts are 160×256: A is the left half and B the right half
of a 320×256 picture. The game decodes it with the MDEC in 24-bit and converts
it to 15-bit in VRAM with a deterministic **4×4 ordered dither** (depends only
on `x&3`, `y&3`); the approximate matrix
`[[0,2,-1,1],[4,-2,3,-3],[-1,1,0,2],[3,-3,4,-2]]` reproduces ~99.5 % of the
pixels **[H]** as to its exact form. `nrtool lsc` writes a 24-bit PNG without
that dither.

Quantisation table used by the game for Y and UV (zig-zag order):
`2,16,16,19,16,19,22,22,22,22,22,22,26,24,26,27,27,27,26,26,26,26,27,27,27,29,29,29,34,34,34,29,29,29,27,27,29,29,32,32,34,34,37,38,37,35,35,34,35,38,38,40,40,40,48,48,46,46,56,56,58,69,69,83`
(the standard MPEG-1 intra matrix, also the PsyQ libraries' default), with the
standard scale table.

## `.WVE` — EA video **[V]**

A sequence of blocks with a readable FourCC and a **big-endian** size:

| Block | Contents |
|---|---|
| `VLC0` | 0x1C8 bytes: table of 0xE0 MDEC values (`u16`) defining EA's variant of the BS coding (replaces the standard table) |
| `MDEC` | video frame: width, height, frame number (BE) and BS frame in EA format |
| `au00` / `au01` | SPU-ADPCM audio in 16-byte blocks (left channel, then right), pitch `0x0800` = 22 050 Hz |

The intro (`INTRO.WVE`) has 567 `MDEC` frames and an `au00` block every four
frames: 15 frames per second of video, streamed from the disc in bursts of
about seven sectors. The game decodes each frame as soon as its sectors
arrive, so playback speed depends on CD interrupts being serviced on time
(see [NATIVE.md](NATIVE.md)).

## `.AV` — streaming audio (EA SWVR) **[E]**

Groups aligned to `0x6000` bytes:

| Block | Structure |
|---|---|
| `SWVR` | 0x24 bytes; stream name at `+0x14` (e.g. `xxx.stream`) |
| `VAGB` | mono clip: SPU-ADPCM data from `+0x18`; the last block has the end flag (1) |
| `VAGM` | music block: `+0x14 u16` total blocks, `u16` index; `+0x18 u32` bytes per channel; data from `+0x1C`, left channel then right |
| `FILL` | padding up to `0x6000` |

The sample rate is **not** stored in the headers **[H]**: the game sets it
when programming the SPU (pitches seen between `0x514` and `0x5CD`).
`nrtool swvr-wav` writes 22 050 Hz by default; listening at another rate may
be needed.

## BS coding (MDEC) **[V]**

- Sony standard v1/v2: Huffman codes publicly documented (psx-spx).
- EA variant: the same 224 code lengths, in its own index order, mapped to the
  0xE0 MDEC values of `VLC0` (or of the default table built into the
  executable, read at run time from the user's image).
- Details of the game's decoder in [NATIVE.md](NATIVE.md).

## Game timing variables (run-time observation) **[V]**

Not a file format, but documented here for the physics-step work:

| Address | Meaning |
|---|---|
| `gp+0x654` (`800AF738`) | tick counter, advanced by 5 every video field (300 Hz) |
| `gp+0x634` (`800AF718`) | tick value at the previous game frame |
| `gp+0x5FC` (`800AF6E0`) | step handed to the simulation: average of the last four frame times (in ticks, remainder carried), clamped to 25 |
| `gp+0x5D4..0x5DA` | history of the last frame times and the carried remainder |
| `gp+0x644` (`800AF728`) | game frame counter of the race loop |
| `800269C0` | race frame-timing routine (wait, measure, average, then call the vehicle/camera updates with the step) |
| `80074860` / `80074AC8` | **[H]** replay: read / record the step of each frame (a recorded step makes the frame routine wait for exactly that step) |
