# SPDX-License-Identifier: BSD-3-Clause

"""
MkDocs hook that writes the Guides section's task pages from docs/snippets: a page per category,
after the hand-written guides, a section per task file, a tab per language. A task whose Python
file defines `figure(quick)` shows what it makes beneath its code, the image docs/gallery/gallery.py
renders to images/snippets, captioned by the function's docstring. `{{ tasks() }}` on a page lists
them all.
"""

from __future__ import annotations

import ast
import re
from pathlib import Path

from mkdocs.exceptions import PluginError
from mkdocs.structure.files import File

ROOT = Path(__file__).resolve().parents[2]
SNIPPETS = ROOT / "docs" / "snippets"
TASKS = re.compile(r"\{\{\s*tasks\(\)\s*\}\}")

# The categories in navigation order.
CATEGORIES = {
    "configuration": "Configuration",
    "craters": "Craters",
    "boulders": "Boulders",
    "albedo": "Albedo",
    "customization": "Customization",
}

# Each category's tasks from first steps to advanced use, by file name; tasks not listed follow by name.
ORDER = {
    "craters": [
        "add-craters-to-a-terrain",
        "list-craters-of-a-layer",
        "list-craters-from-a-config",
        "place-craters-by-hand",
        "use-published-crater-models",
        "choose-a-crater-profile",
        "degrade-craters-with-age",
        "space-craters-apart",
        "vary-crater-density-with-a-map",
        "roughen-and-stretch-crater-rims",
        "raise-central-peaks",
        "terrace-crater-walls",
        "throw-secondary-craters-in-rays",
        "skip-craters-a-dem-resolves",
        "replace-a-blurred-crater",
        "restamp-craters-from-a-coarser-run",
    ],
    "boulders": [
        "list-boulders-of-a-layer",
        "list-boulders-from-a-config",
        "place-boulders-by-hand",
        "choose-boulder-sizes",
        "gather-boulders-in-patches-and-clusters",
        "throw-blocks-around-craters",
        "pose-boulders",
        "draw-boulders-as-relief-or-models",
        "write-boulder-models",
        "weather-boulders-with-fillets-and-ventifacts",
        "break-boulders-as-they-wear",
        "generate-a-boulder-shape",
        "split-a-boulder-shape-into-pieces",
        "design-a-boulder-style",
    ],
    "albedo": [
        "evaluate-a-synthetic-albedo",
        "add-noise-to-a-synthetic-albedo",
        "evaluate-a-configured-albedo",
        "upscale-an-albedo-map",
        "fade-crater-rays-with-age",
        "brighten-steep-slopes",
        "brighten-the-albedo-with-rocks",
        "scatter-dark-and-bright-splotches",
        "add-reflectance-zones-around-a-new-crater",
        "brighten-a-lander-blast-zone",
        "mix-hapke-photometry-from-materials",
    ],
    "customization": [
        "stack-noise-fractals",
        "write-a-source",
        "write-a-layer",
        "write-an-upsampler",
        "write-a-size-distribution",
        "write-a-crater-age-model",
        "write-a-crater-profile",
        "write-a-degradation-model",
        "write-a-surface-field",
        "write-an-albedo-source",
        "write-an-albedo-layer",
        "use-rock-meshes-for-boulders",
    ],
}

# Each language's tab title, highlighting and line comment, in tab order.
LANGUAGES = {".py": ("Python", "py", "#"), ".cpp": ("C++", "cpp", "//")}

_STATE = {}


class Task:
    """One task's files: its title and overview from the C++ file's header, and its regions."""

    def __init__(self, category, stem, paths):
        self.category = category
        self.stem = stem
        self.paths = sorted(paths, key=lambda p: list(LANGUAGES).index(p.suffix))
        described = next((p for p in self.paths if p.suffix == ".cpp"), self.paths[0])
        self.title, self.overview = header(described)
        self.regions = {path: regions(path) for path in self.paths}
        names = {tuple(found) for found in self.regions.values()}
        if len(names) != 1 or not next(iter(names)):
            raise PluginError(f"{category}/{stem}: each language needs the same marked regions")
        self.caption = next((figure_caption(p) for p in self.paths if p.suffix == ".py"), None)

    def markdown(self):
        lines = [f"## {self.title}", "", self.overview, ""]
        for name in self.regions[self.paths[0]]:
            for path in self.paths:
                tab, language, _ = LANGUAGES[path.suffix]
                source = path.relative_to(ROOT).as_posix()
                lines += [
                    f'=== "{tab}"',
                    "",
                    f"    ```{language}",
                    f'    --8<-- "{source}:{name}"',
                    "    ```",
                    "",
                ]
        if self.caption is not None:
            image = f"../images/snippets/{self.category}/{self.stem}.jpg"
            lines += [
                "<figure markdown>",
                "",
                f"![{self.caption or self.title}]({image})",
                "",
                f'<figcaption markdown="span">{self.caption}</figcaption>',
                "",
                "</figure>",
                "",
            ]
        return "\n".join(lines)


def figure_caption(path):
    """The docstring of the Python file's `figure` function ("" without one), or None if it has no
    `figure`."""
    for node in ast.parse(path.read_text()).body:
        if isinstance(node, ast.FunctionDef) and node.name == "figure":
            return " ".join((ast.get_docstring(node) or "").split())
    return None


def header(path):
    """The title and overview of a C++ file's header comment (its @brief, then its paragraphs), or
    a Python file's docstring, its title alone."""
    if path.suffix == ".py":
        match = re.search(r'^"""(.+?)\.?"""', path.read_text(), re.MULTILINE)
        if not match:
            raise PluginError(f"{path.relative_to(ROOT)}: the docstring needs a title")
        return match.group(1), ""
    text = []
    for line in path.read_text().splitlines():
        if line.startswith("///"):
            text.append(line[3:].strip())
        elif text:
            break
    body = [line for line in text if line != "@file"]
    if not body or not body[0].startswith("@brief "):
        raise PluginError(f"{path.relative_to(ROOT)}: the header needs an @brief title")
    title = body[0][len("@brief ") :]
    paragraphs = " ".join(line if line else "\n\n" for line in body[1:]).replace(" \n\n ", "\n\n")
    return title, paragraphs.strip()


def regions(path):
    """The names of the file's --8<-- regions, in order."""
    return re.findall(r"--8<-- \[start:(\w+)\]", path.read_text())


def scan():
    """Every category's tasks, in navigation order."""
    found = {}
    for path in sorted(SNIPPETS.glob("*/*")):
        category = path.parent.name
        # Folders starting with _ hold code pages include whole, not tasks.
        if path.suffix not in LANGUAGES or category.startswith("_"):
            continue
        if category not in CATEGORIES:
            raise PluginError(f"{path.relative_to(ROOT)}: no category {category} in guides.py")
        found.setdefault(category, {}).setdefault(path.stem, []).append(path)
    for category, stems in ORDER.items():
        missing = [stem for stem in stems if stem not in found.get(category, {})]
        if missing:
            raise PluginError(f"guides.py: no {category} task {', '.join(missing)} in docs/snippets")
    return {
        category: [Task(category, stem, found[category][stem]) for stem in ordered(category, found[category])]
        for category in CATEGORIES
        if category in found
    }


def ordered(category, stems):
    """A category's task stems in ORDER, then those it leaves out by name."""
    order = ORDER.get(category, [])
    return sorted(stems, key=lambda stem: (0, order.index(stem), "") if stem in order else (1, 0, stem))


def slug(title):
    """The anchor the toc extension gives a heading."""
    return re.sub(r"[\s-]+", "-", re.sub(r"[^\w\s-]", "", title.lower())).strip("-")


def task_list(page):
    """Every task, by category, linked from page."""
    here = Path(page.file.src_uri).parent
    lines = []
    for category, tasks in _STATE["tasks"].items():
        target = Path("guides") / f"{category}.md"
        link = Path(*[".."] * len(here.parts), target).as_posix() if here.parts else target
        lines += [f"### {CATEGORIES[category]}", ""]
        lines += [f"- [{task.title}]({link}#{slug(task.title)})" for task in tasks]
        lines.append("")
    return "\n".join(lines)


def on_config(config):
    tasks = scan()
    _STATE["tasks"] = tasks
    section = next(
        (item["Guides"] for item in config.nav if isinstance(item, dict) and "Guides" in item), None
    )
    if section is None:
        raise PluginError("guides.py: the nav needs a Guides section for the task pages")
    section += [{CATEGORIES[c]: f"guides/{c}.md"} for c in tasks]
    return config


def on_files(files, config):
    for category, tasks in _STATE["tasks"].items():
        text = "\n".join([f"# {CATEGORIES[category]}", ""] + [task.markdown() for task in tasks])
        files.append(File.generated(config, f"guides/{category}.md", content=text))
    return files


def on_page_markdown(markdown, page, config, files):
    return TASKS.sub(lambda _: task_list(page), markdown)


def on_serve(server, config, builder):
    server.watch(str(SNIPPETS))
    return server
