# SPDX-License-Identifier: BSD-3-Clause

"""The Guides section's task pages (docs/scripts/guides.py), written from docs/snippets."""

import pathlib
import sys

import pytest

pytest.importorskip("mkdocs")

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "docs" / "scripts"))
import guides  # noqa: E402

CPP = """// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Do a Thing
///
/// What doing it means,
/// over two lines.
///
/// A second paragraph.
#include "snippet_test.h"

// --8<-- [start:do_a_thing]
// --8<-- [end:do_a_thing]
"""

PYTHON = '''# SPDX-License-Identifier: BSD-3-Clause
"""Do a Thing."""

# --8<-- [start:do_a_thing]
# --8<-- [end:do_a_thing]
'''

FIGURE = '''

def figure(quick=False):
    """What it makes."""
'''


@pytest.fixture
def snippets(tmp_path, monkeypatch):
    (tmp_path / "craters").mkdir()
    monkeypatch.setattr(guides, "SNIPPETS", tmp_path)
    monkeypatch.setattr(guides, "ROOT", tmp_path)
    monkeypatch.setattr(guides, "ORDER", {})
    return tmp_path


def write_task(folder, python=PYTHON, cpp=CPP, stem="do-a-thing"):
    (folder / "craters" / f"{stem}.py").write_text(python)
    (folder / "craters" / f"{stem}.cpp").write_text(cpp)


def test_a_task_has_its_title_overview_and_a_tab_per_language(snippets):
    write_task(snippets)
    (task,) = guides.scan()["craters"]
    page = task.markdown()
    assert page.startswith("## Do a Thing\n\nWhat doing it means, over two lines.\n\nA second paragraph.\n")
    assert page.index('=== "Python"') < page.index('=== "C++"'), "Python first"
    assert '--8<-- "craters/do-a-thing.cpp:do_a_thing"' in page
    assert "<figure" not in page, "no figure without figure()"


def test_a_task_that_draws_a_figure_shows_it_captioned(snippets):
    write_task(snippets, python=PYTHON + FIGURE)
    (task,) = guides.scan()["craters"]
    page = task.markdown()
    assert "![What it makes.](../images/snippets/craters/do-a-thing.jpg)" in page
    assert '<figcaption markdown="span">What it makes.</figcaption>' in page
    assert page.index('=== "C++"') < page.index("<figure"), "the figure follows the code"


def test_languages_with_different_regions_are_an_error(snippets):
    write_task(snippets, python=PYTHON.replace("do_a_thing", "other"))
    with pytest.raises(guides.PluginError, match="same marked regions"):
        guides.scan()


def test_a_folder_without_a_category_is_an_error(snippets):
    (snippets / "unknown").mkdir()
    (snippets / "unknown" / "task.py").write_text(PYTHON)
    with pytest.raises(guides.PluginError, match="no category unknown"):
        guides.scan()


def test_tasks_follow_their_order_then_the_rest_by_name(snippets, monkeypatch):
    for stem in ["a", "b", "c", "d"]:
        write_task(snippets, stem=stem)
    monkeypatch.setattr(guides, "ORDER", {"craters": ["c", "a"]})
    assert [task.stem for task in guides.scan()["craters"]] == ["c", "a", "b", "d"]


def test_an_ordered_task_without_files_is_an_error(snippets, monkeypatch):
    write_task(snippets)
    monkeypatch.setattr(guides, "ORDER", {"craters": ["do-a-thing", "gone"]})
    with pytest.raises(guides.PluginError, match="no craters task gone"):
        guides.scan()


def test_every_snippet_folder_has_a_category():
    assert set(guides.scan()) == {
        p.name for p in guides.SNIPPETS.iterdir() if p.is_dir() and p.name[0] not in "_."
    }
