# TerrainGen's documentation

The pages and their images are under `_src/`; `overrides/` holds the theme templates and CSS, and
`scripts/` the MkDocs hooks.

Code on the pages comes from files that are tested, never written into the Markdown:

- `snippets/<category>/`: short Python and C++ files side by side, each marking the lines a page
  shows with `# --8<-- [start:name]` and `# --8<-- [end:name]` (`//` in C++). A page includes them
  with `--8<-- "docs/snippets/<category>/<file>:<name>"`. The Python files run in
  tests/python/test_docs_snippets.py; the C++ files build into the `docs_snippets` test suite.
- `gallery/<category>/<name>.py`: a figure each, a `figure(quick)` function returning a matplotlib
  figure. `make gallery` renders them into `_src/images/gallery/`, committed with the page that shows
  them; tests/python/test_docs_gallery.py and `make gallery-check` render them small to check that
  they still run.

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
make gallery
make gallery-check
make clean
```
