# SPDX-License-Identifier: BSD-3-Clause

"""The documentation's Python snippets (docs/snippets/<category>/*.py): each one runs."""

import pathlib
import runpy

import pytest

SNIPPETS = pathlib.Path(__file__).resolve().parents[2] / "docs" / "snippets"


@pytest.mark.parametrize("path", sorted(SNIPPETS.glob("*/*.py")), ids=lambda p: f"{p.parent.name}/{p.name}")
def test_python_snippet_runs(path, tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    runpy.run_path(str(path), run_name="__main__")
