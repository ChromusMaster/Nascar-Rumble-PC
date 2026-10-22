@echo off
rem Build NR Engine on Windows with CMake.
setlocal
for %%I in ("%~dp0..") do set "NR_ROOT=%%~fI"
set "NR_SDK=%~1"
set "NR_TYPE=%~2"
if "%NR_TYPE%"=="" set "NR_TYPE=Release"
if /i not "%NR_TYPE%"=="Release" if /i not "%NR_TYPE%"=="Debug" (
  echo error: configuration must be Release or Debug.
  exit /b 1
)
if "%NR_SDK%"=="" (
  echo usage: scripts\build_windows.bat C:\path\to\SDL3 [Release^|Debug]
  echo An SDL2 development package is also supported.
  exit /b 1
)
where cmake >nul 2>nul
if errorlevel 1 (
  echo error: CMake 3.16 or newer must be in PATH.
  exit /b 1
)
set "NR_SDL_DIR="
call :candidate "%NR_SDK%\x86_64-w64-mingw32\lib\cmake\SDL3" 3 mingw
call :candidate "%NR_SDK%\lib\cmake\SDL3" 3 msvc
call :candidate "%NR_SDK%\cmake\SDL3" 3 msvc
call :candidate "%NR_SDK%\cmake" 3 msvc
call :candidate "%NR_SDK%" 3 msvc
call :candidate "%NR_SDK%\x86_64-w64-mingw32\lib\cmake\SDL2" 2 mingw
call :candidate "%NR_SDK%\lib\cmake\SDL2" 2 msvc
call :candidate "%NR_SDK%\cmake\SDL2" 2 msvc
call :candidate "%NR_SDK%\cmake" 2 msvc
call :candidate "%NR_SDK%" 2 msvc
if not defined NR_SDL_DIR (
  echo error: no SDL3Config.cmake, sdl3-config.cmake, SDL2Config.cmake or sdl2-config.cmake found.
  echo Extract a complete SDL development package. A runtime DLL or a documentation folder is insufficient.
  exit /b 1
)
set "NR_BUILD=%NR_ROOT%\build-sdl%NR_SDL_MAJOR%-%NR_STYLE%"
echo == SDL%NR_SDL_MAJOR% package: %NR_SDL_DIR%
if "%NR_STYLE%"=="mingw" goto mingw
echo == configuring for Visual Studio x64
cmake -S "%NR_ROOT%" -B "%NR_BUILD%" -A x64 -DNR_BUILD_SDL=ON -DNR_REQUIRE_SDL=ON -DNR_SDL_VERSION=%NR_SDL_MAJOR% "-DSDL%NR_SDL_MAJOR%_DIR=%NR_SDL_DIR%"
goto build
:mingw
where gcc >nul 2>nul
if errorlevel 1 (
  echo error: this package requires an x86_64 MinGW GCC toolchain in PATH.
  exit /b 1
)
echo == configuring for MinGW x64
cmake -S "%NR_ROOT%" -B "%NR_BUILD%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=%NR_TYPE% -DNR_BUILD_SDL=ON -DNR_REQUIRE_SDL=ON -DNR_SDL_VERSION=%NR_SDL_MAJOR% "-DSDL%NR_SDL_MAJOR%_DIR=%NR_SDL_DIR%"
:build
if errorlevel 1 exit /b 1
cmake --build "%NR_BUILD%" --config %NR_TYPE% --parallel
if errorlevel 1 exit /b 1
set "NR_BIN=%NR_BUILD%"
if exist "%NR_BUILD%\%NR_TYPE%\nrlauncher.exe" set "NR_BIN=%NR_BUILD%\%NR_TYPE%"
if not exist "%NR_BIN%\nrlauncher.exe" (
  echo error: nrlauncher.exe was not built.
  exit /b 1
)
if not exist "%NR_BIN%\nrengine.exe" (
  echo error: nrengine.exe was not built.
  exit /b 1
)
echo Executables are in "%NR_BIN%".
echo Start nrlauncher.exe. A shared SDL%NR_SDL_MAJOR% runtime is copied automatically by CMake.
exit /b 0
:candidate
if defined NR_SDL_DIR exit /b 0
if exist "%~1\SDL%~2Config.cmake" goto found
if exist "%~1\sdl%~2-config.cmake" goto found
exit /b 0
:found
set "NR_SDL_DIR=%~1"
set "NR_SDL_MAJOR=%~2"
set "NR_STYLE=%~3"
exit /b 0
