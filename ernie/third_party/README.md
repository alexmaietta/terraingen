# Vendored third-party code

Unmodified copies of small single-header libraries used by Ernie. Each directory keeps
the upstream license; the `.cpp` file beside the headers is ours and only compiles the
implementation. Build glue is in `cmake/third_party/<name>.cmake`.

| Library | Version | Files | License | Used for |
| --- | --- | --- | --- | --- |
| [stb](https://github.com/nothings/stb) | commit `2c980bb59875b0d32144a71867fbdebb2f77cd20` | `stb_image.h`, `stb_image_write.h` | MIT or public domain | Reading PNG (8/16-bit) and JPEG textures; zlib for tinyexr |
| [tinyexr](https://github.com/syoyo/tinyexr) | `v1.0.9` | `tinyexr.h` | BSD-3-Clause (with OpenEXR BSD-3-Clause portions) | Reading OpenEXR textures |

To update one, replace its headers with the files from the new upstream version, update the
version here, and rerun the image tests (`ernie_tests_unit` or, inside TerrainGen, `terraingen_tests_ernie_unit`, with `--gtest_filter='Image*:*Png*:*Exr*'`).
