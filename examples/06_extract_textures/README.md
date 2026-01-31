# 06_extract_textures

Extracts images from a glTF file and writes them as PNGs.

## Purpose

- Demonstrates `gltf_image_decode_rgba8()` and `gltf_write_png_rgba8()`.
- Exercises image sources from URI, data URI, and bufferView.
- Provides a simple end-to-end texture extraction workflow.

## Usage

```
./gltf_example_06_extract_textures <model.gltf|model.glb> [out_dir] [--force]
```

- `out_dir` defaults to `out`.
- If `out_dir` does not exist, pass `--force` to create it.

## Notes

- Requires the library built with `GLTF_ENABLE_IMAGES=ON`.
- Output PNGs are named `image_XX_WIDTHxHEIGHT.png`.
