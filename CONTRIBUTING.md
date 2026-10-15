# Contributing to TerrainGen

Thank you for helping. Bug reports, questions, references and pull requests are all welcome; for anything larger than a fix, please open an issue first so we can agree on the approach.

## Building and testing

TerrainGen needs CMake 3.22 or newer and a C++20 compiler (GCC 11, Clang 14, Apple Clang 15 or MSVC 2022). Everything else is fetched by CMake.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
ctest --test-dir build -j 8 --output-on-failure
```

The Vulkan compute tests skip on machines without a Vulkan device. The Python bindings build with `pip install .`; run their tests with `pytest tests/python`.

Coverage needs GCC or Clang:

```sh
cmake -S . -B build-coverage -DTERRAINGEN_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-coverage -j 8
ctest --test-dir build-coverage -j 8
```

The 3D GUI (`terraingen CONFIG --gui`) builds with Ernie, the renderer, from a local checkout: `-DTERRAINGEN_ERNIE_DIR=../ernie`. Without one, configuring warns and builds everything else, and `--gui` says how to add it; `-DTERRAINGEN_BUILD_GUI=OFF` leaves the GUI out without the warning.

## Pull requests

- Every change comes with tests, and new code should be covered by them.
- Run [pre-commit](https://pre-commit.com) before committing (`pre-commit install` once): it formats C++ with clang-format and CMake with cmake-format, and runs cppcheck.
- Keep commits focused, with a one-line message saying what changed.
- CI must pass on Linux, macOS and Windows.

## Conventions

- **Cite what you model.** Constants, fits and models come from published sources: name the paper in the doc comment with its DOI or URL. If a value has no source, say so rather than guess.
- **Plain ASCII** in code, comments, docs and commit messages: write "metres", "degrees", "D^-2" and "sigma" rather than Unicode symbols.
- **Document public headers** with `///` comments that say what a function does, its units and what it throws; prefer clear names and doc comments to inline comments.
- **Dependencies:** TerrainGen stays light. Please discuss a new third-party library in an issue before adding it; vendored code in `third_party/` stays byte-identical to upstream.
- **Configuration fields** carry a description and, for numbers, a unit (see the field lists in `terraingen/pipeline/detail/fields_*.h`); a test checks every field has them.

## Reporting bugs

Open an issue with the configuration (or code) that shows the problem, what you expected and what happened, and your platform.

By contributing you agree that your contributions are licensed under the project's [BSD 3-Clause License](LICENSE).
