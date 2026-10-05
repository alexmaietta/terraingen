# Vendored third-party code

| Library | Version | License | Source | Used by |
| --- | --- | --- | --- | --- |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | `json.hpp` release asset; SHA-256 `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`, identical to `single_include/nlohmann/json.hpp` at the tag | `terraingen::pipeline` (private) |
| [miniz](https://github.com/richgel999/miniz) | 3.1.2 | MIT | `miniz.c` and `miniz.h` from the `miniz-3.1.2.zip` release asset; SHA-256 of the zip `f0446d863f9c19926ad9483c523fdc42e42b8d4a6a431d27e09d49c79a140d9a`, matching GitHub's published digest | `terraingen::core` (private): Deflate in GeoTIFF |

Files are unmodified; each folder keeps the library's license.
