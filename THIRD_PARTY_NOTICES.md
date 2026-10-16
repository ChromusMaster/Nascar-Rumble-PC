# Third-party notices

## Libraries

### SDL2 (`nrlauncher` and `nrengine` only)

Simple DirectMedia Layer — <https://www.libsdl.org/> — used for the window,
audio and input. It is not included in the source tree; it is obtained when
building (Linux: the distribution's `libsdl2` package). When `SDL2.dll` (or
another binary form) is distributed with executables built from this project,
the zlib licence applies:

```
Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

### stb_truetype (launcher text rendering)

`src/third_party/stb_truetype.h` v1.26 by Sean Barrett and contributors,
<https://github.com/nothings/stb>. Dual-licensed by its authors as **public
domain (Unlicense)** or **MIT**; the file keeps its original licence text at
the end. It is used to draw the launcher's text with fonts already installed
on the user's system (no font file is distributed).

### font8x8 (in-game overlay font)

`src/ui/font8x8.c` was generated from **font8x8** by Daniel Hepper
(<https://github.com/dhepper/font8x8>), based on the IBM PC fonts by Marcel
Sondaar; **public domain**.

### fontconfig (Linux, optional)

`libfontconfig.so.1` is loaded at run time with `dlopen` to find system fonts
for the launcher (`fontconfig` package, MIT-style licence). It is not linked
or distributed.

### Operating-system components (Windows)

`d3d9.dll`, `d3d10_1.dll`, `d3d11.dll`, `d3d12.dll`, `dxgi.dll`,
`d3dcompiler_47.dll`, `opengl32.dll`, `hid.dll` and `setupapi.dll` are
Windows components loaded or linked at run time; they are not distributed
with the engine.

No other third-party library is used. Wine, vkd3d, Mesa (llvmpipe), Xvfb and
llvm-mingw were used for testing and cross-compiling during development;
none of them is distributed.

## Reference documentation and test data

- **psx-spx** — "PlayStation Specifications" (Martin Korth / nocash and
  contributors), <https://psx-spx.consoledev.net/>. Used as the reference for
  hardware and kernel behaviour. No text was copied; some **hardware constant
  tables** were transcribed from it because they are needed to reproduce the
  hardware: the SPU Gaussian interpolation table (512 entries), the GTE UNR
  division table, the BS format Huffman codes and the MDEC zig-zag/scale
  tables. The document states that it claims no copyright on its content.
- **ps1-tests** by Jakub Czekański, <https://github.com/JaCzekanski/ps1-tests>
  (MIT licence). Its GTE test vectors (`gte/test-all`, values recorded on a real
  console) and the CPU memory access-time log (`cpu/access-time`) were used
  **locally** to validate the GTE and calibrate the CPU memory timing. No file
  of that project is included here.
- USB HID Usage Tables 1.4 and Device Class Definition for HID 1.11 (USB
  Implementers Forum) for the HID report-descriptor parser.
- [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) and
  the SDL forum report on DragonRise layouts were consulted for generic
  controller layouts (no data copied; SDL ships its own database).

## Projects consulted (no code copied)

Provided as references for how reimplementation projects are organised:

- REDRIVER2 (Driver 2 reimplementation) — its FMV player paces video by the
  stream's own frame time; the same principle guided the 0.3.0 intro fix,
  implemented independently here;
- Silent Hill decompilation / PC port — the **organisation** of disc selection
  (`gamedata/`, `disc_image`, detection by the boot executable), the launcher
  concept and the set of graphics enhancements were replicated from that
  port's documented behaviour; the implementation here is our own;
- PS2Recomp.

No code from these projects was incorporated.
