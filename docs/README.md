# TerrainGen's documentation

For C++ API, install `doxygen`.

```sh
sudo apt install doxygen
```

For the Python API, TerrainGen must be built with Python bindings, `-DTERRAINGEN_BUILD_PYTHON=ON`.

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
