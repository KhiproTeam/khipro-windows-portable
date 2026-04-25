# Khipro - Windows Portable

## About Khipro

Khipro is the first compositional lowercase keyboard layout concept for Bengali, designed to be the fastest typing method for Bengali. It uses a fixed-phonetic approach where you type Latin characters and they are composited into Bengali script in real time.

The input method definition lives at [khipro-m17n](https://github.com/rank-coder/khipro-m17n) and works with the M17n library on Linux. This repository is a Windows portable build that wraps the same input method into a standalone system tray application — no installer, no dependencies, just run the exe.

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

## Build

On Linux you need `g++-mingw-w64-x86-64` and `gcc-mingw-w64-x86-64` for cross-compiling. On Windows you need MinGW-w64 with C++17 support.

```
make
```

That's it — `khipro.exe` will be in the project root.

## Contributors

![Contributors](https://contrib.rocks/image?repo=KhiproTeam/khipro-windows-portable)

## License

This repository is licensed under the [Mozilla Public License v2.0](LICENSE) (MPL-2.0).

> The input method file `data/bn-khipro.mim` comes from the `khipro-m17n` project and is licensed under the MIT License (see the file header and upstream repo: [https://github.com/rank-coder/khipro-m17n](https://github.com/rank-coder/khipro-m17n)).

