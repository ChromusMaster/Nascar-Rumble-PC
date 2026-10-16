cmake_minimum_required(VERSION 3.13)
project(nrengine VERSION 0.3.2 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(NR_BUILD_SDL2 "Build the SDL2 frontend (window, audio, input)" ON)
option(NR_BUILD_TESTS "Build unit tests" ON)

if(MSVC)
  add_compile_options(/W3 /D_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -Wno-format-truncation -Wno-unknown-warning-option)
endif()

set(NR_CORE_SOURCES
  src/common/common.c src/common/image.c src/common/config.c
  src/disc/disc.c src/disc/gameid.c src/disc/manifest_slus01068.c
  src/psx/psx.c src/psx/cpu.c src/psx/bus.c src/psx/gte.c src/psx/dma.c src/psx/timers.c
  src/psx/gpu.c src/psx/cdrom.c src/psx/spu.c src/psx/mdec.c src/psx/sio.c
  src/psx/hle_bios.c src/psx/overrides.c
  src/formats/ctrl.c src/formats/bs.c src/formats/lsc.c src/formats/swvr.c src/formats/adpcm.c
  src/app/app.c src/app/audio_sync.c src/native/native.c src/native/pace.c
  src/render/rlist.c src/render/gfx.c src/render/swr.c src/render/postfx.c src/render/gpu_replay.c
  src/disc/discsel.c src/common/thread.c src/mods/mods.c
  src/common/paths.c src/common/proc.c src/input/hidparse.c)

add_library(nrcore STATIC ${NR_CORE_SOURCES})
target_include_directories(nrcore PUBLIC src)
target_compile_definitions(nrcore PUBLIC NR_VERSION_STRING="${PROJECT_VERSION}")
if(WIN32)
  target_link_libraries(nrcore PUBLIC shell32 bcrypt)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(nrcore PUBLIC Threads::Threads m)
endif()

add_executable(nrengine_headless src/app/main_headless.c)
target_link_libraries(nrengine_headless nrcore)

add_executable(nrtool src/tools/nrtool.c)
target_link_libraries(nrtool nrcore)

set(NR_UI_SOURCES src/ui/font8x8.c src/ui/canvas.c src/ui/ui.c src/platform/video.c)

if(NR_BUILD_SDL2)
  find_package(SDL2 CONFIG QUIET)
  if(NOT SDL2_FOUND)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
    endif()
  endif()
  set(NR_INPUT_SOURCES src/input/input.c src/input/hidraw.c)
  set(NR_FRONTEND_SOURCES src/platform/sdl2/main_sdl.c src/platform/sdl2/vb_software.c src/platform/sdl2/vb_opengl.c ${NR_UI_SOURCES} ${NR_INPUT_SOURCES})
  if(WIN32)
    list(APPEND NR_FRONTEND_SOURCES src/platform/win32/vb_d3d9.c
         src/platform/win32/vb_d3d10.c src/platform/win32/vb_d3d11.c src/platform/win32/vb_d3d12.c
         src/platform/win32/d3d_common.c)
  endif()
  if(TARGET SDL2::SDL2 OR TARGET PkgConfig::SDL2)
    if(TARGET SDL2::SDL2)
      set(NR_SDL_LIBS SDL2::SDL2)
      if(TARGET SDL2::SDL2main)
        list(PREPEND NR_SDL_LIBS SDL2::SDL2main)
      endif()
    else()
      set(NR_SDL_LIBS PkgConfig::SDL2)
    endif()
    # engine window (started by the launcher)
    add_executable(nrengine WIN32 ${NR_FRONTEND_SOURCES})
    target_link_libraries(nrengine nrcore ${NR_SDL_LIBS})
    # launcher
    add_executable(nrlauncher WIN32 src/launcher/launcher.c src/launcher/lui.c src/launcher/ltext.c src/launcher/l10n.c
                   src/ui/font8x8.c ${NR_INPUT_SOURCES})
    target_link_libraries(nrlauncher nrcore ${NR_SDL_LIBS})
    if(WIN32)
      # Direct3D 9/10/11/12, DXGI and d3dcompiler_47 are loaded at run time (LoadLibrary)
      target_link_libraries(nrengine comdlg32 dxguid uuid hid setupapi)
      target_sources(nrlauncher PRIVATE src/platform/win32/winutil.c)
      target_link_libraries(nrlauncher comdlg32 hid setupapi)
      if(TARGET SDL2::SDL2)
        add_custom_command(TARGET nrengine POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:nrengine>)
      endif()
    else()
      target_link_libraries(nrlauncher ${CMAKE_DL_LIBS})
    endif()
  else()
    message(WARNING "SDL2 not found: only nrengine_headless, nrtool and nrtests are built. "
                    "Install SDL2 (libsdl2-dev) or set SDL2_DIR to build the engine window and the launcher.")
  endif()
endif()

if(NR_BUILD_TESTS)
  enable_testing()
  add_executable(nrtests tests/test_main.c)
  target_link_libraries(nrtests nrcore)
  target_compile_definitions(nrtests PRIVATE NR_GOLDEN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden_slus01068.txt")
  add_test(NAME unit COMMAND nrtests)
  if(DEFINED ENV{NR_DISC})
    set_tests_properties(unit PROPERTIES ENVIRONMENT "NR_DISC=$ENV{NR_DISC}" TIMEOUT 900)
  endif()
endif()

# ---------------------------------------------------------------------------
# Installation (Linux/Unix) and Debian package
#
#   cmake --install build --prefix /usr        (or /usr/local)
#   cpack --config build/CPackConfig.cmake -G DEB
#
# Layout: executables in <prefix>/games, example mods and documentation in
# <prefix>/share/nrengine and <prefix>/share/doc/nrengine, desktop entry and
# icon in the XDG locations, udev rule for direct USB controller access.
# No game file is ever installed or packaged: the user supplies the disc.
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
  include(GNUInstallDirs)
  set(NR_INSTALL_TARGETS nrengine_headless nrtool)
  if(TARGET nrengine)
    list(APPEND NR_INSTALL_TARGETS nrengine nrlauncher)
  endif()
  install(TARGETS ${NR_INSTALL_TARGETS} RUNTIME DESTINATION games)
  install(DIRECTORY mods/ DESTINATION ${CMAKE_INSTALL_DATADIR}/nrengine/mods)
  install(FILES nrengine.ini.example DESTINATION ${CMAKE_INSTALL_DATADIR}/nrengine)
  install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
  install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME copyright)
  install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR})
  install(FILES packaging/linux/nrengine.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES packaging/linux/nrengine.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
  set(NR_UDEV_DIR "lib/udev/rules.d" CACHE STRING "Directory (relative to the prefix) for the udev rule")
  install(FILES packaging/linux/70-nrengine-gamepads.rules DESTINATION ${NR_UDEV_DIR})

  set(CPACK_PACKAGE_NAME "nrengine")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_CONTACT "NR Engine maintainers <nrengine@localhost>")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Reimplementation engine for NASCAR Rumble (requires your own disc)")
  set(CPACK_PACKAGE_DESCRIPTION "NR Engine runs the PlayStation game NASCAR Rumble from the user's own disc
image, with enhanced rendering (OpenGL or software), a launcher and generic USB
controller support. No game data is included.")
  set(CPACK_STRIP_FILES ON)
  set(CPACK_DEBIAN_PACKAGE_SECTION "games")
  set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  # dpkg-shlibdeps computes Depends from the binaries when SDL2 comes from the
  # distribution's libsdl2-dev. With a self-built SDL2 (no package to query)
  # set -DNR_DEB_SHLIBDEPS=OFF and the fixed list below is used instead
  # (glibc 2.34 = Debian 12 / Ubuntu 22.04; SDL 2.0.18 is the oldest supported).
  option(NR_DEB_SHLIBDEPS "Compute .deb dependencies with dpkg-shlibdeps" ON)
  if(NR_DEB_SHLIBDEPS)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  else()
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.34), libsdl2-2.0-0 (>= 2.0.18)")
  endif()
  set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "fontconfig, fonts-noto-cjk")
  set(CPACK_DEBIAN_PACKAGE_SUGGESTS "libgl1")
  set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
  set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
  set(CPACK_GENERATOR "DEB;TGZ")
  include(CPack)
endif()
