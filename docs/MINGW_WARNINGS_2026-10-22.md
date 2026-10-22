# MinGW warning corrections

The supplied Windows build screenshot showed warnings while compilation was still progressing. It did not establish whether the complete build ultimately succeeded.

## Corrections

- `src/render/swr.c`: replaced the `%zu` memory-size diagnostic with `%lu` and an explicit unsigned-long conversion. The renderer limits both internal scale factors to 16, so the reported allocation is at most 512 MiB and fits unsigned long on Windows. This avoids the size_t format rejected by the MinGW printf checker without suppressing format validation.
- `src/common/common.c`: retained negative-size, caller-limit and file-position checks. The additional SIZE_MAX allocation-overflow check is compiled only when LONG_MAX can reach SIZE_MAX. On Windows x64, long is narrower than size_t, so this comparison was always false. On architectures where it is necessary, the check remains before allocation of the terminating byte.
- `CMakeLists.txt`: removed `-Wno-unknown-warning-option`. Existing misleading-indentation and format-truncation options now depend on successful compiler feature checks using their positive warning forms. No additional warning suppression was introduced.

## Validation and use

Rebuilt the SDL3 configuration on Linux with GCC and ran all seven existing CTest groups. The build and test logs accompany this update under `docs/validation/mingw-warnings/`. Native MinGW/MSVC compilation remains unverified in this environment; the supplied Windows diagnostics were addressed in source, not reproduced here.

Replace the project files with this archive, retaining your own game data and settings, and rerun `scripts\build_windows.bat C:\SDL3` from the same PowerShell session where the x86_64 toolchain is selected. CMake automatically reconfigures the existing build when CMakeLists.txt changes. If the compiler path itself changes, use a fresh build directory.

All earlier SDL3 support and executable icon assets are retained. No source comments were added. Previously existing Markdown files were preserved; this English document is new.
