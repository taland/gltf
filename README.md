# glTF (C11)

This repository is an early C11 + CMake skeleton for a `gltf` static library and a small example.

## Build

```sh
cmake -S . -B build
cmake --build build
```

## Run example

macOS/Linux:

```sh
./build/bin/gltf_example_01_dump
```

Windows (PowerShell):

```powershell
.\build\bin\gltf_example_01_dump.exe
```

You can also pass a path to a `.gltf` file:

```sh
./build/bin/gltf_example_01_dump examples/01_dump/sample_minimal.gltf
```

## Run tests

Configure/build as usual:

```sh
cmake -S . -B build
cmake --build build
```

Run via CTest:

```sh
ctest --test-dir build
```

Or run the test binary directly:

```sh
./build/bin/gltf_tests
```
