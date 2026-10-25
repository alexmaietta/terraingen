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
    if gallery.downloads_needed(dict(gallery.figures())[name]):
        pytest.skip("its data files are not downloaded (docs/gallery/assets.py)")
    written = gallery.main([name, "--quick", "--offline", "--output-dir", str(tmp_path)])
    assert written == [tmp_path / f"{name}.jpg"]
    assert written[0].read_bytes().startswith(b"\xff\xd8\xff"), "the figure is a JPEG"


def test_every_figure_renders_without_names(tmp_path):
    names = [name for name, path in gallery.figures() if not gallery.downloads_needed(path)]
    assert gallery.main(["--quick", "--offline", "--output-dir", str(tmp_path)]) == [
        tmp_path / f"{name}.jpg" for name in names
    ]


def test_offline_skips_figures_whose_data_is_not_downloaded(tmp_path, monkeypatch):
    monkeypatch.setenv("TERRAINGEN_ASSET_DIR", str(tmp_path / "empty"))
    assert gallery.main(["dem/upsampling", "--quick", "--offline", "--output-dir", str(tmp_path)]) == []


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
def test_category_page_shows_figure(gallery_page, name):
    category, stem = name.split("/")
    page = gallery_page.markdown(category, gallery_page.categories()[category])
    assert f"images/gallery/{name}.jpg" in page, f"the {category} page shows {stem}"
    assert f'data-gallery="{category}"' in page, f"{stem} opens in the {category} lightbox"


def test_figures_have_captions(gallery_page):
    for figure in gallery_page.figures():
        assert figure["caption"], f"{figure['name']} has a caption"


def write_figure(directory, name, body):
    (directory / "craters").mkdir(exist_ok=True)
    (directory / "craters" / f"{name}.py").write_text(f'"""Figure {name}."""\n\n{body}')


def test_title_order_and_width(gallery_page, tmp_path, monkeypatch):
    write_figure(tmp_path, "a_first", "ORDER = 2\n")
    write_figure(tmp_path, "b_second", 'ORDER = 1\nTITLE = "Second"\nWIDE = True\n')
    monkeypatch.setattr(gallery_page, "GALLERY", tmp_path)
    figures = gallery_page.figures()
    assert [figure["name"] for figure in figures] == ["b_second", "a_first"], "ORDER sorts a category"
    assert [figure["title"] for figure in figures] == ["Second", "A first"], "TITLE, else the name"
    page = gallery_page.markdown("craters", figures)
    assert page.count('class="tg-gallery__wide"') == 1, "only the WIDE figure spans its row"


def test_categories_join_the_nav_and_home_page(gallery_page, tmp_path, monkeypatch):
    write_figure(tmp_path, "one", "")
    monkeypatch.setattr(gallery_page, "GALLERY", tmp_path)

    class Config:
        nav = [{"Home": "index.md"}, {"Gallery": "gallery/index.md"}]

    config = gallery_page.on_config(Config())
    assert config.nav[1] == {"Gallery": ["gallery/index.md", {"Craters": "gallery/craters.md"}]}

    class Page:
        class file:
            src_uri = "gallery/index.md"

    home = gallery_page.on_page_markdown("# Gallery\n", Page(), config, None)
    assert home.endswith("## Figures\n\n- [Craters](craters.md)\n"), "the home page lists the categories"


def test_figure_without_caption_is_an_error(gallery_page, tmp_path, monkeypatch):
    (tmp_path / "craters").mkdir()
    (tmp_path / "craters" / "bare.py").write_text("ORDER = 1\n")
    monkeypatch.setattr(gallery_page, "GALLERY", tmp_path)
    with pytest.raises(ValueError, match="docstring"):
        gallery_page.figures()
