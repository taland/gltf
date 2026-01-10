# glTF (C11)

Minimal C11 + CMake project that builds a `gltf` static library, examples, and tests.

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
./build/bin/gltf_example_01_dump tests/fixtures/01-minimal.gltf
```

## Run tests

Configure/build as usual:

```sh
cmake -S . -B build
cmake --build build [--clean-first]
```

Run via CTest:

```sh
ctest --test-dir build [--output-on-failure]
```

Or run the test binary directly:

```sh
./build/bin/gltf_tests
```
