# SPDX-License-Identifier: BSD-3-Clause

"""
MkDocs hook that writes a Gallery page per category of the figure scripts in docs/gallery, lists them
under the Gallery tab and at the foot of the gallery's home page: a grid of figures, each captioned by
its script's docstring and opening in a lightbox. A script may also set `TITLE` (else its name),
`ORDER` (its place in its category, else by name) and `WIDE` (true to span the row).
"""

from __future__ import annotations

import ast
from pathlib import Path

from mkdocs.structure.files import File

GALLERY = Path(__file__).resolve().parents[1] / "gallery"

INTRO = "Figures made by the scripts in `docs/gallery/{category}`. Click one to enlarge it."


def constants(module):
    """The module-level constants a script assigns, by name."""
    found = {}
    for node in module.body:
        if isinstance(node, ast.Assign) and isinstance(node.value, ast.Constant):
            for target in node.targets:
                if isinstance(target, ast.Name):
                    found[target.id] = node.value.value
    return found


def figures():
    """Every figure as a dict of its category, name, title, caption and width, in gallery order."""
    found = []
    for path in sorted(GALLERY.glob("*/*.py")):
        module = ast.parse(path.read_text())
        docstring = ast.get_docstring(module)
        values = constants(module)
        if not docstring:
            raise ValueError(f"{path} needs a docstring, its caption")
        found.append(
            {
                "category": path.parent.name,
                "name": path.stem,
                "title": values.get("TITLE", path.stem.replace("_", " ").capitalize()),
                "caption": " ".join(docstring.split()),
                "wide": bool(values.get("WIDE", False)),
                "order": values.get("ORDER", 0),
            }
        )
    return sorted(found, key=lambda figure: (figure["category"], figure["order"], figure["name"]))


def categories():
    """The figures of each category, by category name, in gallery order."""
    grouped = {}
    for figure in figures():
        grouped.setdefault(figure["category"], []).append(figure)
    return grouped


#: Category titles that the folder name does not spell, such as acronyms.
TITLES = {"dem": "DEMs"}


def title_text(category):
    return TITLES.get(category, category.replace("_", " ").capitalize())


def markdown(category, figures_in_category):
    """The Gallery page of one category."""
    lines = [
        f"# {title_text(category)}",
        "",
        INTRO.format(category=category),
        "",
        '<div class="tg-gallery" markdown>',
        "",
    ]
    for figure in figures_in_category:
        title, caption = figure["title"], figure["caption"]
        description = caption.replace('"', "'")
        lines += [
            '<figure class="tg-gallery__wide" markdown>' if figure["wide"] else "<figure markdown>",
            "",
            f"![{caption}](../images/gallery/{category}/{figure['name']}.jpg)"
            f'{{ data-gallery="{category}" data-title="{title}" data-description="{description}" }}',
            "",
            f'<figcaption markdown="span">**{title}.** {caption}</figcaption>',
            "",
            "</figure>",
            "",
        ]
    return "\n".join([*lines, "</div>", ""])


def on_config(config):
    pages = [{title_text(category): f"gallery/{category}.md"} for category in categories()]
    for item in config.nav:
        if isinstance(item, dict) and "Gallery" in item:
            home = item["Gallery"] if isinstance(item["Gallery"], str) else item["Gallery"][0]
            item["Gallery"] = [home, *pages]
    return config


def on_files(files, config):
    for category, figures_in_category in categories().items():
        files.append(
            File.generated(config, f"gallery/{category}.md", content=markdown(category, figures_in_category))
        )
    return files


def on_page_markdown(markdown_text, page, config, files):
    if page.file.src_uri != "gallery/index.md":
        return markdown_text
    links = [f"- [{title_text(category)}]({category}.md)" for category in categories()]
    return "\n".join([markdown_text.rstrip(), "", "## Figures", "", *links, ""])
