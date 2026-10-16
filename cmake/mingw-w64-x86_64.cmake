# Toolchain para compilacao cruzada Linux -> Windows x86_64 (MinGW-w64 ou llvm-mingw).
# Uso:
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
#         -DMINGW_ROOT=/caminho/llvm-mingw -DSDL2_ROOT=/caminho/SDL2-2.x.y/x86_64-w64-mingw32
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(MINGW_ROOT "" CACHE PATH "Raiz do toolchain MinGW (contendo bin/x86_64-w64-mingw32-gcc)")
set(SDL2_ROOT "" CACHE PATH "Pacote SDL2 de desenvolvimento MinGW (pasta x86_64-w64-mingw32)")
if(MINGW_ROOT)
  set(_p "${MINGW_ROOT}/bin/")
else()
  set(_p "")
endif()
set(CMAKE_C_COMPILER "${_p}x86_64-w64-mingw32-gcc")
set(CMAKE_RC_COMPILER "${_p}x86_64-w64-mingw32-windres")
set(CMAKE_FIND_ROOT_PATH "${MINGW_ROOT}/x86_64-w64-mingw32" "${SDL2_ROOT}")
if(SDL2_ROOT)
  set(SDL2_DIR "${SDL2_ROOT}/lib/cmake/SDL2" CACHE PATH "")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
