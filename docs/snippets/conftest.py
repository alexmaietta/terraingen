# SPDX-License-Identifier: BSD-3-Clause

"""The fixtures Python docs snippets share, as snippet_test.h's, through the public API only."""

import pytest


@pytest.fixture
def output_folder(tmp_path):
    """A fresh folder for files a snippet writes."""
    return tmp_path


@pytest.fixture
def in_output_folder(tmp_path, monkeypatch):
    """A fresh folder made the working directory, for snippets that write files by bare name."""
    monkeypatch.chdir(tmp_path)
    return tmp_path
