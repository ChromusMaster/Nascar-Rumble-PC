# SDL3 support and compiler warning fixes

## Scope

This update continues the earlier remediation without replacing its fixes or executable icons. The 15 Markdown files supplied in the original ZIP remain byte-identical. No source comments were added during this SDL update. This new English document records the changes and current validation; tool availability and SDL validation described here supersede the earlier remediation report for this update.

## Changes

- Added `src/platform/nr_sdl.h` to support native SDL3 and retain SDL2 compatibility. SDL3 builds disable legacy SDL names. The adapter handles boolean API results, window creation and fullscreen, renderer creation, float rectangles and mouse coordinates, pixel capture surfaces, keyboard state, native Windows window handles, OpenGL drawable dimensions and audio streams.
- Migrated frontend, launcher and input code to the adapter. SDL3 enumerates joystick instance IDs instead of SDL2 device indices. Existing controller binding names remain compatible.
- Added CMake selection through `NR_SDL_VERSION=AUTO`, `2` or `3`. Automatic selection prefers SDL3. Minimum supported versions are SDL3 3.2 and SDL2 2.0.18. `NR_REQUIRE_SDL=ON` or an explicitly selected major version makes a missing development package fatal instead of silently skipping the launcher.
- Preserved `NR_BUILD_SDL2` as the initial default for the new `NR_BUILD_SDL` option. Use the new option explicitly when changing an existing build directory.
- Reworked the Windows batch script to discover actual SDL CMake package files, distinguish conventional Visual Studio and MinGW development package layouts, use separate build directories and verify both graphical executables before reporting success. CMake copies shared SDL runtime libraries alongside the graphical executables.
- Corrected the six integer-to-float assignments in `src/render/gpu_replay.c` associated with the reported MSVC C4244 warnings. Explicit casts follow selection of byte-valued color components; all values from 0 through 255 are represented exactly by float. No warning suppression was added.
- Updated fallback Debian dependencies to follow the selected SDL major version and omit SDL for headless-only builds.
- Preserved launcher and headless ICO, PNG and SVG assets and Windows resource integration under `packaging/icons/`.

## Building on Windows

Install CMake 3.16 or newer and the appropriate C compiler. Extract the complete SDL3 development SDK, not only its runtime DLL or documentation directory. For the Visual Studio SDK, use Visual Studio with C++ desktop build tools installed. For the conventional MinGW SDK layout, use an x86_64 MinGW toolchain in PATH.

From the project directory:

```bat
scripts\build_windows.bat C:\SDL3 Release
```

For the Visual Studio SDK, outputs are normally under `build-sdl3-msvc\Release`. For MinGW, they are under `build-sdl3-mingw`. Start `nrlauncher.exe`. The headless executable is `nrengine_headless.exe`.

An SDL2 development SDK can be passed to the same script. A custom SDK layout can be configured directly:

```bat
cmake -S . -B build-sdl3 -A x64 -DNR_BUILD_SDL=ON -DNR_REQUIRE_SDL=ON -DNR_SDL_VERSION=3 -DSDL3_DIR=C:\SDL3\cmake
cmake --build build-sdl3 --config Release --parallel
ctest --test-dir build-sdl3 -C Release --output-on-failure
```

Point `SDL3_DIR` at the directory containing the actual SDL3 package configuration. A folder named SDL3 alone is not enough. For a headless-only build, use `-DNR_BUILD_SDL=OFF`.

## Validation

Validation ran on Linux with GCC, SDL3 3.2.28 and SDL2 2.32.10 built from official release sources. Both complete frontend builds succeeded without compiler warning or error diagnostics in their build logs. Both variants passed all seven CTest groups: SDL adapter, atomic I/O, mod security, render security, configuration security, font security and unit tests.

The new SDL adapter test checks software rendering and pixel readback, keyboard event field mappings, queued audio and clearing, virtual joystick enumeration/opening/button input and legacy controller binding names. The launcher also rendered a screenshot successfully with the dummy video/audio drivers under each SDL version.

Configuration checks confirmed that explicit SDL3 selection fails when SDL3 discovery is disabled, automatic selection chooses an available SDL3 package, and the legacy `NR_BUILD_SDL2=OFF` flag disables graphical targets in a fresh build directory. Logs are included under `docs/validation/sdl-update/`.

Native Windows execution, MSVC compilation, physical gamepads, real audio hardware and accelerated graphics backends were not exercised here. The reported C4244 expressions were corrected in source, but a native MSVC rebuild is still needed to confirm its complete diagnostic output. Game-disc-dependent integration behavior was not validated. This archive contains source and icon assets, not newly built Windows executables or SDL development SDKs.
