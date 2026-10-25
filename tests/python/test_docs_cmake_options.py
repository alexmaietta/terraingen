# SPDX-License-Identifier: BSD-3-Clause

"""The CMake options table (docs/scripts/cmake_options.py), read from CMakeLists.txt."""

import pathlib
import sys

import pytest

pytest.importorskip("mkdocs")

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "docs" / "scripts"))
import cmake_options  # noqa: E402


def test_options_give_name_description_and_default():
    text = """
option(A_ONE "Build one" ON)
option(A_TWO "Build two, over
       two lines" ${PROJECT_IS_TOP_LEVEL})
"""
    assert cmake_options.options(text) == [
        ("A_ONE", "Build one", "`ON`"),
        ("A_TWO", "Build two, over two lines", "`ON` at the top level"),
    ]


def test_the_placeholder_becomes_the_table_of_every_option():
    page = cmake_options.on_page_markdown("Options:\n\n{{ cmake_options() }}\n", None, None, None)
    names = [name for name, _, _ in cmake_options.options(cmake_options.CMAKE_LISTS.read_text())]
    assert "TERRAINGEN_BUILD_PYTHON" in names
    assert page.startswith("Options:\n\n| Option | Default | Description |\n")
    assert all(f"| `{name}` |" in page for name in names)


def test_pages_without_the_placeholder_are_left_alone():
    assert cmake_options.on_page_markdown("# Page\n", None, None, None) == "# Page\n"
