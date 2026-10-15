"""Runs a Python example by name, or lists them, as `terraingen-examples` does for C++.

    python examples/python                       # list the examples
    python examples/python craters_simple [OPTIONS]
    python examples/python craters_simple --help # the example's options

With Ernie's Python package installed the example opens in its viewer, with an Examples panel to
switch to the others; `--viewer null` runs it without one.
"""

import runpy
import sys

import _common


def main():
    examples = {path.stem.removeprefix("example_"): path for path in _common.discover_examples()}
    if len(sys.argv) < 2 or sys.argv[1] in ("--list", "-h", "--help"):
        print(__doc__.strip() if len(sys.argv) > 1 and sys.argv[1] != "--list" else "")
        category = None
        for name, path in examples.items():
            if path.parent.name != category:
                category = path.parent.name
                print(f"{category}:")
            print(f"  {name}")
        return 0
    name = sys.argv[1].removeprefix("example_")
    if name not in examples:
        print(f"No example named {sys.argv[1]}; run without arguments to list them", file=sys.stderr)
        return 2
    sys.argv = [str(examples[name]), *sys.argv[2:]]
    runpy.run_path(str(examples[name]), run_name="__main__")
    return 0


if __name__ == "__main__":
    sys.exit(main())
