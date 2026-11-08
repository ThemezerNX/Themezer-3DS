# How Themezer-3DS is built

A guide for working on the code: where things live, the rules that keep the app responsive, and the conventions.

## Layers

```
source/
  core/      portable: formats and helpers, no 3DS headers (zip, LZ11, PNG, SMDH, BCSTM, UTF-8, paths, versions)
  domain/    portable: what the app does to the SD card and the HOME Menu's storage (themes, splashes, badges, the library)
  sys/       the 3DS: files, threads and jobs, network, audio, the system
  services/  built on sys: Themezer's and ThemePlaza's catalogues, updates, QR codes
  gfx/       drawing: the renderer, text, textures, icons
  ui/        input and the widgets screens are made of
  scenes/    the screens
  app/       the app itself: start-up, settings, strings, and the autopilot of developer builds
tests/       unit tests for core/ and domain/, built and run on a computer with `make test`
third_party/ code from elsewhere, with its license
```

A layer only uses the layers above it in this list. `core/` and `domain/` don't include `<3ds.h>`: they work on the
`FileSystem` interface (`core/io.hpp`), which `sys/fs.hpp` implements for the SD card and extdata, and `tests/memory_fs`
implements in memory. That is what makes the code that writes the HOME Menu's files testable.

## Threads

- **The main thread draws and handles input, nothing else.** It never reads the SD card, decodes an image or waits for
  the network. A frame takes a few milliseconds of it.
- Work runs on a **worker** (`sys/jobs.hpp`): `jobs::async(Lane::Io, work, done)` runs `work` on the SD card worker and
  `done` with its result on the main thread, during the next frame. Network work uses `Lane::Network`, so a slow server
  never delays the SD card, and a list's icons `Lane::Icons`, with a connection of their own, so a preview downloading
  never delays them.
- **State the UI uses is only touched on the main thread.** Workers get copies of what they need and post results back.
  There are no locks in UI code.
- Every job gets a `CancelToken`. An object that starts jobs keeps a `JobGuard` per job, which cancels it when the object
  goes away; results of cancelled jobs are dropped before they reach the main thread.
- Music plays on its own thread (`sys/audio`), and so does the QR scanner's camera.

## Errors

Functions that can fail return `Expected<T>` (`core/result.hpp`): a value, or an `Error` with an `Errc` the UI turns into
a message for the user, plus the system's code and a note for the log. `TZ_TRY(expr)` unwraps a value or returns the
error; `TZ_CHECK(expr)` does the same for `Expected<void>`. There are no exceptions (`-fno-exceptions`).

Messages to the user say what happened and what to do, in plain words. The `Error::detail` is for the log.

## Conventions

- C++23, no exceptions or RTTI. Types are `PascalCase`, functions and variables `camelCase`, members end in `_`,
  constants are `kName`. Files are `snake_case`. Everything is in `namespace tz`.
- `.clang-format` has the formatting.
- Each file starts with its SPDX license line and copyright. New code is GPL-3.0-or-later.
- Comments explain why, and the formats: offsets and their meaning, where they come from.
- User-visible text goes through `app/strings` so it can be translated (below).

## Translations

The app speaks the console's language: `romfs/lang/<code>.json` maps each English text to its translation, and a text
without one shows in English. The English in the code is the key:

- `tr("Back")` for a text, `trf("Delete {}?", name)` to fill in its `{}`s in order.
- `trn("{} badge", "{} badges", count)` for a text with a count, which fills its first `{}`. Its value in a translation
  is an array of the language's plural forms (`core/plural.hpp`): one for Japanese, Chinese and Korean, three for
  Russian, two for the rest.
- Give them string literals, both arms of a `?:` at most: `tools/i18n.py` finds the keys in the code, and fails on a
  `tr()` of a variable. Translate where a text is made, not where it's drawn.

After changing a text, `python3 tools/i18n.py sync` adds it to every language's file, empty, and drops what the code
doesn't have any more. `make test` runs `tools/i18n.py check`, which fails on a stale key, a `{}` too many or too few,
or plural forms of the wrong length, and lists what isn't translated yet. To add a language the console has, give its
code in `app/strings.cpp`, `tools/i18n.py`, and its plural rule in `core/plural.cpp`.

Translations are about as long as the English. The hint bar has the least room: when a page's hints don't fit, their
labels get smaller, then the keys closer together, then the longest labels are cut short with "…", and only when that
would leave less than a few letters does the first hint go (as SELECT does in English while picking a shuffle). Dialog
buttons get smaller, then cut short. So keep hint labels to a word, and check them in Azahar, whose font is a little
wider than the console's. Translations use only characters the console's font has. Folder and file names the app makes
on the SD card, the log, and names (Themezer, ThemePlaza, START, HOME...) stay English. The HOME Menu's description of
the CIA in each language is `APP_DESCRIPTIONS` in the Makefile: the Themezer website's slogan.

## The screens

The screens are small (400x240 and 320x240) and their pixels large, so the look keeps to a few rules:

- **Readable text.** The console's font at `metric::kTextNormal` for most text and `kTextSmall` for second lines and
  hints; nothing smaller. When something doesn't fit, it's shortened or left out, not shrunk.
- **Sharp edges.** Text and icons are drawn from whole pixels. Icons are Lucide's, drawn at the size they're shown
  (`metric::kIcon`, or `kIconLarge` on empty pages and in messages), never scaled.
- **Lined up.** Text in a box or next to an icon is placed with `textTop(middle, size)`, which puts the middle of its
  capitals on the middle of the box or the icon.
- **The console's buttons.** Hints show the font's own button symbols (`ui::drawButton`); START and SELECT, which the
  font hasn't got, are small pills with the word.
- **The item on top, the list below.** The top screen shows the selected item's preview, as the HOME Menu would, with
  nothing over it; without a preview, its icon, name and description. The rows (`scenes/frame.hpp`, `row::`) look the
  same on every page, and say what the console uses now: "In use", "In shuffle", "Top in use".

## The formats

- **Themes**: `domain/themes` and `domain/home_theme`. The HOME Menu reads a custom theme from its theme extdata
  (`0x2CC`/`0x2CD`/`0x2CE`/`0x2CF` by region): `BodyCache.bin` and `BgmCache.bin` (or the `_rd`/`_NN` files for shuffle),
  with their sizes in `ThemeManage.bin`, selected in the HOME Menu's `SaveData.dat`. The research behind the field
  meanings is in the Themezer-3DS history (theme-management research, October 2026).
- **Badges**: `domain/badges`. `BadgeData.dat` and `BadgeMngFile.dat` in extdata `0x14D1`.
- **Splashes**: `domain/splashes`. Luma3DS reads `/luma/splash.bin` and `/luma/splashbottom.bin`.

## Error reports

An error dialog offers to share the error (Y) when the app is to blame for it: `ErrorInfo::report` is set by `describe()`
and the storage errors in `scenes/errors.cpp` for the codes `report::shareable()` (`domain/report`) names, and not for
the network, a full SD card, cancelling, files too big for the HOME Menu or what the HOME Menu has in use. Pressing
the button has `services/reporter` send `domain/report`'s JSON to `POST <api>/reports` in the background; nothing is
ever sent otherwise. Themezer's side only logs the report, which is how it is looked up later. It is text, a few KiB at
most: no files. A diagnostic worth having in a report belongs in the `Error`'s `detail`.

## Testing

`make test` runs the unit tests on your computer. The rest is tried in the Azahar emulator (`make preview`) and on a
3DS: `make send` starts the 3dsx in the Homebrew Launcher over the network (`THREEDS_IP=...` if it isn't found by
itself). [DEVELOPMENT.md](DEVELOPMENT.md) has the autopilot, which takes screenshots of the screens in either.
