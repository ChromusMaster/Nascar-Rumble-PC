# Tests and diagnostics

## Running

```sh
scripts/run_tests.sh                                    # unit tests only
scripts/run_tests.sh "/path/NASCAR Rumble (USA).cue"    # + tests with your disc
```

or directly `build/nrtests` (with the `NR_DISC` variable for the data tests),
or `ctest --test-dir build` (passes `NR_DISC` if it was set when `cmake` ran).

Without `NR_DISC` the data group is reported as **skipped** — no game data is
needed for the unit tests. `scripts/build_deb.sh` runs the unit tests before
packaging.

## What is tested

### Unit tests (no game files)

| Group | Coverage |
|---|---|
| `cpu` | load delay slot, branch delay slot, unaligned LWL/LWR, DIV/DIVU edge cases, ADD overflow exception (EPC/CAUSE), disassembler |
| `gte` | RTPS (projection, SZ3, FLAG), division overflow, NCLIP, LZCS/LZCR, AVSZ3, IRGB/ORGB |
| `gpu` | quick fill, quad and triangle with the top-left fill rule, CPU→VRAM→CPU round trip, saturated semi-transparency |
| `dma` | OTC (ordering table) and completion after the modelled delay |
| `timers` | timer 2 with target, reset at target and IRQ |
| `bs` | BS v2 decoder (DC, AC, sign, end of block/frame) and a grey MDEC macroblock |
| `adpcm` | SPU-ADPCM blocks: shift, saturation, filter 1 |
| `disc` | synthetic 40-sector ISO: volume, file lookup, reads, synthesised sector header; identification without an executable; error messages (missing file, CUE pointing at a missing BIN, unsupported track) |
| `misc` | CRC32, SHA-1, INI, PNG writing |
| `hle` | HLE kernel call from MIPS code (`strlen` through A0h) |
| `audio_sync` | dynamic rate control: queue at target → ratio 1; full queue → up to 0.5 % faster; overfull → block dropped; interpolation keeps a ramp |
| `swr` | recording the command list does not change console VRAM; replay at 1×/15-bit identical to the console rasteriser (whole VRAM); 4× + AA 16x: scale 16, exact interior colours, partial sample coverage on edges |
| `discsel/overlay` | image detection in `gamedata/` by content, precedence of choices; replacing a file with a larger one (virtual sectors) without changing the image file; INI update keeping comments and section headers |
| `hid` | HID report-descriptor parser and report decoding: "PC Twin Shock"-style pad (5 axes, hat, 12 buttons, vendor padding) and a game pad with report IDs |
| `session` | launcher session token: 32 hex digits, unique, accepted only when the command line and the environment match |

### With the user's data (`NR_DISC`)

| Test | Criterion |
|---|---|
| identification | status `OK`, sectors read from the CD |
| LSC screens | all 70 `.LSC` files decode with the native decoder |
| determinism | two runs of 400 frames → same VRAM CRC32 |
| native routines | 1 500 frames with `--verify-natives`: zero mismatches and at least one check per replaced routine |
| regression (boot) | VRAM CRC32 at frame 400 = reference |
| regression (race) | scripted input through the menus into the race with the accelerator pressed; VRAM CRC32 at frame 5 600 = reference; more than 100 000 primitives drawn |
| memory card | Game Options > Load and Save > Save on a new card image: the image holds the `MC` header and the directory entry `BASLUS-01068` in use (0x51) |
| RAM reserved for mods | 0x6000–0xBFFF stays zero up to the race without mods (neither the game nor the HLE kernel uses it) |
| picture enhancements | the same race with 2× + AA 16x + 24-bit + bilinear + PGXP + CRT + ACES produces the same console VRAM |

The data tests run with `engine.physics_step = original`, so the reference
values describe the console behaviour. References are in
`tests/golden_slus01068.txt` (CRC32 values only, no game data). When an
intended, validated change alters the output, update them with
`build/nrtests --update-golden` (with `NR_DISC` set) and record the reason in
`docs/STATUS.md`.

Typical time: about 2 s without data; several minutes with data (two
5 600-frame races, one with enhanced rendering).

## Continuous integration (GitHub Actions)

`.github/workflows/ci.yml` runs on every push and pull request. It only
validates the code: no package or binary is produced or published, and no game
data is involved (the disc-based tests above are run locally).

| Job | What it checks |
|---|---|
| Linux gcc / clang | full build with warnings as errors (`-Werror`), unit tests |
| AddressSanitizer + UBSan | unit tests with memory-error, leak and undefined-behaviour detection; the launcher driven through every tab, control and language (`scripts/ci/launcher_ui_test.sh`, scripted clicks under Xvfb) |
| Windows MinGW-w64 | build with warnings as errors and unit tests (the toolchain of the released binaries) |
| Windows MSVC | build and unit tests with Visual Studio |
| cppcheck | static analysis (warnings, performance, portability; `src/third_party` excluded) |
| CodeQL | GitHub security analysis through the repository's CodeQL default setup (Settings > Code security; choose the "Extended" query suite for more checks); results appear in the Security tab |
| Hygiene | `scripts/check_package.py` (no disc images, game files or binaries committed) and ShellCheck on the scripts |

Compiler errors, sanitizer reports and failed test checks are shown as
annotations on the commit or pull request (problem matchers in
`.github/problem-matchers`).

Before a release, also run the sanitizer build against your own disc (a few
thousand frames with scripted input, see "Diagnostics").

## Hardware reference vectors (local validation)

The GTE was checked against the 1 150 vectors of the `gte/test-all` program of
[ps1-tests](https://github.com/JaCzekanski/ps1-tests) (MIT licence), whose
expected values were recorded on a real console: 0 differences. The CPU memory
access costs were calibrated from the `cpu/access-time` hardware log of the
same project. These files are not part of this repository; the comparison was
done locally.

## Diagnostics

| Need | Command |
|---|---|
| Check the image | `nrtool info IMG` / `nrtool verify IMG` |
| Captures along the game | `nrengine_headless IMG --frames 6000 --screenshot-every 300 --screenshot-dir out` |
| Look at VRAM | `--vram-at N` |
| Replay a session | `--input-script file.txt` (format in [CONFIG.md](CONFIG.md)) |
| Record the audio | `nrengine_headless IMG --frames 3000 --wav out.wav` |
| Game messages (TTY) | appear in the log as `tty` |
| Missing features | "Features NOT implemented" summary on exit |
| Investigate code | `nrtool disasm IMG 800A5440 40`, `--watch-pc`, `--watch-write`, `--trace`, `--dump-hle`, `--profile-pc A,B` |
| Validate a native routine | `--verify-natives` (report on exit) |
| Isolate the GPU model / natives | `--gpu-instant`, `--no-native` |
| Compare physics settings | `--save-state-at N:FILE` once, then several `--load-state FILE` runs with different `--overclock` / `--physics-step` and `--screenshot-at` (snapshots hold game memory: keep them local) |
| Reproduce a launcher UI problem | `NRLAUNCHER_TEST_CLICKS="x,y;x,y"` (window pixels, one click every 8 frames), `NRLAUNCHER_SCREENSHOT=out.png NRLAUNCHER_SCREENSHOT_FRAME=N`, `NRLAUNCHER_TAB`, `NRLAUNCHER_LANG`; build with `-DCMAKE_C_FLAGS=-fsanitize=address` to catch memory errors |
| Engine log of a launcher session | `nrengine.log` in the settings folder |
