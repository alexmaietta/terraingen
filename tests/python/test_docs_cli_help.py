# SPDX-License-Identifier: BSD-3-Clause

"""The command line reference (docs/scripts/cli_help.py), from `terraingen --help`."""

import pathlib
import sys

import pytest

pytest.importorskip("mkdocs")

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "docs" / "scripts"))
import cli_help  # noqa: E402


@pytest.fixture
def program(tmp_path):
    """A stand-in `terraingen` that prints a usage message."""
    script = tmp_path / "terraingen"
    script.write_text("#!/bin/sh\nprintf 'Usage:\\n  terraingen CONFIG\\n'\n")
    script.chmod(0o755)
    return script


def test_the_placeholder_becomes_the_help_as_text(program, monkeypatch):
    monkeypatch.setattr(cli_help, "BUILT", program)
    page = cli_help.on_page_markdown("## Usage\n\n{{ cli_help() }}\n", None, None, None)
    assert page == "## Usage\n\n```text\nUsage:\n  terraingen CONFIG\n```\n"


def test_without_a_build_the_command_on_the_path_runs(program, tmp_path, monkeypatch):
    monkeypatch.setattr(cli_help, "BUILT", tmp_path / "missing")
    monkeypatch.setenv("PATH", str(program.parent))
    assert cli_help.command() == str(program)


def test_without_any_command_the_build_fails(tmp_path, monkeypatch):
    monkeypatch.setattr(cli_help, "ROOT", tmp_path)
    monkeypatch.setattr(cli_help, "BUILT", tmp_path / "build" / "bin" / "terraingen")
    monkeypatch.setenv("PATH", str(tmp_path))
    with pytest.raises(cli_help.PluginError, match="no build/bin/terraingen"):
        cli_help.command()


def test_pages_without_the_placeholder_are_left_alone():
    assert cli_help.on_page_markdown("# Page\n", None, None, None) == "# Page\n"
