# TerrainGen documentation

Sphinx sources for the TerrainGen docs. Pages are Markdown (MyST); the API reference is generated
from Python docstrings (autodoc) and C++ `///` comments (Doxygen + Breathe).

```sh
pip install -r docs/requirements.txt
pip install -e .            # or a CMake build in build/; autodoc imports the module
sudo apt install doxygen    # for the C++ reference
make -C docs build          # output in docs/_build/html
make -C docs serve          # serve at http://127.0.0.1:8000 with live reload
```
