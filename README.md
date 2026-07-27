# WargusHD Android

An Android port of the open-source **Stratagus** real-time strategy engine, aimed at
running classic 2D RTS gameplay natively on phones and tablets in high definition.

WargusHD Android is an engine only. It ships **no game data**. To play, you supply your
own legally-owned game files on your device; the engine loads them at runtime. This is
the same model used by projects like DevilutionX and ScummVM.

## Status

Early development. The engine builds and runs on desktop today; the Android target is
being brought up milestone by milestone (build system, on-device runtime, touch
controls, mobile UI, HD polish). Expect rough edges.

## Built on

- **Stratagus** — a free, cross-platform RTS engine (GPLv2).
- **SDL2** (with SDL2_mixer and SDL2_image) for graphics, audio and input.
- **CMake** build, **C++17**, **Lua** (tolua++) for data-driven game logic.

## Building

Desktop builds use CMake and the SDL2 family of libraries. See `doc/` for engine and
scripting reference material. Android build instructions will be documented here as the
`android/` project stabilizes.

```sh
cmake -B build
cmake --build build
```

## Controls

Desktop uses mouse and keyboard. Touch controls for Android (tap-select, drag-select,
pinch-zoom, on-screen commands) are in progress.

## License

The engine is distributed under the **GNU General Public License, version 2** — see
`COPYING`. Third-party components retain their own licenses (see `doc/`). Game content
is not included and is not covered by this license; you must provide your own.
