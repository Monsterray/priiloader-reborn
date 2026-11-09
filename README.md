<p align="center" width="100%">
  <img width="720" alt="Priiloader" src="./Images/Logo.png">
</p>

<p align="center">
  <a href="https://github.com/Monsterray/priiloader-reborn/releases"><img alt="Release" src="https://img.shields.io/github/v/release/Monsterray/priiloader-reborn?include_prereleases"></a>
  <a href="./LICENSE"><img alt="License: GPL v2" src="https://img.shields.io/badge/license-GPL%20v2-blue"></a>
  <img alt="Platform: Wii, vWii, Wii Mini" src="https://img.shields.io/badge/platform-Wii%20%7C%20vWii%20%7C%20Wii%20Mini-lightgrey">
</p>

# Priiloader

[Priiloader](https://wiibrew.org/wiki/Priiloader) is a System Menu loader. It runs before the Wii Menu, so it can:

- autoboot the System Menu, the Homebrew Channel, BootMii, a disc, a title, or any DOL or ELF you install;
- show its own menu, where you can start homebrew when the System Menu is broken (a banner brick, say);
- patch the System Menu with hacks: block updates, region free, Wiimmfi and more;
- ask for a password before its menu or the autoboot.

It works on the Wii, the vWii and the Wii Mini. It is a heavily modified [Preloader 0.30](https://wiibrew.org/wiki/Preloader), by [DacoTaco](https://wiibrew.org/wiki/User:DacoTaco) and BadUncle.

This is **priiloader-reborn**: a fork of [DacoTaco/priiloader](https://github.com/DacoTaco/priiloader) that carries on from upstream 0.10. See [what is new](#what-is-new-since-upstream-010).

> **Warning.** Priiloader replaces the boot content of the System Menu on the Wii's NAND. Make a **BootMii NAND backup** before you install, update or remove it.
> Priiloader will **not** save a Wii whose System Menu IOS is broken (IOS80 on System Menu 4.3), or one whose boot1 or boot2 is broken.

## Install

1. Make a BootMii NAND backup, and keep it off the Wii.
2. Download `priiloader-reborn-X.Y.Z-sd.zip` from the [releases](https://github.com/Monsterray/priiloader-reborn/releases) and unzip it to the root of the SD card. It holds `apps/priiloader`.
3. Run **Priiloader** from the Homebrew Channel and press `+` or `A` to install. (`-` or `Y` removes it. `HOME` quits.)

You need a way to start homebrew. The Homebrew Channel 1.0.7 or newer needs no patched IOS, and Priiloader itself needs none either.

**To update**, run the installer again (it keeps your settings, hacks and password), or use **Check For Update** in Priiloader's own menu. Updates come from this fork's GitHub releases, over HTTPS, and the download is checked against the sha256 digest GitHub lists. The Wii's clock must be right, or the certificate check fails. Release candidates show only when **Show RC Updates** is on.

> Do not run `priiloader.dol` from the Homebrew Channel on a Wii that already has Priiloader installed. Use the installer.

## Use

- **Open the menu:** hold `RESET` while the Wii powers on. Pressing `B` on a Wii Remote or GameCube pad at that moment stops an autoboot too.
- **Settings and hacks:** in the menu. `Autoboot` picks what starts; `Return to` and `Shutdown to` pick what happens when a title exits or the Wii shuts down.
- **Something wrong at boot:** switch on `Dump Gecko output` in the settings. Priiloader writes `sd:/prii.log`, with the Wii clock's time and date on each boot. Attach it to a bug report.
- **Uninstall:** run the installer and press `-` or `Y`.

## What is new since upstream 0.10

- A stuck USB drive can no longer cause a black screen: the SD card mounts first, USB starts with the menu, and shutting the mounts down waits at most 2 seconds.
- Autoboot of a title no longer goes black until RESET; autoboot is faster (about 1.5 s to the Homebrew Channel on a real Wii).
- The installer restores the System Menu when a write fails, and works with no buttons: `Installer.dol install|update|remove`.
- The updater reads GitHub releases over HTTPS (mbedTLS) and checks the sha256 digest. `Update from` can still pick DacoTaco's server.
- Hack choices are saved by name (`hackstat.ini`). Settings from upstream's `loader.ini` carry over.
- The Wii Remote pointer works in the System Menu after Priiloader's menu. The USB keyboard works in the menu.
- Crash fixes in the autoboot path, the password check, the DVD drive and the ELF loader.
- A boot log with the Wii's time and date; a warning-free build; host and Dolphin tests.

The full list is in [CHANGELOG.md](./CHANGELOG.md).

## More

| Document | What is in it |
| --- | --- |
| [docs/](./docs/README.md) | Index of everything below |
| [FAQ](./docs/FAQ.md) | Questions, and the vWii notes |
| [Hacks](./docs/HACKS.md) and [list of hacks](./docs/HACKSLIST.md) | What the hacks do; how to write one |
| [SD card](./docs/sd-card.md) | What goes on the card, and what lives on NAND |
| [Settings](./docs/settings.ini) | Every setting, its default and its place in the file |
| [Updates](./docs/updates.md) | How the updater works; how to cut a release |
| [CONTRIBUTING.md](./CONTRIBUTING.md) | Set up, build, test, send a change |
| [REVIEW.md](./REVIEW.md) | The review plan, findings and open items |

Hack discussion and testing: [upstream discussion 354](https://github.com/DacoTaco/priiloader/discussions/354). Many user-made hacks are on the [WiiBrew hacks page](https://wiibrew.org/wiki/Priiloader/hacks).

## Build

Install devkitPro (devkitPPC r51 and libogc 3.1.0), then run `make` at the repository root. It makes `src/priiloader/priiloader.dol` and `src/Installer/Installer.dol`. [CONTRIBUTING.md](./CONTRIBUTING.md) has the full steps for Windows, Linux and macOS, and the tests.

## Credits

- **DacoTaco**, the author and maintainer of Priiloader, and **BadUncle**.
- **crediar**, whose Preloader 0.30 is the base; **phpgeek**, the installer.
- Everyone who sent code, hacks and bug reports upstream: see the [contributors](https://github.com/DacoTaco/priiloader/graphs/contributors) and [WiiBrew](https://wiibrew.org/wiki/Priiloader).
- The devkitPro team, libogc, and #wiidev.
- priiloader-reborn: [Monsterray](https://github.com/Monsterray) and contributors.

Priiloader is free. If you paid for it, you were scammed. Upstream's author takes donations on [ko-fi](https://ko-fi.com/dacotaco).

## License

GPL version 2 only. See [LICENSE](./LICENSE). Priiloader comes with no warranty.
