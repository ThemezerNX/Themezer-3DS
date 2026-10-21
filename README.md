# Themezer-3DS

The theme manager for the Nintendo 3DS by [Themezer](https://themezer.net). Install themes, splash screens and
badges from your SD card, or find new ones on [Themezer](https://themezer.net/3ds) or [ThemePlaza](https://themeplaza.art) and install them
straight away: browse and search them, scan an item's QR code, or type its ID.

<p>
  <img src="docs/screenshots/themes.png" width="32%" alt="The themes on the SD card">
  <img src="docs/screenshots/splashes.png" width="32%" alt="Splash screens on Themezer">
  <img src="docs/screenshots/badges.png" width="32%" alt="Badges on Themezer">
</p>

Themezer-3DS is written from scratch, inspired by [Anemone3DS](https://github.com/astronautlevel2/Anemone3DS).

## Installing

- **CIA:** To install the [latest release](https://github.com/ThemezerNX/Themezer-3DS/releases/latest), scan this QR with FBI:  
![image](https://github.com/user-attachments/assets/eecf1ec0-cf46-4c56-bb55-9fe1d215d976)
- **CIA:** install `themezer-3ds.cia` from the
  [latest release](https://github.com/ThemezerNX/Themezer-3DS/releases/latest), for example with FBI.
- **3DSX:** copy `themezer-3ds.3dsx` from the [latest release](https://github.com/ThemezerNX/Themezer-3DS/releases/latest) to `/3ds/` on your SD card and start it from the Homebrew Launcher.

Themes, splash screens and badges are read from `/Themes`, `/Splashes` and `/Badges` on the SD card, the same folders
Anemone3DS uses. The app keeps its settings and caches in `/3ds/themezer-3ds`.

## Compared with Anemone3DS

What you'll notice coming from Anemone3DS 3.0.1, its latest release:

| | Themezer-3DS | Anemone3DS |
| --- | --- | --- |
| Speed | ✅ Faster SD card loading, faster downloads and browsing, much better QR scanning | |
| Online | ✅ Themezer and ThemePlaza: search, sort, or type an item's ID | ThemePlaza: search |
| Library Mangement | ✅ Several install options, easier management, uninstall the current theme, splash or badge set | |
| Initial setup | ✅ None: automatically creates the required files and turns on Luma3DS's splashes for you | Requires manually changing settings |
| Splash screens | ✅ Install the top or bottom screen on its own | Both screens together |
| Badges | ✅ Every downloaded set shows up as its own set, disable/enable sets | Downloaded sets all go into one set, no mangement possible |
| Updates | ✅ Updates itself | Manual updates |

And many more bugfixes for issues Anemone3DS has had for years!

## Building

You need [devkitPro](https://devkitpro.org/wiki/Getting_Started) with devkitARM and these packages:

```sh
dkp-pacman -S 3ds-dev 3ds-curl 3ds-mbedtls 3ds-jansson 3ds-libpng 3ds-zlib
```

and [makerom](https://github.com/3DSGuy/Project_CTR) and [bannertool](https://github.com/diasurgical/bannertool) on
your `PATH`. Then:

```sh
make          # out/themezer-3ds.3dsx and out/themezer-3ds.cia
make test     # unit tests, on your computer
make send     # sends the 3dsx to a 3DS waiting in the Homebrew Launcher (Y)
make preview  # opens the 3dsx in the Azahar emulator
```

[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) has more: trying the app in an emulator, and the autopilot that walks
through its screens and takes screenshots.

## License

Themezer-3DS is free software: you can redistribute it and/or modify it under the terms of the
[GNU General Public License](LICENSE), version 3 or (at your option) any later version, like Anemone3DS.

The libraries the app is built with keep their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
The app shows these notices on its About screen.

## Credits

Themezer-3DS is made by [Themezer](https://themezer.net) and [its contributors](CONTRIBUTORS.md).

It was inspired by Anemone3DS and wouldn't exist without it: thanks to Alex Taber
([@astronautlevel2](https://github.com/astronautlevel2)) and the
[Anemone3DS contributors](CONTRIBUTORS.md#anemone3ds-contributors).

The file formats the app works with were documented by others before it:

- [3dbrew](https://www.3dbrew.org), for the HOME Menu's theme, badge and extdata formats.
- [yellows8](https://github.com/yellows8)'s [3ds_homemenu_extdatatool](https://github.com/yellows8/3ds_homemenu_extdatatool),
  for the theme cache.
- [MrCheeze](https://github.com/MrCheeze)'s [GYTB](https://github.com/MrCheeze/GYTB) and
  [AntiMach](https://github.com/AntiMach)'s [Advanced Badge Editor](https://github.com/AntiMach/advanced-badge-editor),
  for the badge files.
- [Luma3DS](https://github.com/LumaTeam/Luma3DS), for its splash screens.

The banner, boot animation, tune and background music are Themezer's own; [meta/README.md](meta/README.md) describes
how they were made and how to rebuild them.
