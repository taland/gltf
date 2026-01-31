# Examples

This directory contains example programs built alongside the `gltf` static library.

## Build

From the repository root:

```sh
cmake -S . -B build
cmake --build build
```

## Examples list

### 01_dump

Prints a high-level summary of a glTF file (asset version, scene/node/mesh counts), plus scene names.

Default input: `tests/fixtures/01-minimal.gltf`

```sh
./build/bin/gltf_example_01_dump [path/to/model.gltf]
```

### 04_world_trs

Computes world transforms for each node and prints a scene hierarchy with local/world TRS and AABBs.

Default input: `tests/fixtures/04-world_trs.gltf`

```sh
./build/bin/gltf_example_04_world_trs [path/to/model.gltf]
```

### 05_material_report

Emits a material report including PBR factors and texture bindings (with image/sampler details).

Default input: `tests/fixtures/05-materials.gltf`

```sh
./build/bin/gltf_example_05_material_report [path/to/model.gltf]
```

### 06_extract_textures

Extracts images from a glTF/GLB and writes them as PNGs.

Requirements:

- Build with `GLTF_ENABLE_IMAGES=ON`.

Usage:

```sh
./build/bin/gltf_example_06_extract_textures <model.gltf|model.glb> [out_dir] [--force]
```

- `out_dir` defaults to `out`.
- If `out_dir` does not exist, pass `--force` to create it.
- Output PNGs are named `image_XX_WIDTHxHEIGHT.png`.

## Notes

- macOS/Linux binaries are in `build/bin/`.
- Windows binaries end with `.exe` in `build\bin\` (PowerShell example: `gltf_example_01_dump.exe`).
