# Mods (`mods/` folder)

The `mods/` folder is the engine's extension point for content supplied by the
user. **No file of your disc image is changed**: whatever a mod replaces is
handed to the game in memory while it runs.

The officially supported kind in this version is **language**
(`mods/Language`). The same structure (file replacement) is ready for future
track or car mods, but those formats are not documented or validated yet —
see "Other mods" at the end.

The launcher's own language (menu texts of the launcher) is a separate
setting and does not need a mod; see [CONFIG.md](CONFIG.md).

## Where it lives

`mods/` is searched in the current folder, in the data folder, next to the
executables and (installed versions) in the shared data folder, e.g.
`/usr/share/nrengine/mods`:

```
nrlauncher(.exe)
nrengine(.exe)
nrengine.ini
gamedata/                 (your disc image, optional - see CONFIG.md)
mods/
  Language/
    example-pt-BR/
      text.txt            translated texts
      files/              (optional) replacement files
```

## Turning a language on

Choose it in the launcher: **General → Game language** lists every folder in
`mods/Language`. "Original (English)" uses the game's own texts. The choice is
stored as:

```ini
[game]
language = example-pt-BR      ; folder name in mods/Language
```

## `text.txt`

UTF-8, comments with `#` or `;`. Three sections:

```ini
[options]
accents = ascii        ; default: "ção" becomes "cao" (the game font is ASCII only)
; accents = keep       ; keep Latin-1 bytes (only with a font that has them)

[fe]
; menu texts: number = text
100 = 1 Jogador
200 = Corrida Simples

[exe]
; messages built into the executable: "original" = "translation"
"<original text copied from the template>" = "<translation>"
```

- **`[fe]`** — the game's menus fetch each text by number (resource `TxtR`
  inside `CW/FEND/FE.TRK`). The engine replaces the game's lookup routine
  (`80090984`) with a native version: if the number exists in the
  translation, it returns the translated text; otherwise the original routine
  runs. There is no length limit beyond the reserved space (24 KB for all
  texts), but very long texts may not fit the menu box.
- **`[exe]`** — messages stored in the executable itself (pause, warnings).
  The executable's copy **in memory** is changed in place; the translation must
  fit the space of the original (the excess is cut and logged).
- Codes such as `[+11]`, `[-02]` and `\x13` control position/colour: keep them.
  Accepted escapes: `\xNN`, `\n`, `\\`.

### Creating the full template

```sh
nrtool export-text "NASCAR Rumble (USA).cue" text.txt
```

This writes, from **your** copy, every menu text and executable message, ready
to translate. That file contains original game texts: use it locally and
**distribute only the translated lines** (like
`mods/Language/example-pt-BR/text.txt`, which ships with the engine and holds
only translations written for the project).

## `files/` — file replacement

Any file in `<mod>/files/` replaces, for the game, the file with the same path
on the disc (case-insensitive), for example `files/CW/HELPSCRN/HELP1.LSC`. Use
it for screens with drawn text (help pictures) that you made yourself.

How it works: the engine's virtual CD layer serves the new content. If the new
file is larger than the original it gets virtual sectors after the end of the
disc and the directory record is corrected when the game reads it. The image
file is never written.

Restrictions: the executable (`SLUS_010.68`) and `SYSTEM.CNF` cannot be
replaced; files that do not exist on the disc are ignored with a warning.

## Other mods

`mods.load` in `nrengine.ini` (or `--mod` for `nrengine_headless`) loads other
folders of `mods/`, comma separated, with the same structure (`text.txt`
and/or `files/`):

```ini
[mods]
load = Cars/my-paint, Tracks/test
```

File replacement works for any file on the disc, but the car (textures/models)
and track formats are **not yet documented or validated** by this project; it
is prepared infrastructure, not a tested feature.

## What is never done

- no byte of your `.bin`/`.iso` image is modified;
- no original text or file ships with the engine;
- mods only change what the game reads **while it runs**.
