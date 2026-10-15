"""The harness the Python examples run on (examples/python/_common.py) and their runner
(examples/python/__main__.py), without a viewer."""

import os
import pathlib
import subprocess
import sys

import numpy as np
import pytest

import terraingen

EXAMPLES = pathlib.Path(__file__).resolve().parents[2] / "examples" / "python"
sys.path.insert(0, str(EXAMPLES))
import _common  # noqa: E402


class Ramp(_common.Example):
    """A ramp, for the harness's tests.

    More about it.
    """

    def generate(self):
        self.heights = np.tile(np.arange(4, dtype=np.float32), (3, 1))
        self.show("ramp", self.heights, 2.0)

    def save(self, out):
        (out / "ramp.txt").write_text("ramp\n")

    def test_final(self):
        assert self.heights.shape == (3, 4)


class Untested(_common.Example):
    def generate(self):
        pass


def args_for(example_class, *argv):
    return example_class.create_parser().parse_args(["--viewer", "null", *argv])


def test_example_names_and_describes_itself():
    assert Ramp.name() == "test_examples_harness"
    assert Ramp.description() == "The harness the Python examples run on (examples/python/_common.py) and their runner"
    example_class = _common.load_example(EXAMPLES / "craters" / "example_craters_simple.py")
    assert example_class.name() == "craters_simple"
    assert example_class.description() == "Lunar craters on a synthetic terrain."


def test_show_keeps_the_grid_of_rasters_and_centres_arrays():
    example = Ramp(None, None)
    example.generate()
    name, heights, pixel_size, corner, albedo = example.views[0]
    assert (name, heights.shape, pixel_size, corner, albedo) == ("ramp", (3, 4), 2.0, (-4.0, 3.0), None)
    raster = terraingen.evaluate(terraingen.Terrain(terraingen.FlatSource(5, 5, 2.0), [])).heights
    example.show("flat", raster, albedo=raster.values)
    _, heights, pixel_size, corner, albedo = example.views[1]
    assert (heights.dtype, pixel_size, corner, albedo.shape) == (np.float32, 2.0, (-5.0, 5.0), (5, 5))


def test_run_without_a_viewer_saves_and_checks(tmp_path, capsys):
    args = args_for(Ramp, "--test", "--output-dir", str(tmp_path / "out"))
    example = _common.run(Ramp(None, args), args)
    assert (tmp_path / "out" / "ramp.txt").read_text() == "ramp\n"
    assert example.views[0][0] == "ramp"
    assert "example_test_examples_harness: checks passed" in capsys.readouterr().out

    args = args_for(Ramp, "--no-save", "--output-dir", str(tmp_path / "unsaved"))
    _common.run(Ramp(None, args), args)
    assert not (tmp_path / "unsaved").exists()


def test_run_needs_test_final_to_test(tmp_path):
    args = args_for(Untested, "--test", "--no-save")
    with pytest.raises(NotImplementedError):
        _common.run(Untested(None, args), args)
    with pytest.raises(NotImplementedError):
        _common.Example(None, args).generate()


def test_parser_defaults():
    args = Ramp.create_parser().parse_args([])
    assert args.viewer == ("window" if _common.ernie_available() else "null")
    assert (args.headless, args.test, args.output_dir, args.save) == (False, False, "output", None)


def test_discovers_examples_in_teaching_order():
    paths = _common.discover_examples()
    categories = [path.parent.name for path in paths]
    assert categories[0] == "terrain"
    assert categories == sorted(categories, key=_common.CATEGORIES.index)
    assert EXAMPLES / "craters" / "example_craters_simple.py" in paths
    assert all(path.name != "_common.py" for path in paths)


def test_runner_lists_and_runs_examples(tmp_path):
    env = {**os.environ, "PYTHONPATH": str(pathlib.Path(terraingen.__file__).resolve().parents[1])}
    listed = subprocess.run([sys.executable, str(EXAMPLES)], capture_output=True, text=True, env=env, check=True)
    assert "craters:\n  craters_simple\n" in listed.stdout
    unknown = subprocess.run([sys.executable, str(EXAMPLES), "nothing_here"], capture_output=True, text=True, env=env)
    assert unknown.returncode == 2
    ran = subprocess.run(
        [sys.executable, str(EXAMPLES), "example_terrain_fbm", "--viewer", "null", "--test", "--output-dir", str(tmp_path)],
        capture_output=True,
        text=True,
        env=env,
    )
    assert ran.returncode == 0, ran.stderr
    assert "example_terrain_fbm: checks passed" in ran.stdout
