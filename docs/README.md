# TerrainGen's documentation

For C++ API, install `doxygen`.

```sh
sudo apt install doxygen
```

For the Python API, TerrainGen must be built with Python bindings, `-DTERRAINGEN_BUILD_PYTHON=ON`.
The configuration reference is generated from the same build: `scripts/generate_schema.py` reads
the library's configuration schema at build time and writes the pages under `reference/`, so
nothing there is kept in the source tree.

Install `mkdocs` and related dependencies.

```sh
pip install -r requirements.txt
```

We provide the following make commands.

```sh
make build
make serve
make clean
```
