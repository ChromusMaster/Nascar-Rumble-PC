# NR Engine remediation report — 2026-10-21

## Delivery status

This is a source remediation package based on the supplied `Nascar-Rumble-PC(1).zip`, project version 0.3.2. The eight identified source locations have been patched. Their CodeQL closure is **IMPLEMENTED; CODEQL VERIFICATION PENDING**. No SARIF, original alert fingerprints, authenticated scanning results or CodeQL executable were available. This report does not claim “8/8 confirmed,” zero new High findings or completion of all phases of the requested roadmap.

The source archive already contained changes beyond its bundled Git index, including launcher font work. The uploaded file bytes, rather than that stale Git index, were used as the comparison baseline. No commit or pull request was created. Existing Markdown files are byte-identical to the uploaded versions. No new source comments were added. New documentation is written in English.

The delivered ZIP excludes the supplied `.git/` metadata and `build/` products. It contains source, tests, documentation and icon assets, not newly compiled Windows executables, game media, BIOS files or extracted game data.

## Implemented changes

### Configuration and atomic replacement

`src/common/config.c` now uses checked, bounded dynamic buffers rather than accumulating unchecked `snprintf` return values. Key/value storage is dynamic; replacement allocations are committed only after success. The existing void setter API is preserved and logs allocation/size failures without destroying the old value.

Limits are 16 MiB per configuration file, 4,096 bytes per full key and 1 MiB per value. BOM, untouched lines, CRLF/LF and a missing final newline on replaced lines are retained. Matching active or commented key lines are normalized to `key = value`, preserving their line ending. Repeated matching keys/sections are all updated to avoid a later duplicate overriding the saved choice. CR/LF injection, invalid names and embedded NUL files are rejected. Integer lookup validates range and conversion errors.

`nr_write_file_atomic()` writes a unique same-directory temporary file and checks write, flush, close and replacement results. POSIX uses `mkstemp`, `fsync` and `rename`, preserving existing permission bits; Windows uses exclusive `CreateFileW`, `FlushFileBuffers` and `MoveFileExW`. Pre-replacement failures leave the existing bytes intact. This is file replacement atomicity, not a claim of full power-loss durability: the parent directory is not fsynced, and Windows ACL inheritance/preservation needs native verification. Concurrent configuration writers are not serialized. Other persistence systems, including memory cards, were not converted.

### Software rendering and command storage

The three flagged VRAM offsets now widen operands before multiplication. Renderer initialization checks allocation size, stores allocated element count and validates replay invariants. Copy rows use a renderer-owned 64 KiB scratch array, eliminating per-copy heap allocation and its silent failure. Native dimensions, upload spans and exposed pixel coordinates are checked. Existing row-by-row copy order and mask handling are retained.

`nr_rlist_alloc_data()` rejects counts exceeding the hard budget before adding them, preventing wraparound and unbounded capacity growth. Invalid replay commands produce explicit errors. This is not a complete invalid-command fallback mechanism; rejected replay can produce an incomplete presented frame. Maximum software VRAM remains 512 MiB at 16-by-16 sample expansion. An adaptive memory budget is not implemented.

### Vendored font patch

The upstream header reviewed on 2026-10-21 still identifies itself as stb_truetype 1.26. A minimal local patch was chosen instead of an unverified vendor replacement. License text and existing third-party notices are unchanged.

A shared helper validates positive bitmap dimensions, a 16,384-pixel axis limit, `size_t` multiplication and a 64 MiB bitmap budget. The four reported allocation/clear paths use checked sizes. Packing validates padding, stride and node count, and clears rows according to stride without overwriting padding bytes. SDF rejects invalid scale/padding/distances and releases partial allocations. Glyph bitmap scale and shifts are bounded before integer box calculations.

The launcher limits system-font reads to 64 MiB and validates TTC/SFNT table-directory ranges before initialization. These checks do **not** make stb a secure parser for hostile fonts. Fontconfig-selected/system fonts remain trusted input. Language packs cannot provide font paths. Deep malformed-font fuzzing, all parser internals and real CJK rendering are not certified by this patch.

Upstream and rule references:

- https://github.com/nothings/stb/blob/master/stb_truetype.h
- https://codeql.github.com/codeql-query-help/cpp/cpp-overflowing-snprintf/
- https://codeql.github.com/codeql-query-help/cpp/cpp-integer-multiplication-cast-to-long/

### Audio defect found with ASan

The existing interpolation expression rounded the exact position `-1.0` down to `-2`, then read samples before the input buffer. ASan reproduced the out-of-bounds read in the existing repeated-block audio test. Using `floor()` fixes this case. Undersized output capacity now drops the unrepresented input portion and clamps the carried position; invalid state and queue-threshold arithmetic are checked. Regression assertions cover short and zero-capacity output.

The test suite's MIPS encoder macros also shifted signed opcode constants beyond `INT_MAX`. Operands are now unsigned before shifting, allowing UBSan to exercise the runtime instead of stopping in the test harness.

### User language packs

Game text parsing now validates UTF-8 and escapes, rejects NUL injection and unsupported code points, validates IDs, caps entries, handles partial allocation failure and applies deterministic duplicate handling. Executable replacement no longer overwrites adjacent zero storage or cuts oversized translations. Overlay lookup rejects unsafe relative paths, links/reparse points, special files and reserved Windows device names, with bounded reads.

The launcher accepts optional `mods/Language/<existing-locale>/launcher.ini` catalogs, validates IDs/UTF-8/format arguments and falls back per key. Existing built-in locale identities remain the selectable set. Catalogs are loaded once with stable string lifetime. There is no JSON manifest support, arbitrary new locale discovery, hot reload or external pack font loading. See [TRANSLATIONS.md](TRANSLATIONS.md) for the actual supported contract and paths.

### Executable icons

`packaging/icons/nrlauncher.*` and `nrengine_headless.*` extend the project's existing dark/checkered vector identity: a gold launch symbol and a cyan terminal symbol. Assets are original project-native SVG artwork, exported to 256-pixel transparent PNG and ICO containing 16, 24, 32, 48, 64, 128 and 256 pixel images. No generated raster model or online logo asset was used.

CMake enables RC on Windows and attaches a separate icon resource to each executable. The headless target remains a console executable. Linux installs the additional SVG assets; the existing desktop entry/icon is retained. PE resource compilation, Explorer display and the launcher taskbar/window icon require Windows validation; no executable-window icon change is claimed.

## Eight-alert evidence matrix

All rows refer to locations in the supplied baseline, not current line numbers. Original IDs/fingerprints and final SARIF remain unavailable.

| Image alert | Baseline location | Implemented correction | Reproducible test | CodeQL status |
|---|---|---|---|---|
| #1 | stb_truetype.h:4613 | Checked SDF bitmap size, parameter bounds and partial-allocation cleanup | `font_security` | Pending; fingerprint unavailable |
| #2 | stb_truetype.h:3984 | Validated pack dimensions, stride/padding/node count; row clear | `font_security` padded-stride and allocation tests | Pending; fingerprint unavailable |
| #3 | stb_truetype.h:3825 | Checked bake bitmap size and input parameters | `font_security` invalid bake dimensions | Pending; fingerprint unavailable |
| #4 | stb_truetype.h:3742 | Checked glyph bitmap allocation; scale/shift bounds | `font_security` valid/invalid bitmap cases | Pending; fingerprint unavailable |
| #5 | swr.c:463 | Widened source offset before multiplication | `render_security`, existing VRAM comparison | Pending; fingerprint unavailable |
| #6 | swr.c:467 | Widened destination offset before multiplication | `render_security`, existing VRAM comparison | Pending; fingerprint unavailable |
| #7 | swr.c:116 | Widened native row/column operands before multiplication | `unit`, native texture/render path | Pending; fingerprint unavailable |
| #8 | config.c:154 | Replaced formatted length accumulation with checked byte appends and atomic write | `config_security`, `atomic_io` | Pending; fingerprint unavailable |

## Validation and limitations

Environment: Linux x86_64, GCC 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04). Local builds compile the core, headless executable, nrtool, launcher text/localization code and tests directly with GCC using the source list in CMake. CMake, SDL2 development packages, CodeQL and a Windows cross-compiler were unavailable. An attempted package installation failed because the environment could not perform required privilege changes. No approval workaround was attempted.

Commands executed from the task workspace:

```sh
ASAN_OPTIONS=detect_leaks=0 python3 work/Nascar-Rumble-PC/scripts/validate_gcc.py --sanitize --output validation-sanitized-final
python3 work/Nascar-Rumble-PC/scripts/validate_gcc.py --output validation-release
```

For reproduction from the delivered project root:

```sh
python3 scripts/validate_gcc.py --output /tmp/nr-validation
python3 scripts/validate_gcc.py --sanitize --output /tmp/nr-validation-san
```

The local sanitizer run disabled LeakSanitizer only because this runtime prevents its `/proc`/thread inspection and it terminated with a ptrace-related diagnostic. AddressSanitizer and UndefinedBehaviorSanitizer remained enabled, with recovery disabled. Leak checking is therefore **not validated locally**. The CI sanitizer job does not disable leak detection.

Results and build commands are retained under `docs/validation/`. The suite reports 168 successful checks, zero failures and one skipped game-data group. Additional dedicated groups cover configuration/fault injection, font dimensions/allocation failure, 40 renderer configurations, mod parsing/path rejection and five atomic IO failure stages. No goldens were changed. Compiler warnings remain in pre-existing disc-path and shared-data-path formatting; they are recorded in the logs and were not suppressed by a new compiler flag.

The font tests use the existing redistributable built-in font, not proprietary game resources. Renderer tests are synthetic and check mask, edge wrap and overlapping copy cases; they do not prove every PS1 hardware overlap quirk. Game media was absent, so race, physics, native-hook, game glyph, snapshot, two-player and image-reference tests were skipped. SDL window rendering, OpenGL and all Direct3D backends were not run. No full-race or long-duration performance claim is made.

## CI and completion gates

`.github/workflows/security.yml` adds Linux/Windows core tests, Linux ASan/UBSan and an SDL-enabled CodeQL build. `scripts/check_security_sarif.py` fails on either targeted query or any finding with security severity at least 7. This intentionally blocks all High results, including pre-existing ones, until reviewed; it does not silently baseline or suppress them. SARIF is retained as a workflow artifact even after gate failure. The workflow was authored but not executed in this environment.

Required external completion steps:

1. Run CMake/CTest and the resource compiler on native Windows. Build the SDL launcher and verify both executable icons.
2. Run CodeQL on the corrected revision, retain SARIF, recover the eight original fingerprints and compare. Review any other High findings; do not close the eight alerts solely from source inspection.
3. Run the existing `NR_DISC` tests locally with the owner's authorized reference image. Do not upload media or game-memory snapshots to public CI.
4. Validate renderer parity and device-loss/resize behavior on actual OpenGL and D3D9/10/11/12 hardware.

## Roadmap phase accounting

| Phase | State in this delivery |
|---|---|
| 0, source inspection | Completed for the requested source paths; no CodeQL baseline artifacts |
| P0, eight source patches | Implemented and covered by local synthetic tests; CodeQL closure pending |
| P1, stability | Targeted config, audio, allocation, path and command-list fixes; not a whole-emulator certification |
| P2, visual fidelity/features | Software safety checks only; GPU mask parity, advanced visuals and hardware comparisons not implemented |
| P3, physics | Unchanged; original MIPS routines and existing timing options retained; no reference disc to discover/validate new hooks |
| P4, opponents | Unchanged; routines/structures not mapped or measured |
| P5, powers/cooldowns | Not implemented; actual activation semantics and authoritative per-runner state unavailable |
| P6, translations | Legacy parser hardening and external catalogs for existing launcher locales; broader manifest/new-locale design deferred |
| P7, consolidation | Local regression and sanitizer evidence, source package, authored CI; remote CI/hardware/media gates pending |

No new gameplay profiles or disconnected cooldown state machines were added. The existing `engine.physics_step = original|auto|30|60` behavior remains the existing contract. Shipping an unconnected timer or guessing a game memory address would not implement the requested gameplay feature safely.
