# Progressive native reimplementation

## Principle

The game first runs with its own code (read from the user's image). Each
original function is then rewritten in C and **replaces** the original only
after it has been verified against it. The reimplementation therefore never
goes through a phase where "almost everything works".

## Mechanism (`src/psx/overrides.c`)

- `psx_override_add(p, address, function, name, observe_only)` registers a C
  function for an address of the executable.
- When the CPU reaches the address (outside a delay slot), the C function gets
  the whole machine, reads arguments from `a0..a3`/stack, writes results to
  RAM and `v0`, reports its modelled execution time and returns like `jr ra`.
- The modelled time is not added in one block: it is **consumed as stall
  cycles** at the return address, and interrupts (CD sectors, timers, VBlank)
  keep being serviced during it, as if the original code were running. Before
  0.3.0 the time was added at once, which delayed CD interrupts by up to
  600 000 cycles and made the intro video play slowly.
- A native function may **decline** a call (return `false`); the original
  code then runs — used for cases not covered yet.
- **Observers** only look at or adjust state and let the original
  instruction execute (e.g. capturing the active VLC table, or the steady
  physics step).

## Safety against different executables

`src/native/native.c` (and `src/native/pace.c`) keep, for every routine, the
instruction words expected at the address. If they do not match (another
executable, another region, a modified version) the routine is **not
enabled** and the original runs. The disc identification also requires the
reference SHA-1 of the executable unless the user passes `--allow-modified`.

## Verification (`--verify-natives`)

On every call:

1. save the CPU state;
2. run the **original routine** nested until it returns, measuring cycles and
   recording RAM/scratchpad;
3. restore the state and run the **native version**;
4. compare all of RAM (2 MB), scratchpad and result registers;
5. keep the **original** result (a defect in the native version never changes
   the game in this mode) and log any mismatch with address and size.

On exit it prints calls, declines, checks and the ratio between the modelled
time and the measured time of the original.

## Replaced routines

| Address | Original routine | Status |
|---|---|---|
| `80098F28` | VLC table initialisation (builds lookup tables from 0xE0 MDEC values; `0` selects the standard table at `800AC290`) | **observer**: captures the active table; the original keeps running |
| `8009905C` | EA's BS decoder (bitstream → MDEC RLE), used by the `.WVE` videos, `.LSC` screens and other MDEC pictures | **replaced** by `nr_bs_decode_game` (`src/formats/bs.c`) |
| `800269E0`, `80026CF8` | game frame timing (wait for the next tick, final averaged step) | **observers** of the steady physics step, see [CONFIG.md](CONFIG.md) |

Original behaviour reproduced by the native version (including quirks): it
writes the two header half-words first; DC is `(qscale << 10) | dc` without a
mask; the escape code `0x1FF` ends the frame in any block; it finishes with
64 × `FE00`; it returns `v0 = 0`.

### Verification result (0.3.0)

Run with the reference image from boot through the logos and the whole intro
video (3 400 frames), `--verify-natives`:

```
DecodeBS (MDEC decoder) @ 8009905C calls=498 declined=0 verified_ok=498 mismatched=0
    measured cycles: original=299415239 native model=282190579 (0.942)
```

- **498 of 498 calls identical** byte for byte in RAM and registers.
- With the 0.3.0 CPU timing (memory-access costs and instruction cache) the
  original routine became about 6 % slower than before, so the time model was
  recalibrated from `200 + 31 ×` to `200 + 33 × half-words produced` cycles
  (aggregate error +0.3 % on the run above). Individual calls vary (the real
  cost depends on the bitstream content), so the decoding pace is not
  cycle-identical — a documented difference in [STATUS.md](STATUS.md).
- With the native routine the intro video plays at 14.8 decoded frames per
  second (median 4.0 fields between frames; the stream is 15 fps).

The data test (`nrtests` with `NR_DISC`) repeats the verification over the
first 1 500 frames and fails on any mismatch.

## Adding a routine

1. Locate and understand the function (`nrtool disasm`, `--watch-pc`,
   `--trace`, `--profile-pc`).
2. Implement it in C in `src/native/` (or as a decoder in `src/formats/`).
3. Add the entry to `k_natives` with its signature words.
4. Run `--verify-natives` through every part of the game that calls the
   routine; require zero mismatches.
5. Calibrate the cycle cost with the printed ratio.
6. Update this table and `docs/STATUS.md`.
