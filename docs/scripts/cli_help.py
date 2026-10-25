# SPDX-License-Identifier: BSD-3-Clause

"""
MkDocs hook that writes the `terraingen` command's help, as `terraingen --help` prints it, wherever a
page says `{{ cli_help() }}`, so the command line reference never falls out of step with the
command. It runs the command built in build/ (cmake -S . -B build && cmake --build build), or else
the one on the PATH.
"""

from __future__ import annotations

import re
import shutil
import subprocess
from pathlib import Path

from mkdocs.exceptions import PluginError

ROOT = Path(__file__).resolve().parents[2]
BUILT = ROOT / "build" / "bin" / "terraingen"
PLACEHOLDER = re.compile(r"\{\{\s*cli_help\(\)\s*\}\}")


def command():
    """The `terraingen` command to run: the one built in build/, else the one on the PATH."""
    if BUILT.exists():
        return str(BUILT)
    found = shutil.which("terraingen")
    if found is None:
        raise PluginError(f"cli_help.py: no {BUILT.relative_to(ROOT)} and no terraingen on the PATH")
    return found


def help_text(program):
    """What `program --help` prints."""
    return subprocess.run([program, "--help"], check=True, capture_output=True, text=True).stdout


def on_page_markdown(markdown, page, config, files):
    if not PLACEHOLDER.search(markdown):
        return markdown
    block = "```text\n" + help_text(command()).rstrip() + "\n```"
    return PLACEHOLDER.sub(lambda _: block, markdown)


def on_serve(server, config, builder):
    server.watch(str(BUILT))
    return server
