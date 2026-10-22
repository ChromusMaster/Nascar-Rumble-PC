# User-supplied translations

This supplement describes the formats implemented by the remediation patch. No online service is needed. The engine does not generate translations or distribute extracted game text. Existing Markdown files retain their original content and language; where older documentation describes cutting executable messages, the rejection policy below supersedes it.

## Locations and precedence

The first matching location wins:

1. `mods/` in the current working directory.
2. `mods/` under `nr_data_dir()`.
3. `mods/` beside the executables.
4. `mods/` under `nr_share_dir()`.

On Windows, a writable executable folder is portable storage. Otherwise data goes to `%APPDATA%/NREngine`. On Linux, a writable executable folder outside `/usr/` and `/opt/` is portable storage. Installed Linux data goes to `$XDG_DATA_HOME/nrengine`, or `$HOME/.local/share/nrengine` when that variable is unset. Settings use `$XDG_CONFIG_HOME/nrengine` or `$HOME/.config/nrengine`; these are not necessarily the data directory. Shared installations use `/usr/share/nrengine`, `/usr/local/share/nrengine`, or the prefix-derived shared folder documented by `src/common/paths.c`.

## Launcher catalog

For a language already present in the Launcher language selector, create:

```text
mods/Language/en-US/launcher.ini
```

Select the corresponding locale in **General > Launcher language**. The catalog is read once per language, on first selection; restart the launcher after editing it. The folder name must match an existing locale code, for example `pt-BR`, `en-GB`, `ja-JP` or `zh-Hant-TW`. This version does not discover additional locale identities.

Minimal original example:

```ini
[strings]
S_BTN_PLAY = Start session
S_BTN_SAVE = Store settings
S_BTN_QUIT = Exit launcher
```

Grammar: UTF-8, optional BOM, LF or CRLF, one `S_ID = value` per line. Leading and trailing whitespace is removed. `[strings]` is optional. Blank lines and lines starting with `#` or `;` are ignored. Values are literal: do not quote them, and do not use JSON or backslash escape sequences. The first `=` separates key and value. IDs are the `S_*` enum names in `src/launcher/l10n.h`, excluding `S_COUNT`.

The file limit is 256 KiB; a value may contain at most 4,096 bytes. Unknown IDs, duplicate valid IDs, malformed UTF-8, disallowed controls and incompatible format arguments are rejected per entry. The first valid value for an ID wins. Runtime allocation failures are logged; missing entries continue using internal strings. Format arguments must preserve the exact sequence of `%s`, `%d` and `%%` in the English source. Positional arguments, additional conversions, width modifiers and `%n` are not accepted.

Fallback is external text for the selected locale, then its built-in text, then the corresponding external/built-in parent locale, then English. Catalog strings have process lifetime, so returning to menus does not invalidate pointers. A catalog cannot load a font or execute scripts. Glyph coverage still depends on the built-in/system font chain; UTF-8 validity does not guarantee glyph availability.

`manifest.json` and `launcher.json` are not supported. The deliberately small INI grammar avoids adding a JSON dependency. A separate CLI catalog validator and arbitrary new locale discovery remain unimplemented.

## Game catalog

The legacy layout remains supported:

```text
mods/Language/my-language/text.txt
mods/Language/my-language/files/
```

Select **General > Game language**, set `game.language = my-language`, or use `nrengine_headless --language my-language` with your own disc. Choosing Original, or clearing `game.language`, disables game translations. Launcher and game languages remain separate settings.

Create a local template using your own disc:

```sh
nrtool export-text "/path/to/your-disc.cue" text.txt
```

Do not redistribute that extracted template. Fill only the entries you intend to replace. `text.txt` supports `[options]`, `[fe]` and `[exe]`, as documented in `MODS.md`. `accents = ascii` transliterates supported Latin-1 accents; `accents = keep` emits Latin-1 bytes and requires compatible game glyphs. The original game does not automatically acquire Unicode font support.

Accepted escapes are `\n`, `\\` and `\xNN` with two hexadecimal digits. `\x00`, malformed escapes, invalid UTF-8, surrogates and unsupported characters beyond Latin-1 reject the entry rather than truncating it. Preserve game control sequences when translating. The file limit is 1 MiB, with up to 4,096 front-end entries and 4,096 executable entries. Front-end IDs must be decimal integers from 0 through `INT_MAX`. Duplicate front-end IDs keep the first valid definition.

Front-end strings share the existing 24 KiB reserved RAM area. Entries beyond that space retain the original fallback and generate a warning. The existing GetText hook still requires its instruction signature.

Executable translations must fit within the original string's own byte length. Longer translations are skipped with a warning. Adjacent zero bytes are no longer treated as spare string storage. These substitutions still operate on the in-memory executable; safe semantic identification of every string occurrence requires validation with the reference disc.

## Overlay files and diagnostics

Images containing text need compatible user-supplied replacements, such as `files/CW/HELPSCRN/HELP1.LSC`. The engine does not translate pixels. Overlays are limited to 64 MiB per file; paths with traversal, absolute components, Windows device names, symlinks/reparse points or special files are rejected by the path checks. Overlong paths are rejected. The executable and `SYSTEM.CNF` remain protected.

Use the engine log to distinguish malformed text, duplicate IDs, unsupported characters, excessive message length and missing overlays. If a pack fails, remove the offending entry or switch back to Original. Pack contents must remain unchanged while being loaded: path validation is not a sandbox against a separate local process concurrently replacing filesystem entries. Aggregate overlay memory budgeting and a race-resistant handle-based loader remain follow-up work.
