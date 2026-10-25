# SPDX-License-Identifier: BSD-3-Clause

"""
MkDocs hook that writes a Gallery page per category of the figure scripts in docs/gallery, lists them
under the Gallery tab and at the foot of the gallery's home page: a grid of figures, each captioned by
its script's docstring, opening in a lightbox, and linking to a page of its own with the whole script. A script may also set `TITLE` (else its name),
`ORDER` (its place in its category, else by name), `WIDE` (true to span the row) and `SECTION` (the
heading it sits under, in the order `SECTIONS` gives). Within a section the single-width figures
come first, side by side, then the wide ones.
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
                "section": values.get("SECTION", ""),
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


#: The section headings of each category in page order; sections not listed follow by name.
SECTIONS = {
    "albedo": ["Background", "Impacts and landers"],
    "boulders": ["Placement", "Crater ejecta", "Models", "Pose"],
    "craters": [
        "Named craters",
        "Populations",
        "Density maps",
        "Spacing",
        "Profiles",
        "Rim shapes",
        "Degradation",
        "Slopes and datums",
        "Central peaks",
        "Terraces",
        "Textures",
    ],
    "distributions": ["Building distributions", "Published distributions"],
    "geology": ["Dunes and bedforms", "Bedrock and sand"],
    "relief": ["Noise types", "Fractal settings", "Upsampling"],
}


def title_text(category):
    return TITLES.get(category, category.replace("_", " ").capitalize())


def sections(category, figures_in_category):
    """The figures of a category as (heading, figures) in page order, single-width figures first."""
    grouped = {}
    for figure in figures_in_category:
        grouped.setdefault(figure["section"], []).append(figure)
    order = SECTIONS.get(category, [])

    def rank(heading):
        return (0, order.index(heading), "") if heading in order else (1, 0, heading)

    return [
        (heading, sorted(grouped[heading], key=lambda f: f["wide"])) for heading in sorted(grouped, key=rank)
    ]


def markdown(category, figures_in_category):
    """The Gallery page of one category."""
    lines = [f"# {title_text(category)}", "", INTRO.format(category=category), ""]
    for heading, figures_in_section in sections(category, figures_in_category):
        if heading:
            lines += [f"## {heading}", ""]
        lines += grid(category, figures_in_section)
    return "\n".join(lines)


def grid(category, figures_in_section):
    """The lines of one grid of figures."""
    lines = ['<div class="tg-gallery" markdown>', ""]
    for figure in figures_in_section:
        title, caption = figure["title"], figure["caption"]
        description = caption.replace('"', "'")
        classes = ' class="tg-gallery__wide"' if figure["wide"] else ""
        lines += [
            f'<figure id="{figure["name"]}"{classes} markdown>',
            "",
            f"![{caption}](../images/gallery/{category}/{figure['name']}.jpg)"
            f'{{ data-gallery="{category}" data-title="{title}" data-description="{description}" }}',
            "",
            f'<figcaption markdown="span">**{title}.** {caption}</figcaption>',
            "",
            f"[:material-code-tags: Code]({category}/{figure['name']}.md){{ .tg-gallery__code }}",
            "",
            "</figure>",
            "",
        ]
    return [*lines, "</div>", ""]


def code_page(category, figure):
    """The page of one figure's script: a link back to the figure in its gallery, the figure and its
    caption, then the whole script."""
    name = figure["name"]
    back = f"../{category}.md#{name}"
    return "\n".join(
        [
            f"[:material-arrow-left: {title_text(category)}]({back}){{ .tg-gallery__back }}",
            "",
            f"# {figure['title']}",
            "",
            f"![{figure['caption']}](../../images/gallery/{category}/{name}.jpg)",
            "",
            figure["caption"],
            "",
            f"Made by `docs/gallery/{category}/{name}.py`; back to [the figure in {title_text(category)}]({back}).",
            "",
            "```py",
            f'--8<-- "docs/gallery/{category}/{name}.py"',
            "```",
            "",
        ]
    )


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
        for figure in figures_in_category:
            path = f"gallery/{category}/{figure['name']}.md"
            files.append(File.generated(config, path, content=code_page(category, figure)))
    return files


def on_page_markdown(markdown_text, page, config, files):
    if page.file.src_uri != "gallery/index.md":
        return markdown_text
    links = [f"- [{title_text(category)}]({category}.md)" for category in categories()]
    return "\n".join([markdown_text.rstrip(), "", "## Figures", "", *links, ""])
