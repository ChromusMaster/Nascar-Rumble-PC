# Building NR Engine

This document explains, step by step, how to build NR Engine from source on
Linux (including the `.deb` package for Debian, Ubuntu and derivatives), on
Windows (MinGW-w64 or Visual Studio) and with a Linux → Windows cross
compiler. No game file is needed to build or to run the unit tests.

## 1. What gets built

| Program | Needs SDL2 | Purpose |
|---|---|---|
| `nrlauncher` | yes | **the program players start**. Every setting is chosen here; "Play!" starts the engine |
| `nrengine` | yes | the engine window (video, audio, controllers). It only runs when started by the launcher; if it is opened directly it starts the launcher and exits |
| `nrengine_headless` | no | the same engine without a window, for tests and diagnostics (PNG captures, WAV output, scripted input) |
| `nrtool` | no | inspection of your disc image (identification, CRC32, file list, disassembly, translation template) |
| `nrtests` | no | unit tests, plus regression tests when `NR_DISC` points at your disc image |

## 2. Requirements

| Item | Version | Notes |
|---|---|---|
| CMake | 3.13 or newer | validated with 3.28.3 |
| C compiler | any C99 compiler: GCC, Clang, MinGW-w64, MSVC 2019+ | validated: GCC 13.3 (Linux x86_64), clang 18 / llvm-mingw 20240619 (Windows x86_64 cross build). MSVC was **not** tested |
| SDL2 | 2.0.18 or newer (validated 2.30.9 on Windows, 2.30.0 on Ubuntu 24.04) | only for `nrlauncher` and `nrengine` |
| OpenGL headers | none | OpenGL functions are loaded at run time through SDL; no GLEW/GLAD/libGL development package is needed |
| Direct3D SDK | none | Direct3D 9/10/11/12 headers come with MinGW-w64 and the Windows SDK; the DLLs are loaded at run time |
| dpkg-dev | any | only to build the `.deb` package |

Everything else (PNG, WAV, SHA-1, CRC32, ISO9660, INI, HID report parsing,
TrueType rendering through the bundled `stb_truetype.h`) is part of the source
tree. The build never downloads anything.

Optional at run time on Linux: `fontconfig` (finds system fonts for the
launcher; loaded with `dlopen`, not linked) and `fonts-noto-cjk` (needed only
to show the launcher in Chinese, Japanese or Korean). Latin texts use the
font embedded in the launcher (NREngine Sans), so no system font is needed
for English, Portuguese or Spanish.

### Launcher font (maintainers only)

`src/launcher/lfont.c` is generated and committed; building never needs
Python. To change the font, edit `scripts/gen_launcher_font.py` and run it
from the repository root (needs `fontTools` and `skia-pathops`; `brotli`
for WOFF2):

```sh
pip install fonttools skia-pathops brotli
python3 scripts/gen_launcher_font.py                 # rewrites src/launcher/lfont.c
python3 scripts/gen_launcher_font.py --out fonts     # also writes .ttf, .otf, .woff, .woff2
```

The standalone files written by `--out` are for previews and other uses only;
they are not part of the source tree.

## 3. Linux (any distribution)

### 3.1 Install the build dependencies

```sh
# Debian, Ubuntu, Linux Mint, Pop!_OS, elementary, Raspberry Pi OS
sudo apt update
sudo apt install build-essential cmake libsdl2-dev

# Fedora / RHEL / CentOS Stream (EPEL for SDL2 on RHEL)
sudo dnf install gcc make cmake SDL2-devel

# openSUSE
sudo zypper install gcc make cmake libSDL2-devel

# Arch Linux / Manjaro
sudo pacman -S --needed base-devel cmake sdl2
```

### 3.2 Build with the helper script

```sh
cd nrengine-0.3.2
scripts/build.sh                 # Release build in ./build
scripts/build.sh Debug           # debug build
BUILD_DIR=out scripts/build.sh   # other build folder
```

The script configures with CMake, compiles with one job per CPU core and lists
the executables. If SDL2 is missing, only `nrengine_headless`, `nrtool` and
`nrtests` are built and the script says which package to install.

### 3.3 Build by hand (what the script does)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel "$(nproc)"
```

Useful variables:

| Variable | Example | Effect |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `Release`, `Debug`, `RelWithDebInfo` | optimisation level (default `Release`) |
| `SDL2_DIR` | `/opt/SDL2/lib/cmake/SDL2` | SDL2 installed outside the system paths |
| `CMAKE_C_COMPILER` | `clang` | choose the compiler |
| `CMAKE_INSTALL_PREFIX` | `/usr/local` | where `cmake --install` puts files |
| `NR_BUILD_SDL2` | `OFF` | build only the programs without a window |
| `NR_BUILD_TESTS` | `OFF` | do not build `nrtests` |
| `NR_UDEV_DIR` | `lib/udev/rules.d` | where the controller udev rule is installed (relative to the prefix) |

### 3.4 Run from the build folder (portable mode)

```sh
./build/nrlauncher
```

When the executables sit in a folder you can write to (and not under `/usr`
or `/opt`), NR Engine runs in **portable mode**: `nrengine.ini`, memory cards,
screenshots, `gamedata/` and `mods/` live next to the executables. Put your
disc image in `build/gamedata/` or choose it with the launcher's "Browse..."
button.

### 3.5 Run the tests

```sh
cd build
./nrtests                                     # unit tests, no game data needed
NR_DISC="/path/NASCAR Rumble (USA).cue" ./nrtests   # also regression tests with your disc
ctest --output-on-failure                     # the same through CTest
```

### 3.6 Install system-wide without a package

```sh
sudo cmake --install build --prefix /usr/local
sudo udevadm control --reload-rules && sudo udevadm trigger   # controller rule
nrlauncher
```

Installed layout (prefix `/usr/local` shown):

| Path | Contents |
|---|---|
| `/usr/local/games/` | `nrlauncher`, `nrengine`, `nrengine_headless`, `nrtool` |
| `/usr/local/share/nrengine/` | `mods/` (example language pack), `nrengine.ini.example` |
| `/usr/local/share/doc/nrengine/` | README, licence, third-party notices, these documents |
| `/usr/local/share/applications/nrengine.desktop` | menu entry ("NR Engine") |
| `/usr/local/share/icons/hicolor/scalable/apps/nrengine.svg` | icon (original artwork, CC0) |
| `/usr/local/lib/udev/rules.d/70-nrengine-gamepads.rules` | read access to USB game controllers for direct reading (see [CONTROLLERS.md](CONTROLLERS.md)) |

An installed engine stores its files per user, following the XDG base
directories: settings in `~/.config/nrengine/nrengine.ini`; memory cards,
screenshots, logs, `gamedata/` and your own `mods/` in
`~/.local/share/nrengine/`. The example mods from `/usr/share/nrengine/mods`
are found as well.

Uninstall: `xargs sudo rm < build/install_manifest.txt`.

## 4. Debian / Ubuntu package (.deb)

### 4.1 Target distributions

The package is built against the oldest current stable release so that one
`.deb` installs on both supported families:

| Distribution | glibc | SDL3 package | Kernel shipped |
|---|---|---|---|
| Debian 13 "trixie" (13.7, September 2026) | 2.41 | `libsdl3-0` 3.2.10 | 6.12 LTS |
| Ubuntu 26.04 LTS "Resolute Raccoon" | 2.43 | `libsdl3-0` 3.4.2 | 7.0 |

The kernel version does not matter for this package: NR Engine is an ordinary
user-space program that talks to the kernel only through glibc, SDL3 and the
`hidraw` interface, which have been stable for many years. What decides
compatibility is the glibc version and the SDL3 version the binaries were
linked against, which is why the package is built on Debian 13.

### 4.2 One command on Debian or Ubuntu

```sh
sudo apt install build-essential cmake libsdl3-dev dpkg-dev file
scripts/build_deb.sh
```

The script configures a Release build in `build-deb/` with prefix `/usr`,
compiles every program, runs the unit tests (no game data involved), runs
CPack with the DEB generator (`dpkg-shlibdeps` fills `Depends:` with the
libraries the binaries actually use) and writes
`dist/nrengine_<version>_<arch>.deb`.

### 4.3 From Windows (Docker Desktop)

The `.deb` must be built by a Linux toolchain. With Docker Desktop running:

```bat
scripts\build_deb_docker.bat
```

or, from a Linux or WSL shell, `scripts/build_deb_docker.sh`. Both start a
throw-away `debian:13` container, install the build dependencies, copy the
source tree (read-only mount), run `scripts/build_deb.sh` and write the
package to `dist\`. Another image can be passed as the first argument (for
example `ubuntu:26.04`); a package built on a newer distribution does not
install on an older one.

### 4.4 By hand

```sh
cmake -S . -B build-deb -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DNR_SDL_VERSION=3
cmake --build build-deb --parallel "$(nproc)"
cd build-deb && cpack -G DEB
```

With an SDL3 built from source (no `libsdl3-dev` package for `dpkg-shlibdeps`
to query) add `-DNR_DEB_SHLIBDEPS=OFF`; the fixed list
`libc6 (>= 2.34), libsdl3-0 (>= 3.2.0)` is used instead.

### 4.5 Check and install

```sh
dpkg-deb -f         dist/nrengine_*_amd64.deb
dpkg-deb --contents dist/nrengine_*_amd64.deb
sudo apt install ./dist/nrengine_*_amd64.deb
nrlauncher
sudo apt remove nrengine
```

Package details:

| Field | Value |
|---|---|
| Name | `nrengine` |
| Section / priority | `games` / `optional` |
| Depends | computed by `dpkg-shlibdeps` (libc6, libsdl3-0) |
| Recommends | `fontconfig`, `fonts-noto-cjk` |
| Suggests | `libgl1` (OpenGL driver) |
| Maintainer scripts | `postinst` reloads the udev rules and the icon cache |

The package **contains no game data**. After installing, each user selects
their own disc image in the launcher (or copies it to
`~/.local/share/nrengine/gamedata/`).

To build for another architecture (for example `arm64` on a Raspberry Pi 4/5
with 64-bit Raspberry Pi OS), run the same commands on that machine; the
engine is portable C99 and does not use x86-specific code.

## 5. Windows

### 5.1 MinGW-w64 / llvm-mingw (recommended)

`scripts\build_windows.bat` builds with warnings treated as errors and then
runs the unit tests (`ctest`). A build that produces any warning fails.

1. Install CMake from <https://cmake.org/download/> (tick "Add CMake to the
   PATH").
2. Install a compiler, either
   - [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases)
     (`llvm-mingw-<date>-ucrt-x86_64.zip`, extract and add its `bin` folder to
     PATH), or
   - [MSYS2](https://www.msys2.org/) and, in the "UCRT64" shell,
     `pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,SDL2,make}`.
3. Download `SDL2-devel-2.30.x-mingw.zip` from
   <https://github.com/libsdl-org/SDL/releases> and extract it, for example to
   `C:\libs\SDL2-2.30.9`.
4. In a Command Prompt in the source folder:
   ```bat
   scripts\build_windows.bat C:\libs\SDL2-2.30.9
   ```
   `SDL2.dll` is copied next to the executables automatically.
5. Start `build\nrlauncher.exe`.

With MSYS2 you can also run `scripts/build.sh` inside the UCRT64 shell (SDL2
comes from pacman, so no path is needed). Copy `SDL2.dll` from
`/ucrt64/bin` next to the executables before distributing them.

### 5.2 Visual Studio 2019/2022

1. Install Visual Studio with the "Desktop development with C++" workload
   (includes CMake and the Windows SDK).
2. Download `SDL2-devel-2.30.x-VC.zip` and extract it.
3. In "x64 Native Tools Command Prompt for VS":
   ```bat
   scripts\build_windows.bat C:\libs\SDL2-2.30.9
   ```
   The executables are in `build\Release\`.

   Alternatively open the source folder with "File → Open → Folder" and let
   Visual Studio configure CMake; set `SDL2_DIR` to `C:\libs\SDL2-2.30.9\cmake`
   in `CMakeSettings.json`.

> MSVC builds were **not** tested for this release. The code is plain C99
> with designated initialisers, which MSVC 2019+ accepts; report any
> error with the compiler output.

### 5.3 Windows 7 to Windows 11

The executables target Windows 7 SP1 and later (`_WIN32_WINNT=0x0601` is
defined for every Windows build, so an API newer than Windows 7 is a compile
error instead of a start-up failure). The imports of `nrengine.exe`,
`nrlauncher.exe` and `nrengine_headless.exe` were checked: `kernel32`,
`user32`, `shell32`, `comdlg32`, `bcrypt`, `hid` and `setupapi` functions that
all exist on Windows 7. Direct3D 10/11, DXGI and the shader compiler are
loaded at run time; Direct3D 12 exists only on Windows 10 and later, and on
older systems the engine falls back to Direct3D 11, OpenGL, Direct3D 9 or the
software renderer. SDL3 supports every desktop Windows version back to XP.

The C runtime depends on the toolchain:

| Toolchain | C runtime | Windows 7 / 8 / 8.1 |
|---|---|---|
| MinGW-w64 "UCRT" builds (WinLibs UCRT, MSYS2 UCRT64, llvm-mingw `ucrt`) | Universal CRT (`api-ms-win-crt-*.dll`) | needs update KB2999226 or a later cumulative update |
| MinGW-w64 "MSVCRT" builds (WinLibs MSVCRT, MSYS2 MINGW64, llvm-mingw `msvcrt`) | `msvcrt.dll` | works without updates |
| Visual Studio | Universal CRT | needs KB2999226 |

`scripts\build_windows.bat` reports which runtime the compiler in `PATH`
uses. For a release meant to run on an unpatched Windows 7, build with an
MSVCRT toolchain. Windows 7 needs SP1 and, for the Direct3D 11 renderer,
the Platform Update (KB2670838); without it the engine uses another renderer.

### 5.4 What the Windows build uses

- Direct3D 9/10/11/12, DXGI and `d3dcompiler_47.dll` are loaded at run time;
  a missing component only disables that renderer.
- OpenGL comes from the graphics driver (`opengl32.dll`).
- Direct USB controller reading uses `hid.dll` and `setupapi.dll`, part of
  every Windows version since XP. No driver installation is needed.
- Files: portable mode when the folder of the executables is writable
  (settings and saves next to them); otherwise `%APPDATA%\NREngine`.

## 6. Cross-compiling Linux → Windows

```sh
# llvm-mingw from https://github.com/mstorsjo/llvm-mingw/releases, extracted to /opt/llvm-mingw
# SDL2-devel-2.30.9-mingw.zip extracted to /opt/SDL2-2.30.9
cmake -S . -B build-win \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
  -DMINGW_ROOT=/opt/llvm-mingw \
  -DSDL2_ROOT=/opt/SDL2-2.30.9/x86_64-w64-mingw32 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-win --parallel "$(nproc)"
```

The Windows binaries of this release were produced this way and executed under
Wine 11 for testing. Distribute `SDL2.dll` (zlib licence, see
`THIRD_PARTY_NOTICES.md`) together with the `.exe` files.

## 7. Troubleshooting

| Message | Cause and fix |
|---|---|
| `SDL2 not found: only nrengine_headless, nrtool and nrtests are built` | install `libsdl2-dev` (Linux) or pass the SDL2 package folder (Windows) |
| `Could NOT find Threads` | install the compiler's development files (`build-essential`) |
| launcher shows squares instead of Chinese/Japanese/Korean letters | install `fonts-noto-cjk` (Linux); on Windows the system fonts are used |
| `OpenGL 3.3 core context: ...` in the log | the driver has no OpenGL 3.3; the engine falls back to the software renderer automatically. Update the driver or choose "Software" |
| `permission denied for /dev/hidrawN` | install the udev rule (see [CONTROLLERS.md](CONTROLLERS.md)) or use the controller through the system driver |
| `dpkg-shlibdeps: error: no dependency information found` | build from a clean folder with the distribution's own compiler and SDL2 package |

## 8. Reproducibility

- Plain C99, no code generation at build time and no downloads.
- The emulation is deterministic: the same image and the same scripted input
  give the same VRAM, frame by frame. The regression tests compare VRAM CRC32
  values with `tests/golden_slus01068.txt` (see [TESTS.md](TESTS.md)).
- Real time only affects the presentation pace in the window; the emulated
  machine always advances in whole video frames.
