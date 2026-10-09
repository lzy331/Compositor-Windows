# Compositor (Windows)

A Windows port of [Compositor](https://github.com/robbietilton/Compositor), a free, open-source
image editor for compositing and post-processing. The original is a macOS app written in
Swift/SwiftUI/AppKit with Metal. This port reimplements it in **C++20 + Qt 6** so it builds and
runs natively on Windows.

The goal is feature parity with the original: the same `.comp` project format, the same layer
model, the same Photoshop-order blend modes, and the same tools.

## Relationship to the original

This is an independent, derivative implementation. It is **not** a fork of the macOS source — the
Swift code is used as the specification, and the C++ here reproduces its behaviour: the same
`.comp` manifest (format version 11), the same on-disk PNG layer files, the same error wording,
and the same numeric results where the original's math is observable.

Licensed under the MIT License, the same as the original. The original copyright notice is
retained in [`LICENSE`](LICENSE), as that license requires.

- Original: https://github.com/robbietilton/Compositor — © 2026 Wonder Assembly LLC
- This port: derivative work, MIT.

## Status

The port is being built in slices. What works today:

| Area | State |
| --- | --- |
| `.comp` project format (manifest v11 + PNG layers) | Done — read and write, byte-compatible naming |
| Document model, layer hierarchy, masks, clipping chains | Done |
| Compositing engine — every Photoshop blend mode, layer/folder masks, clipping masks, adjustment layers, layer effects | Done (CPU) |
| Canvas — zoom/pan/fit viewport, guides, selection outlines | Done |
| Layers panel — hierarchy, thumbnails, visibility, opacity, blend mode, reorder, folders | Done |
| Undo/redo — the original's snapshot history, entry/byte limits, saved-revision tracking | Done |
| Import images (drag-and-drop and file dialog), New Canvas | Done |
| Move tool, Hand tool, Zoom tool | Done |
| Brush and Eraser (deposition, soft tip, opacity cap, smoothing curve) | Done |
| Selection tools (marquee, lasso, wand), crop, gradient, shape, type, clone stamp, spot healing, blur/smudge/liquify | Not yet |
| GPU (Metal → Direct3D) rendering path | Not yet — this port composites on the CPU |

Everything listed as done is covered by the test suite (`ctest`, 7 suites).

## Building

Requirements:

- Windows 10/11
- Qt 6.8 (tested with `win64_mingw`; the `mingw_64` kit)
- MinGW-w64 GCC 13 (bundled with the Qt MinGW kit)
- CMake 3.24+
- Ninja (optional; any CMake generator works)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/mingw_64
cmake --build build
```

Run:

```sh
./build/Compositor.exe
```

Test:

```sh
cd build && ctest --output-on-failure
```

## Layout

```
app/                The executable's entry point
src/core/           Geometry, ids, limits, blend-mode tables
src/document/       The manifest model, hierarchy rules, adjustments
src/io/             .comp read/write, PNG decode/encode, image assets
src/render/         The compositing engine (raster, resample, layer draw, stack)
src/ui/             Viewport, session, canvas widget, layers panel, main window
tests/              One executable per suite, wired into ctest
third_party/        Vendored nlohmann/json
```

## License

MIT. See [`LICENSE`](LICENSE).
