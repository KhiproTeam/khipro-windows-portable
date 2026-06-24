# Khipro - Windows Portable

## About Khipro

Khipro is the first compositional lowercase keyboard layout concept for Bengali, designed to be the fastest typing method for Bengali. It uses a fixed-phonetic approach where you type Latin characters and they are composited into Bengali script in real time.

The input method engine is implemented by the cross-platform [khipro library](https://github.com/KhiproTeam/khipro-library). This repository is a Windows portable build that wraps the same engine into a standalone system tray application — no installer, no dependencies, just run the exe.

## Usage

1. Download `khipro.exe` from the [latest release](https://github.com/KhiproTeam/khipro-windows-portable/releases/latest)
2. Run it — a tray icon appears in the system tray
3. **Toggle on/off:**
   - `Ctrl+Space`
   - Left-click the tray icon
   - Right-click the tray icon and select Active/Inactive
4. **Exit:** Right-click the tray icon and select Exit
5. **Auto-start on login:** `Win+R` > type `shell:startup` > copy `khipro.exe` into that folder
6. Hover the tray icon to see version and status

## Versioning

Hover the tray icon — the `Version:` line shows the portable's full version as three dot/dash-separated parts:

```
Version: 35.0.1-1-1
          ─┬─  ─┬─ ─┬
           │    │   └─ portable patch   (this repo)
           │    └───── library patch    (khipro library)
           └────────── layout version   (m17n spec)
```

- **Layout version** (e.g. `35.0.1`) — the m17n Bangla layout spec, from [rank-coder/khipro-m17n](https://github.com/rank-coder/khipro-m17n).
- **Library patch** (e.g. the middle `-1`) — khipro library patch number; together with the layout version forms the library version (`35.0.1-1`), returned at runtime by `khipro_library_version()`.
- **Portable patch** (the last `-1`) — this repo's own patch. Stays at `1` by default; bump it in [src/resources.h](src/resources.h) (`KHIPRO_PORTABLE_PATCH`) when making portable-only changes between library releases, and reset it to `1` when the library version moves.

Each portable release is tagged `v<layout>-<library_patch>-<portable_patch>` (e.g. `v35.0.1-1-1`) — find your exe's release on the [releases page](https://github.com/KhiproTeam/khipro-windows-portable/releases).

## Build

The library artifact (`khipro.h` + `libkhipro.a`) is **vendored** in [`resources/khipro/`](resources/khipro) and checked into the repo, so `make` works fully offline — no network access or `gh` auth required.

**Prerequisites:** `g++-mingw-w64-x86-64` and `gcc-mingw-w64-x86-64` for cross-compiling. (Add `gh`, `curl`, `unzip` only if you intend to refresh the vendored library.)

```
make          # offline cross-compile → khipro.exe
```

### Refreshing the vendored library

To pull the latest release from [KhiproTeam/khipro-library](https://github.com/KhiproTeam/khipro-library/releases) and overwrite `resources/khipro/`:

```
make update-lib
```

This downloads `khipro-<version>-windows-x86_64.zip`, extracts only the header + static lib, and updates `resources/khipro/.tag`. Review the diff and commit. CI also runs this automatically on the daily poll when a new library tag is detected.

Reset [`KHIPRO_PORTABLE_PATCH`](src/resources.h) back to `"1"` whenever the library version moves.

### CI release flow

[`.github/workflows/release.yml`](.github/workflows/release.yml) runs daily, on manual dispatch, and on push to `main` when `src/resources.h` or `resources/khipro/` change. The decision logic:

- Compares the **target portable tag** (`v<lib_ver>-<portable_patch>`) against the last released tag in `.release-tag`.
- If they **match** → exits early. No `update-lib`, no build, no release.
- If they **differ** → runs `update-lib` only when the upstream library tag is newer than the vendored one, then builds, commits, and publishes under the new tag.

Two ways the tag moves: (1) the library releases a new version (CI auto-bumps `resources/khipro/`), or (2) you bump `KHIPRO_PORTABLE_PATCH` for a portable-only change. Either triggers a release; nothing else does.

## Contributors

![Contributors](https://contrib.rocks/image?repo=KhiproTeam/khipro-windows-portable)

## License

[Mozilla Public License v2.0](LICENSE) (MPL-2.0). `khipro.exe` statically links the [khipro library](https://github.com/KhiproTeam/khipro-library), which is MIT-licensed.
