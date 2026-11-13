# FreeRod

An open-source native C11 + SDL2 reimplementation of the 1989 DOS classic *Street Rod*.

FreeRod is an independent open-source reimplementation. It is not affiliated with, endorsed by, or connected to California Dreams, Logicware, or the current rights holders of *Street Rod*. A legal copy of the original DOS game data files is required to play.

## Status

FreeRod is a work in progress. Currently implemented:

- Opening screens, copy-protection question, and new game setup
- Newspaper, classified ads, and buying/selling cars and spare parts
- Garage, under-the-hood engine work, tires, transmission, paint/bodywork, and gas station
- Street (Bob's Drive-In), opponent challenges, race outcomes, save/load, and Hall of Fame
- Built-in resource viewer (`--viewer`) and optional hi-res PNG art replacement (`--mod`)

Still in progress:

- Driving and 3D road rendering
- Sound and music output

## Requirements

- Original DOS *Street Rod* data files (`Sr.exe`, `Lib1`, `Lib2`, `Hot_data`, and optionally `Hall_dat` / `HOTROD*.SAV`)
- C11 compiler
- CMake 3.21+
- `SDL2` and `SDL2_image`

## Building

### Windows (MSVC, MinGW GCC, or MinGW Clang + Ninja + vcpkg)

Run from a Visual Studio x64 Native Tools Command Prompt or a shell with MinGW `gcc`/`clang` on `PATH` and `VCPKG_ROOT` set (or let `build.bat` locate Visual Studio 2022 and its bundled `vcpkg`):

```bat
build.bat
```

You can choose the compiler (`msvc`, `gcc`, `clang`) and configuration (`debug`, `release`):

```bat
build.bat release
build.bat gcc
build.bat clang release
```

Or invoke CMake directly using the included presets (`msvc`, `mingw-gcc`, `mingw-clang`):

```bat
cmake --preset msvc
cmake --build --preset msvc-debug
```

For MinGW GCC or Clang:

```bat
cmake --preset mingw-gcc
cmake --build --preset mingw-gcc-debug

cmake --preset mingw-clang
cmake --build --preset mingw-clang-debug
```

The executable is written to `build\<msvc|gcc|clang>\<Debug|Release>\freerod.exe` with the SDL2 DLLs copied alongside it.

### Linux / macOS (GCC or Clang + CMake)

With `SDL2` and `SDL2_image` development packages available to CMake, use the `gcc` or `clang` preset (or plain CMake):

```sh
cmake --preset gcc
cmake --build --preset gcc-debug
```

## Running

Place the original game files in an `original/` directory in your working directory (or pass `--data DIR`):

```sh
freerod --data original --saves saves
```

Common options:

- `--data DIR` — directory containing the original game files (default: `original`)
- `--saves DIR` — directory for saved games and `Hall_dat` (default: `saves`)
- `--mod DIR` — directory with hi-res replacement PNGs (e.g. `lib1/000.png`, `lib2/...`)
- `--scale N` — initial window scale multiplier (default: `3`)
- `--render-scale N` — internal scene resolution multiplier (default: `4`)
- `--viewer` — launch the built-in resource viewer (`Left`/`Right` to browse, `Home` for title, `Esc` to quit)
- `--show N` — starting resource index in the viewer

Original DOS command-line switches (`1`, `nomouse`, `demo`, `auto`, `nouemem`) are also accepted.

## License

Licensed under the BSD Zero Clause License (0BSD). See [LICENSE](LICENSE).
