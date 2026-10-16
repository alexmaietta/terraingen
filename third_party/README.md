# Third-party code and assets

TerrainGen is built on the following open source libraries and assets. Each keeps its own license.

## Libraries

| Library | Purpose | License |
| --- | --- | --- |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON configuration files | MIT |
| [miniz](https://github.com/richgel999/miniz) | Deflate compression for GeoTIFF reading and writing | MIT |
| [cgltf](https://github.com/jkuhlmann/cgltf) | glTF mesh reading and writing | MIT |
| [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) | OBJ and MTL mesh reading | MIT |
| [nanobind](https://github.com/wjakob/nanobind) | Python bindings | BSD-3-Clause |
| Ernie | Rendering and user interface of the GUI | TBD |

## Vendored code

| Library | Version | License | Source | Used by |
| --- | --- | --- | --- | --- |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | `json.hpp` release asset; SHA-256 `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`, identical to `single_include/nlohmann/json.hpp` at the tag | `terraingen::config` (private) |
| [miniz](https://github.com/richgel999/miniz) | 3.1.2 | MIT | `miniz.c` and `miniz.h` from the `miniz-3.1.2.zip` release asset; SHA-256 of the zip `f0446d863f9c19926ad9483c523fdc42e42b8d4a6a431d27e09d49c79a140d9a`, matching GitHub's published digest | `terraingen::core` (private): Deflate in GeoTIFF |
| [cgltf](https://github.com/jkuhlmann/cgltf) | 1.15 | MIT | `cgltf.h` and `cgltf_write.h` at tag `v1.15`; SHA-256 `e378a21c084bf1f288bb799de827bb26906efb024255f1ecf1705ea13f11c6ec` and `fb91760451d5c63bea1c0f606ddecb5cf8c17bfe02bf0f23ee4f635af5c47996` | `terraingen::mesh` (private) |
| [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) | 2.0.0rc13 | MIT | `tiny_obj_loader.h` at tag `v2.0.0rc13`; SHA-256 `a55a0933a29caa0e9e84ef62a45cbe8cb25dd0e17bc2850c591c6b06f95d5ebe` | `terraingen::mesh` (private) |

Files are unmodified; each folder keeps the library's license.

## Assets

The lunar regolith textures in [`assets/textures`](../assets/textures/README.md), used as tiling detail for Moon terrain materials, are from Poly Haven:

| Asset | Author | Use | License |
| --- | --- | --- | --- |
| [Moon 01](https://polyhaven.com/a/moon_01) | Greg Zaal, Rico Cilliers and Jenelle van Heerden (photography), Dario Barresi (processing), [Poly Haven](https://polyhaven.com) | Diffuse, normal, roughness and displacement maps of about 3.1 m of lunar regolith per tile | CC0 1.0 |
