# Contributing to TerrainGen

Thank you for helping. Bug reports, questions, references and pull requests are all welcome; for anything larger than a fix, please open an issue first so we can agree on the approach.

## Building and testing

TerrainGen needs CMake 3.22 or newer and a C++17 compiler (tested with GCC 11, Clang 14, Apple Clang 15 and MSVC 2022). Everything else is fetched by CMake. Keep the library and tests C++17: no `std::span` (use `terraingen::Span`), `std::numbers` (use `kPi` from `util/bits.h`), defaulted comparisons, concepts or designated initializers.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
ctest --test-dir build -j 8 --output-on-failure
```

The Vulkan compute tests skip on machines without a Vulkan device. The Python bindings build with `pip install .` (or into `build/python` with the CMake build); run their tests with `pytest tests/python`.

Tests are grouped by kind: `tests/unit` mirrors `src/` one folder per module, `tests/statistical` checks sampled distributions against their models, `tests/golden` pins generated values, `tests/integration` tests the build, install and packages, and `tests/python` has one file per module. Shared helpers live in `tests/support`.

Coverage needs GCC or Clang; the `coverage` target runs the tests and writes a report to `build-coverage/coverage/`:

```sh
cmake -S . -B build-coverage -DTERRAINGEN_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-coverage -j 8 --target coverage
```

The 3D GUI (`terraingen CONFIG --gui`) builds with Ernie, the renderer, from a local checkout: `-DTERRAINGEN_ERNIE_DIR=../ernie`. Without one, configuring warns and builds everything else, and `--gui` says how to add it; `-DTERRAINGEN_BUILD_GUI=OFF` leaves the GUI out without the warning.

## Pull requests

- Every change comes with tests, and new code should be covered by them.
- Run [pre-commit](https://pre-commit.com) before committing (`pre-commit install` once): it formats C++ with clang-format, CMake with cmake-format, Python with ruff, and runs cppcheck and ruff's linter.
- Keep commits focused, with a one-line message saying what changed.
- CI must pass on Linux, macOS and Windows.

## Conventions

- **Cite what you model.** Constants, fits and models come from published sources: name the paper, with its DOI or URL, in the doc comment, and keep the rest of the comment to what the code does. If a value has no source, say so rather than guess.
- **Plain ASCII** in code, comments, docs and commit messages: write "meters", "degrees", "D^-2" and "sigma" rather than Unicode symbols.
- **Document public headers** with short `///` comments: a `@file` line saying what the header holds, then one sentence per type and function. Add `@param` only for units, ranges or special values, `@return` only when the sentence does not say it, and `@throws` for each exception a function raises on purpose. Leave overrides undocumented, and prefer clear names to inline comments.
- **Dependencies:** TerrainGen stays light. Please discuss a new third-party library in an issue before adding it; vendored code in `third_party/` stays byte-identical to upstream.
- **Configuration fields** carry a description and, for numbers, a unit (see the field lists in `src/config/fields/`); a test checks every field has them.

## Reporting bugs

Open an issue with the configuration (or code) that shows the problem, what you expected and what happened, and your platform.

By contributing you agree that your contributions are licensed under the project's [BSD 3-Clause License](https://github.com/alexmaietta/terraingen/blob/main/LICENSE).
