# glTF (C11)

Minimal C11 + CMake project that builds a `gltf` static library, examples, and tests.

glTF 2.0 spec reference: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html

## Contents

- [Quick start](#quick-start)
- [Build](#build)
- [IDE builds (Xcode / VS 2022)](#ide-builds-xcode--vs-2022)
- [Examples](#examples)
- [Tests](#tests)
- [Project layout](#project-layout)
- [Requirements](#requirements)

## Quick start

```sh
cmake -S . -B build
cmake --build build
./build/bin/gltf_example_01_dump tests/fixtures/01-minimal.gltf
```

## Build

```sh
cmake -S . -B build
cmake --build build
```

## IDE builds (Xcode / VS 2022)

Xcode (macOS):

```sh
cmake -S . -B build-xcode -G Xcode
cmake --build build-xcode
```

Visual Studio 2022 (Windows, x64):

```powershell
cmake -S . -B build-vs2022 -G "Visual Studio 17 2022" -A x64
cmake --build build-vs2022
```

## Examples

See the examples guide in [examples/README.md](examples/README.md).

## Tests

Configure/build as usual:

```sh
cmake -S . -B build
cmake --build build [--clean-first]
```

Run via CTest:

```sh
ctest --test-dir build [--output-on-failure]
```

Multi-config generators (Visual Studio) require a configuration:

```sh
ctest --test-dir build -C Debug [--output-on-failure]
```

Or run the test binary directly:

```sh
./build/bin/gltf_tests
```

## Project layout

- `include/gltf/` public headers
- `src/` library sources
- `examples/` example programs
- `tests/` test sources and fixtures
- `third_party/` third-party dependencies

## Requirements

- C11 compiler
- CMake 3.25+ (or newer)
