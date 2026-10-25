# SPDX-License-Identifier: BSD-3-Clause

"""
MkDocs hook that writes the Gallery page from the figure scripts in docs/gallery: each figure under its
category, captioned by its script's docstring and linked to the page its `PAGE` names.
"""

from __future__ import annotations

import ast
from pathlib import Path

from mkdocs.structure.files import File

GALLERY = Path(__file__).resolve().parents[1] / "gallery"

INTRO = "Every figure in the documentation, made by the scripts in `docs/gallery`. Each one links to the page that explains it."


def figures():
    """Every figure as (category, name, caption, page), in name order within each category."""
    found = []
    for path in sorted(GALLERY.glob("*/*.py")):
        module = ast.parse(path.read_text())
        docstring = ast.get_docstring(module)
        pages = [
            node.value.value
            for node in module.body
            if isinstance(node, ast.Assign)
            and any(isinstance(target, ast.Name) and target.id == "PAGE" for target in node.targets)
        ]
        if not docstring or not pages:
            raise ValueError(f"{path} needs a docstring, its caption, and a PAGE, the page that explains it")
        found.append((path.parent.name, path.stem, " ".join(docstring.split()), pages[0]))
    return found


def markdown():
    """The Gallery page."""
    lines = ["# Gallery", "", INTRO, ""]
    category = None
    for figure_category, name, caption, page in figures():
        if figure_category != category:
            category = figure_category
            lines += [f"## {category.replace('_', ' ').capitalize()}", ""]
        lines += [
            "<figure markdown>",
            "",
            f"![{caption}](../images/gallery/{category}/{name}.jpg)",
            "",
            f'<figcaption markdown="span">{caption} [Learn more](../{page})</figcaption>',
            "",
            "</figure>",
            "",
            '??? example "Reproduce this figure"',
            "",
            "    ```python",
            f'    --8<-- "docs/gallery/{category}/{name}.py"',
            "    ```",
            "",
        ]
    return "\n".join(lines)


def on_files(files, config):
    files.append(File.generated(config, "gallery/index.md", content=markdown()))
    return files
