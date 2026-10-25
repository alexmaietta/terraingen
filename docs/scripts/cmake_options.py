# SPDX-License-Identifier: BSD-3-Clause

"""
MkDocs hook that writes the table of TerrainGen's CMake options, read from the `option()` calls of
the top-level CMakeLists.txt, wherever a page says `{{ cmake_options() }}`: each option's name,
default and description, so the docs never fall out of step with the build.
"""

from __future__ import annotations

import re
from pathlib import Path

from mkdocs.exceptions import PluginError

CMAKE_LISTS = Path(__file__).resolve().parents[2] / "CMakeLists.txt"
PLACEHOLDER = re.compile(r"\{\{\s*cmake_options\(\)\s*\}\}")
OPTION = re.compile(r'option\(\s*(\w+)\s+"((?:[^"\\]|\\.)*)"\s+([^\s)]+)\s*\)')

#: How defaults that are CMake variables read in the docs.
DEFAULTS = {"${PROJECT_IS_TOP_LEVEL}": "`ON` at the top level"}


def options(text):
    """Each `option()` in CMake source `text` as (name, description, default), in order."""
    return [
        (name, " ".join(description.split()), DEFAULTS.get(default, f"`{default}`"))
        for name, description, default in OPTION.findall(text)
    ]


def table(found):
    """The Markdown table of `found` options."""
    rows = [f"| `{name}` | {default} | {description} |" for name, description, default in found]
    return "\n".join(["| Option | Default | Description |", "| --- | --- | --- |", *rows])


def on_page_markdown(markdown, page, config, files):
    if not PLACEHOLDER.search(markdown):
        return markdown
    found = options(CMAKE_LISTS.read_text())
    if not found:
        raise PluginError(f"cmake_options.py: no option() in {CMAKE_LISTS}")
    return PLACEHOLDER.sub(lambda _: table(found), markdown)


def on_serve(server, config, builder):
    server.watch(str(CMAKE_LISTS))
    return server
