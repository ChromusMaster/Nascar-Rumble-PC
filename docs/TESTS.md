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
