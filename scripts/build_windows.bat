@echo off
rem Build NR Engine on Windows with CMake.
rem
rem Requirements:
rem   - CMake 3.13 or newer (https://cmake.org/download/), added to PATH
rem   - a C compiler: MinGW-w64 / llvm-mingw (recommended; gcc or clang in PATH)
rem     or Visual Studio 2019/2022 with "Desktop development with C++"
rem   - the SDL2 development package (https://github.com/libsdl-org/SDL/releases):
rem       MinGW:          SDL2-devel-2.x.y-mingw.zip
rem       Visual Studio:  SDL2-devel-2.x.y-VC.zip
rem
rem Usage:  scripts\build_windows.bat C:\path\to\SDL2-2.30.9 [Release|Debug]
rem
rem Result: build\nrlauncher.exe, build\nrengine.exe, build\nrengine_headless.exe,
rem         build\nrtool.exe, build\nrtests.exe and SDL2.dll
rem         (with Visual Studio the files are in build\Release or build\Debug).
setlocal
set ROOT=%~dp0..
set SDL=%~1
set TYPE=%~2
if "%TYPE%"=="" set TYPE=Release
set SDLDIR=
if "%SDL%"=="" (
  echo error: give the folder of the extracted SDL2 development package.
  echo usage: scripts\build_windows.bat C:\path\to\SDL2-2.30.9 [Release^|Debug]
  exit /b 1
)
if exist "%SDL%\x86_64-w64-mingw32\lib\cmake\SDL2" set SDLDIR=%SDL%\x86_64-w64-mingw32\lib\cmake\SDL2
if exist "%SDL%\cmake\sdl2-config.cmake" set SDLDIR=%SDL%\cmake
if "%SDLDIR%"=="" (
  echo error: "%SDL%" does not look like an SDL2 development package
  echo        ^(expected x86_64-w64-mingw32\lib\cmake\SDL2 or cmake\sdl2-config.cmake inside it^).
  exit /b 1
)
where cmake >nul 2>nul
if errorlevel 1 (
  echo error: cmake was not found in PATH.
  exit /b 1
)
where gcc >nul 2>nul
if %errorlevel%==0 goto mingw
where clang >nul 2>nul
if %errorlevel%==0 goto mingw
echo == configuring with Visual Studio
cmake -S "%ROOT%" -B "%ROOT%\build" -A x64 -DSDL2_DIR="%SDLDIR%"
goto build
:mingw
echo == configuring with MinGW (%TYPE%)
cmake -S "%ROOT%" -B "%ROOT%\build" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=%TYPE% -DSDL2_DIR="%SDLDIR%"
:build
if errorlevel 1 exit /b 1
echo == compiling
cmake --build "%ROOT%\build" --config %TYPE% --parallel
if errorlevel 1 exit /b 1
echo.
echo Executables are in %ROOT%\build (or %ROOT%\build\%TYPE% with Visual Studio).
echo Start the game with nrlauncher.exe. Keep SDL2.dll next to the executables.
endlocal
