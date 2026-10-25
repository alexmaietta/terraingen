# TerrainGen's documentation

Install `mkdocs` and related dependencies.

```sh
pip install -r requirements.txt
```

For C++ API, install `doxygen`.

```sh
sudo apt install doxygen
```

For the Python API, TerrainGen must be built with Python bindings, `-DTERRAINGEN_BUILD_PYTHON=ON`.

The configuration reference is generated from the same build. The `scripts/generate_schema.py` script reads the library's configuration schema at build time and writes the pages under `reference/`, so nothing there is kept in the source tree.

There are also snippets that can be referenced in the documentation to provide users example API usages and demonstrations.

The gallery is designed to generate reproducible images that are then embedded in the documentation to visually highlight the different effects TerrainGen can model.

We provide the following make commands.

```sh
make build
make serve
make gallery
make gallery-check
make clean
```
