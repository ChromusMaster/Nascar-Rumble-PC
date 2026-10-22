@echo off
setlocal
for %%I in ("%~dp0..") do set "NR_ROOT=%%~fI"
set "NR_IMAGE=%~1"
if "%NR_IMAGE%"=="" set "NR_IMAGE=debian:13"
where docker >nul 2>nul
if errorlevel 1 (
  echo error: Docker Desktop must be installed and running to build the .deb package on Windows.
  exit /b 1
)
docker info >nul 2>nul
if errorlevel 1 (
  echo error: Docker is installed but not running. Start Docker Desktop and run this script again.
  exit /b 1
)
if not exist "%NR_ROOT%\dist" mkdir "%NR_ROOT%\dist"
docker run --rm -v "%NR_ROOT%:/src:ro" -v "%NR_ROOT%\dist:/dist" %NR_IMAGE% sh -c "set -e; export DEBIAN_FRONTEND=noninteractive; apt-get update -qq; apt-get install -y -qq --no-install-recommends build-essential cmake libsdl3-dev dpkg-dev file ca-certificates >/dev/null; cp -r /src /tmp/nrengine; rm -rf /tmp/nrengine/build-* /tmp/nrengine/dist; find /tmp/nrengine/scripts /tmp/nrengine/packaging -type f \( -name '*.sh' -o -name postinst -o -name '*.rules' -o -name '*.desktop' \) -exec sed -i 's/\r$//' {} +; BUILD_DIR=/tmp/build-deb DIST_DIR=/dist sh /tmp/nrengine/scripts/build_deb.sh"
if errorlevel 1 exit /b 1
echo The package is in "%NR_ROOT%\dist".
exit /b 0
