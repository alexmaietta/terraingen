# SPDX-License-Identifier: BSD-3-Clause

"""
MkDocs hook that writes the configuration reference from the library's own schema.
"""

from __future__ import annotations

import json
import sys
from collections import defaultdict
from pathlib import Path

from mkdocs.structure.files import File

ROOT = Path(__file__).resolve().parents[2]
REPO_BLOB = "https://github.com/alexmaietta/terraingen/blob/main/"

# Titles and slugs of the choices, keyed by where they are first reached from the root.
CHOICES = {
    "body": ("Bodies", "body"),
    "source": ("Sources", "source"),
    "source[raster].upsampling": ("Upsampling", "models/upsampling"),
    "layers": ("Layers", "layers"),
    "layers[craters].density": ("Crater densities", "models/densities"),
    "layers[craters].ages": ("Crater ages", "models/ages"),
    "layers[craters].profile": ("Crater profiles", "models/profiles"),
    "layers[craters].degradation": ("Crater degradation", "models/degradation"),
    "layers[boulders].sizes": ("Boulder sizes", "models/boulder-sizes"),
    "layers[masked].shape": ("Mask shapes", "models/mask-shapes"),
    "outputs": ("Outputs", "outputs"),
    "albedo.source": ("Albedo sources", "albedo-sources"),
    "albedo.layers": ("Albedo layers", "albedo-layers"),
}

# The order of the reference's sections in the navigation.
SECTIONS = [
    ("Bodies", ["body"]),
    ("Sources", ["source"]),
    ("Layers", ["layers"]),
    (
        "Crater and boulder models",
        [
            "source[raster].upsampling",
            "layers[craters].density",
            "layers[craters].ages",
            "layers[craters].profile",
            "layers[craters].degradation",
            "layers[boulders].sizes",
            "layers[masked].shape",
        ],
    ),
    ("Albedo", ["albedo.source", "albedo.layers"]),
    ("Outputs", ["outputs"]),
]


def load_schema():
    """The schema and catalog from the built module."""
    sys.path.insert(0, str(ROOT / "build" / "python"))
    try:
        import terraingen
    except ImportError as error:
        raise RuntimeError(
            "The configuration reference needs TerrainGen built with Python in build/ "
            "(cmake -S . -B build && cmake --build build)"
        ) from error
    return terraingen.config_schema(), terraingen.config_catalog()


def unwrap(schema):
    """The schema an optional or a list holds, and whether it was either."""
    wrapped = []
    while schema.get("kind") in ("optional", "array"):
        wrapped.append(schema["kind"])
        schema = schema["of"]
    return schema, wrapped


def compact(value, limit=40):
    """A value as short JSON for a table cell."""
    if isinstance(value, dict):
        return f'`{{"type": "{value["type"]}", ...}}`' if "type" in value else "`{...}`"
    if isinstance(value, list) and value:
        return "`[...]`"
    if isinstance(value, float) and value == int(value) and abs(value) < 1e15:
        text = f"{value:.1f}"
    else:
        text = json.dumps(value)
    if len(text) > limit:
        text = text[: limit - 3] + "..."
    return f"`{text}`"


def cell(text):
    """Text safe in a Markdown table cell."""
    return text.replace("|", "\\|").replace("\n", " ")


def number(value):
    return f"{value:g}"


def range_text(field):
    """A field's range in interval notation, or empty."""
    low = field.get("minimum")
    high = field.get("maximum")
    if low is None and high is None:
        return ""
    left = "(" if low is None or field.get("exclusive_minimum") else "["
    right = ")" if high is None or field.get("exclusive_maximum") else "]"
    return f"{left}{'-inf' if low is None else number(low)}, {'inf' if high is None else number(high)}{right}"


class Reference:
    """The pages of the configuration reference, built from one schema."""

    def __init__(self, schema, catalog, examples):
        self.schema = schema
        self.catalog = catalog
        self.definitions = schema["definitions"]
        self.paths = {}  # choice id -> path first reached from the root
        self.names = defaultdict(set)  # choice id -> the field names that refer to it
        self.enums = {}  # tuple of choices -> (anchor, name, descriptions)
        self.examples = examples
        self.walk(schema["root"], "")

    # Discovery ---------------------------------------------------------------------------------

    def walk(self, obj, path):
        for field in obj.get("fields", []):
            self.visit(field["schema"], f"{path}.{field['name']}" if path else field["name"], field)

    def visit(self, schema, path, field):
        inner, _ = unwrap(schema)
        kind = inner.get("kind")
        if kind == "enum":
            key = tuple(inner["choices"])
            if key not in self.enums:
                name = field["name"]
                anchor = name.replace("_", "-")
                taken = {a for a, _, _ in self.enums.values()}
                suffix = 2
                while anchor in taken:
                    anchor = f"{name.replace('_', '-')}-{suffix}"
                    suffix += 1
                self.enums[key] = (anchor, name, inner.get("descriptions", []))
            return
        if kind != "ref":
            return
        ref = inner["ref"]
        target = self.definitions[ref]
        if target["kind"] == "choice":
            self.names[ref].add(field["name"])
            if ref in self.paths:
                return
            self.paths[ref] = path
            for alternative in target["alternatives"]:
                self.walk(alternative, f"{path}[{alternative['type']}]")
        else:
            self.walk(target, path)

    def page_of(self, ref):
        """The slug and title of a choice's page."""
        path = self.paths[ref]
        if path in CHOICES:
            title, slug = CHOICES[path]
            return slug, title
        if path.endswith("].simple"):
            return CHOICES["layers[craters].profile"][1], CHOICES["layers[craters].profile"][0]
        name = path.rsplit(".", 1)[-1]
        return f"models/{name.replace('_', '-')}", name.replace("_", " ").capitalize()

    def ref_of(self, path):
        return next(ref for ref, p in self.paths.items() if p == path)

    # Rendering ---------------------------------------------------------------------------------

    def link(self, here, slug, anchor=""):
        """A relative link from page `here` to page `slug`."""
        depth = here.count("/")
        target = "../" * depth + slug + ".md"
        return target + (f"#{anchor}" if anchor else "")

    def type_text(self, schema, here):
        inner, wrapped = unwrap(schema)
        kind = inner.get("kind")
        if kind == "ref":
            target = self.definitions[inner["ref"]]
            if target["kind"] == "choice":
                slug, title = self.page_of(inner["ref"])
                page = "layers/index" if slug == "layers" else slug
                text = f"[{title.lower()}]({self.link(here, page)})"
            else:
                text = "object, below"
        elif kind == "enum":
            anchor, _, _ = self.enums[tuple(inner["choices"])]
            values = ", ".join(f"`{c}`" for c in inner["choices"])
            text = f"[one of]({self.link(here, 'enums', anchor)}) {values}"
        elif kind == "integer" and inner.get("fixed"):
            text = "integer, fixed"
        else:
            text = kind
        for wrapper in reversed(wrapped):
            text = f"list of {text}" if wrapper == "array" else f"{text}, optional"
        return text

    def nested(self, schema):
        """The object definition a field holds, if any."""
        inner, wrapped = unwrap(schema)
        if inner.get("kind") == "ref":
            target = self.definitions[inner["ref"]]
            if target["kind"] == "object":
                return target, "array" in wrapped
        return None, False

    def fields(self, fields, here, indent=""):
        """The fields of an object: the plain ones in a table, then each group collapsed."""
        lines = []
        plain = [f for f in fields if self.nested(f["schema"])[0] is None]
        groups = [f for f in fields if self.nested(f["schema"])[0] is not None]
        if plain:
            lines += [
                f"{indent}| Key | Type | Default | Range | Unit | Description |",
                f"{indent}| --- | --- | --- | --- | --- | --- |",
            ]
            for f in plain:
                lines.append(
                    f"{indent}| `{f['name']}` | {self.type_text(f['schema'], here)} | "
                    f"{compact(f['default'])} | {range_text(f)} | {f['unit']} | "
                    f"{cell(f['description'])} |"
                )
            lines.append("")
        for f in groups:
            target, listed = self.nested(f["schema"])
            title = f"{f['name']}[]" if listed else f["name"]
            lines.append(f'{indent}??? note "`{title}`: {f["description"].replace(chr(34), chr(39))}"')
            lines.append("")
            lines += self.fields(target["fields"], here, indent + "    ")
        return lines

    def used_in(self, names, type_name):
        files = sorted({path for name in names for path in self.examples.get((name, type_name), [])})
        if not files:
            return []
        links = ", ".join(f"[{Path(p).stem}]({REPO_BLOB}{p})" for p in files[:8])
        more = f" and {len(files) - 8} more" if len(files) > 8 else ""
        return [f"**Used in:** {links}{more}", ""]

    def alternative(self, alternative, names, here, level="##"):
        lines = [f"{level} `{alternative['type']}` {{#{alternative['type']}}}", ""]
        lines += [alternative["description"], ""]
        lines += self.fields(alternative["fields"], here)
        lines += [
            '??? example "Every key with its default"',
            "",
            "    ```json",
            *("    " + line for line in json.dumps(alternative["default"], indent=4).splitlines()),
            "    ```",
            "",
        ]
        lines += self.used_in(names, alternative["type"])
        return lines

    def choice_page(self, ref):
        slug, title = self.page_of(ref)
        choice = self.definitions[ref]
        names = self.names[ref]
        keys = ", ".join(f"`{n}`" for n in sorted(names))
        lines = [f"# {title}", "", f'Chosen by `"type"` in {keys}.', ""]
        lines += ["| Type | What it is |", "| --- | --- |"]
        for alternative in choice["alternatives"]:
            first = alternative["description"].split(". ")[0].rstrip(".")
            lines.append(f"| [`{alternative['type']}`](#{alternative['type']}) | {cell(first)}. |")
        lines.append("")
        for alternative in choice["alternatives"]:
            lines += self.alternative(alternative, names, slug)
        return slug, title, "\n".join(lines)

    def layer_pages(self):
        ref = self.ref_of("layers")
        choice = self.definitions[ref]
        names = self.names[ref]
        index = ["# Layers", "", "Relief and features added to the source, applied in order.", ""]
        index += ["| Type | What it is |", "| --- | --- |"]
        pages = []
        for alternative in choice["alternatives"]:
            slug = f"layers/{alternative['type']}"
            first = alternative["description"].split(". ")[0].rstrip(".")
            index.append(f"| [`{alternative['type']}`]({alternative['type']}.md) | {cell(first)}. |")
            body = self.alternative(alternative, names, slug, level="#")
            pages.append((slug, alternative["type"], "\n".join(body)))
        return ("layers/index", "Layers", "\n".join(index + [""])), pages

    def overview(self):
        lines = [
            "# Configuration",
            "",
            "A configuration is a JSON file the `terraingen` command, `GenerateFromConfig` and",
            "`terraingen.load_config` read. This reference is generated from the same field lists",
            "the parser reads, so it always matches the library; the [raw schema](schema.md) is",
            "the same information as JSON, for editors and tools.",
            "",
            "Ranges are the values each field accepts on its own; validation also checks the rules",
            "that tie fields together.",
            "",
            "## Top level",
            "",
        ]
        lines += self.fields(self.schema["root"]["fields"], "index")
        lines += ["## Sections", ""]
        for _, paths in SECTIONS:
            for path in paths:
                title, slug = CHOICES[path]
                target = "layers/index.md" if slug == "layers" else f"{slug}.md"
                lines.append(f"- [{title}]({target})")
        lines += ["- [Enumerations](enums.md)", "- [Raw schema](schema.md)", ""]
        return "\n".join(lines)

    def enum_page(self):
        lines = ["# Enumerations", "", "The named values fields choose between.", ""]
        for choices, (anchor, name, descriptions) in sorted(self.enums.items(), key=lambda item: item[1][0]):
            lines += [f"## `{name}` {{#{anchor}}}", "", "| Value | Meaning |", "| --- | --- |"]
            for value, description in zip(choices, descriptions or [""] * len(choices), strict=True):
                lines.append(f"| `{value}` | {cell(description)} |")
            lines.append("")
        return "\n".join(lines)

    def schema_page(self):
        text = json.dumps(self.schema, indent=2)
        return "\n".join(
            [
                "# Raw schema",
                "",
                "The structure of a configuration as JSON, from the same field lists the parser",
                "reads: each object's fields with their descriptions, units, defaults and ranges,",
                "and each choice's alternatives. `terraingen.config_schema()` and",
                "`terraingen schema` give it too.",
                "",
                "- [schema.json](schema.json): this document.",
                "- [catalog.json](catalog.json): every type with its defaults, as",
                "  `terraingen.config_catalog()` gives it.",
                "",
                '??? abstract "schema.json"',
                "",
                "    ```json",
                *("    " + line for line in text.splitlines()),
                "    ```",
                "",
            ]
        )

    def pages(self):
        """Every page as (slug, title, markdown), in navigation order, and the navigation."""
        pages = [("index", "Overview", self.overview())]
        nav = [{"Overview": "reference/index.md"}]
        for section, paths in SECTIONS:
            entries = []
            for path in paths:
                if path == "layers":
                    index, layers = self.layer_pages()
                    pages.append(index)
                    pages += layers
                    entries.append("reference/layers/index.md")
                    entries += [{title: f"reference/{slug}.md"} for slug, title, _ in layers]
                    continue
                slug, title, text = self.choice_page(self.ref_of(path))
                pages.append((slug, title, text))
                entries.append({title: f"reference/{slug}.md"})
            nav.append({section: entries})
        pages.append(("enums", "Enumerations", self.enum_page()))
        pages.append(("schema", "Raw schema", self.schema_page()))
        nav += [{"Enumerations": "reference/enums.md"}, {"Raw schema": "reference/schema.md"}]
        return pages, nav


def scan_examples():
    """(key, type) -> the example configurations using that type under that key."""
    found = defaultdict(set)

    def walk(value, key, path):
        if isinstance(value, dict):
            if "type" in value and key:
                found[(key, value["type"])].add(path)
            for child_key, child in value.items():
                walk(child, child_key, path)
        elif isinstance(value, list):
            for child in value:
                walk(child, key, path)

    for file in sorted((ROOT / "examples" / "config").rglob("*.json")):
        walk(json.loads(file.read_text()), "", str(file.relative_to(ROOT)))
    return found


_STATE = {}


def on_config(config):
    schema, catalog = load_schema()
    reference = Reference(schema, catalog, scan_examples())
    pages, nav = reference.pages()
    _STATE["pages"] = pages
    _STATE["schema"] = schema
    _STATE["catalog"] = catalog
    entry = {"Configuration": nav}
    position = next(
        (i for i, item in enumerate(config.nav) if isinstance(item, dict) and "API" in item),
        len(config.nav),
    )
    config.nav.insert(position, entry)
    return config


def on_files(files, config):
    for slug, _, text in _STATE["pages"]:
        files.append(File.generated(config, f"reference/{slug}.md", content=text + "\n"))
    files.append(
        File.generated(config, "reference/schema.json", content=json.dumps(_STATE["schema"], indent=2))
    )
    files.append(
        File.generated(config, "reference/catalog.json", content=json.dumps(_STATE["catalog"], indent=2))
    )
    return files
