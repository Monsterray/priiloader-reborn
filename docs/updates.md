# Updates

Priiloader can check for a new version of itself from its menu (the update screen, `Check For Update`). Two sources exist; the setting `Update from:` in the settings screen picks one. It is stored in the settings file (`UpdateSource`) and survives updates.

## Update from

| Value | What it reads | Transport |
| --- | --- | --- |
| `GitHub` (default) | the releases of `Monsterray/priiloader-reborn` | HTTPS (TLS 1.2, certificates always checked), `api.github.com` and the release download hosts |
| `DacoTaco` | `www.dacotaco.com/priiloader/versionV2.bin` (then `.dat`), `Priiloader_Update.dol`, `Priiloader_Beta.dol` | plain HTTP, the file is checked against the SHA-1 in `versionV2.bin` |

The DacoTaco source is upstream's: the versions it offers are upstream's, not this fork's. Its code path is the one Priiloader always had (the HTTP client changed, its behaviour for that server did not; `tests/host/http` still passes the old tests).

## Release convention for the GitHub source

The updater lists `GET /repos/<owner>/<repo>/releases?per_page=30` and chooses from it by these rules.

- Tag `vX.Y.Z` is a release. Tag `vX.Y.Z-rcN` (N from 1) is a release candidate. Numbers are 0 to 255. Any other tag (no `v`, four numbers, `-rc0`, text after the number) is ignored.
- A release must not be marked as a pre-release on GitHub. A release candidate must be marked as a pre-release (the tag decides what it is; the flag keeps it off GitHub's "latest").
- Draft releases are skipped.
- The newest release and the newest release candidate are offered. Release candidates show only when `Show RC Updates` is on.
- The release carries one asset named exactly `Priiloader_Update.dol`: the Installer DOL (`src/Installer/Installer.dol`), the file that runs the install or update on the Wii.
- GitHub shows a `sha256:<hex>` digest for each uploaded asset. Priiloader requires it: no digest, or a file that does not hash to it, is refused and nothing is started.
- The release notes (the body of the release) are shown as the changelog, as plain ASCII.
- The same rules about "newer" apply as for the DacoTaco source: a release update is a newer version, or the same version while a release candidate runs; a release candidate update is a newer candidate of the same version, or a candidate of a newer version.

## What the screen says when it fails

| Message | Meaning and what to do |
| --- | --- |
| `failed to initialise wifi` | The network did not come up (no connection set in the Wii's settings, or DHCP failed). |
| `no network connection`, `the host name did not resolve`, `could not connect to the server`, `the server did not answer in time` | The network is up but the server cannot be reached. Try again; check the router. |
| `the Wii's clock is wrong (certificate dates do not fit)` plus `check the date and time of the Wii` | The date in Wii Settings is outside 2026-01-01 to 2050-01-01 (or a certificate's dates do not fit it). Set the date and time in the Wii Menu settings. Certificates `expired` and `not valid yet` give the same advice. |
| `certificate not trusted`, `certificate is for another host`, `certificate rejected` | The server's certificate does not chain to a root built into Priiloader, or is for another name. This can mean a proxy rewriting the connection. Nothing is downloaded. A GitHub change of root certificate needs a new Priiloader build (roots are in `src/priiloader/tls`). |
| `TLS handshake failed`, `TLS protocol error`, `could not seed the random generator` | The connection broke inside TLS. Try again; report it with the Gecko or Dolphin log if it repeats. |
| `GitHub answered HTTP 403` (or 429) | Rate limit: GitHub allows 60 unauthenticated API calls an hour per address. Wait and try again. Other codes (404 for a wrong repository name, 5xx for an outage) show as `GitHub answered HTTP <n>`. |
| `the download was cut short`, `the reply is too big or of unknown length` | The download ended early, or it was chunked or larger than the limit (16 MB; the release list 1 MB). |
| `the server is too slow` | One call took longer than its deadline (120 s; 600 s for the update DOL), however slowly the server kept sending. |
| `GitHub answered HTTP 403` (or 429) | GitHub refused the request, usually its rate limit; the second line is GitHub's own message. |
| `GitHub says: <message>` | The API answered with an error text. |
| `the release list is not readable` | The reply was not the release list. |
| `this release has no Priiloader_Update.dol` / `this release has no sha256 digest`, then `update refused` | The release was made without the asset, or GitHub did not give a digest. Fix the release. |
| `the download does not match its sha256`, `update refused` | The file is not the one GitHub lists. Nothing is started. |
| `No version updates` / `No RC update` | There is nothing newer. A repository with no releases at all is this case, not an error. |

## How to cut a release by hand

There is no CI release job yet. As the maintainer, from a clean `develop` checkout:

1. Build the whole tree from the devkitPro shell (`make`) and run `python -m unittest discover -s tests`. The version comes from `git describe` and `src/Shared/version.h` (`VERSION_MAJOR`, `VERSION_MINOR`, `VERSION_PATCH`, `VERSION_RC`): set these to the release first and commit.
2. Tag the commit `vX.Y.Z` (or `vX.Y.Z-rcN`) and push the tag. Build again so the Installer's `meta.xml` carries the tag (`git describe`), then run `python tools/make_sd.py --release`: `dist/release` holds `Priiloader_Update.dol` (the Installer built from that commit), the SD card zip and `SHA256SUMS`.
3. Only `Priiloader_Update.dol` is read by the updater; other assets are ignored.
4. On GitHub create the release from that tag: write the notes, tick "pre-release" for a release candidate only, and upload the files in `dist/release`. GitHub computes the digest on upload; check on the release page (or `gh api repos/Monsterray/priiloader-reborn/releases`, field `assets[].digest`) that it shows `sha256:...` and that it equals `sha256sum Priiloader_Update.dol`.
5. Do not leave it as a draft: drafts are skipped.
6. Check with a Wii that is not the only one you have: `Check For Update` offers it, the changelog reads right, and the install runs (attended, with a fresh BootMii NAND backup: the install writes the NAND).

Never re-upload an asset under a published release without checking the digest again: the updater trusts GitHub's digest, so it follows whatever file is there.
