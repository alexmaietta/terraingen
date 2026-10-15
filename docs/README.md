# TerrainGen's documentation

```sh
pip install -r requirements.txt
make build    # the site into ../site/
make serve    # or preview it at http://127.0.0.1:8000
make clean
```

Build TerrainGen with Python into `build/` first (or `pip install .`): the Python API reference reads the module's stubs. The C++ API reference runs Doxygen on the public headers (mkdoxy), so `doxygen` must be installed.
