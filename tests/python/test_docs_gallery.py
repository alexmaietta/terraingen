# SPDX-License-Identifier: BSD-3-Clause

"""The documentation's gallery (docs/gallery): each figure renders, small, to a JPEG."""

import pathlib
import sys

import pytest

pytest.importorskip("matplotlib")

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "docs" / "gallery"))
import gallery  # noqa: E402


@pytest.mark.parametrize("name", [name for name, _ in gallery.figures()])
def test_figure_renders(name, tmp_path):
    written = gallery.main([name, "--quick", "--output-dir", str(tmp_path)])
    assert written == [tmp_path / f"{name}.jpg"]
    assert written[0].read_bytes().startswith(b"\xff\xd8\xff"), "the figure is a JPEG"


def test_every_figure_renders_without_names(tmp_path):
    names = [name for name, _ in gallery.figures()]
    assert gallery.main(["--quick", "--output-dir", str(tmp_path)]) == [
        tmp_path / f"{name}.jpg" for name in names
    ]


def test_unknown_figure_is_an_error(tmp_path):
    with pytest.raises(SystemExit):
        gallery.main(["craters/none", "--output-dir", str(tmp_path)])


@pytest.fixture
def gallery_page():
    pytest.importorskip("mkdocs")
    sys.path.insert(0, str(gallery.ROOT / "docs" / "scripts"))
    import gallery_page

    return gallery_page


@pytest.mark.parametrize("name", [name for name, _ in gallery.figures()])
def test_gallery_page_shows_figure(gallery_page, name):
    page = gallery_page.markdown()
    assert f"images/gallery/{name}.jpg" in page, f"the Gallery page shows {name}"
    assert f'"docs/gallery/{name}.py"' in page, f"the Gallery page reproduces {name}"


def test_gallery_page_links_existing_pages(gallery_page):
    for category, name, caption, page in gallery_page.figures():
        assert caption, f"{category}/{name} has a caption"
        assert (gallery.ROOT / "docs" / "_src" / page).is_file(), f"{category}/{name} links to {page}"


def test_figure_without_page_is_an_error(gallery_page, tmp_path, monkeypatch):
    (tmp_path / "craters").mkdir()
    (tmp_path / "craters" / "bare.py").write_text('"""A figure."""\n')
    monkeypatch.setattr(gallery_page, "GALLERY", tmp_path)
    with pytest.raises(ValueError, match="PAGE"):
        gallery_page.figures()
