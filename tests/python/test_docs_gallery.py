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
