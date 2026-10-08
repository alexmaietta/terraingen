"""Sphinx configuration for the TerrainGen documentation."""

import os
import re
import shutil
import subprocess
import sys
from importlib.util import find_spec
from pathlib import Path

DOCS_DIR = Path(__file__).resolve().parent
ROOT_DIR = DOCS_DIR.parent

project = "TerrainGen"
author = "Alex Maietta"
copyright = "2026, Alex Maietta"
# Without an installed package, autodoc imports the module from a CMake build in build/.
if find_spec("terraingen") is None and (ROOT_DIR / "build" / "python" / "terraingen").is_dir():
    sys.path.insert(0, str(ROOT_DIR / "build" / "python"))

release = re.search(r"\d+\.\d+\.\d+", (ROOT_DIR / "VERSION").read_text()).group(0)
version = release

extensions = [
    "myst_parser",
    "sphinx.ext.autodoc",
    "sphinx.ext.napoleon",
    "sphinx.ext.viewcode",
    "breathe",
]

source_suffix = {".rst": "restructuredtext", ".md": "markdown"}
exclude_patterns = ["README.md", "_build", "_doxygen", "Thumbs.db", ".DS_Store", "api/python_subclassing.md"]

myst_enable_extensions = ["colon_fence", "deflist"]
myst_heading_anchors = 3

autodoc_default_options = {"members": True, "undoc-members": True}

html_theme = "sphinx_rtd_theme"
html_title = f"TerrainGen {release}"
html_static_path = ["_static"]


def _run_doxygen():
    """Generate Doxygen XML for Breathe, skipping when doxygen is not installed."""
    if shutil.which("doxygen") is None:
        print("doxygen not found; the C++ API reference will be empty")
        return
    (DOCS_DIR / "_doxygen").mkdir(exist_ok=True)
    subprocess.run(["doxygen", "Doxyfile"], cwd=DOCS_DIR, check=True)


if os.environ.get("TERRAINGEN_DOCS_SKIP_DOXYGEN") != "1":
    _run_doxygen()

breathe_projects = {"terraingen": str(DOCS_DIR / "_doxygen" / "xml")}
breathe_default_project = "terraingen"
breathe_default_members = ("members", "undoc-members")



def _check_python_reference(app):
    """Warn about public Python names the Python API page leaves out.

    The page lists names explicitly because autodoc's automodule skips nanobind functions, which
    are not routines to `inspect`.
    """
    import terraingen
    from sphinx.util import logging

    page = (DOCS_DIR / "api" / "python.md").read_text()
    missing = [name for name in terraingen.__all__ if f"terraingen.{name}" not in page]
    if missing:
        logging.getLogger(__name__).warning(
            "api/python.md does not document: %s", ", ".join(missing)
        )


def setup(app):
    app.connect("builder-inited", _check_python_reference)
