# SPDX-License-Identifier: BSD-3-Clause

"""The package ships type stubs for its extension module, and they cover the whole API."""

import ast
from pathlib import Path

import terraingen
from terraingen import _terraingen


def stub_names(tree):
    """Top-level names a stub defines: functions, classes and assigned constants."""
    names = set()
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.ClassDef)):
            names.add(node.name)
        elif isinstance(node, ast.Assign):
            names.update(t.id for t in node.targets if isinstance(t, ast.Name))
        elif isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name):
            names.add(node.target.id)
    return names


def test_the_package_is_typed_and_its_stubs_parse():
    package = Path(terraingen.__file__).parent
    assert (package / "py.typed").exists()
    stub = package / "_terraingen.pyi"
    assert stub.exists()
    tree = ast.parse(stub.read_text(encoding="utf-8"))
    assert "config_schema" in stub_names(tree)


def test_the_stubs_cover_every_name_the_extension_exports():
    package = Path(terraingen.__file__).parent
    names = stub_names(ast.parse((package / "_terraingen.pyi").read_text(encoding="utf-8")))
    from_extension = [
        name for name in terraingen.__all__ if hasattr(_terraingen, name) and not name.startswith("__")
    ]
    missing = sorted(name for name in from_extension if name not in names)
    assert not missing, "Not in _terraingen.pyi: " + ", ".join(missing)
