"""The examples' GUI side: the scene a view becomes in Ernie's viewer, and the Examples panel that
switches between examples. Imported only with a viewer, so Ernie stays optional."""

import pathlib
import sys

import numpy as np
from _harness import discover_examples, load_example

#: Prims of the scene the viewer shows, as `terraingen --gui` defines them.
TERRAIN_PRIM = "/World/Terrain"
MATERIAL_PRIM = "/Looks/Terrain"
SUN_PRIM = "/World/Sun"
SETTINGS_PRIM = "/Render/Settings"


def show_terrain(scene, view):
    """Writes a view (`Example.views`) as the scene's terrain prim, with a sun, a grey material
    and render settings shading it with an elevation colormap, as `terraingen --gui` does."""
    import ernie
    from ernie import tokens

    _, heights, pixel_size, origin, albedo = view
    if not scene.has(ernie.Path(SUN_PRIM)):
        scene.define(SUN_PRIM, tokens.DISTANT_LIGHT).set(
            tokens.TRANSFORM, ernie.distant_light_transform((0.61, 0.61, 0.5))
        )
    if not scene.has(ernie.Path(SETTINGS_PRIM)):
        scene.define(SETTINGS_PRIM, tokens.RENDER_SETTINGS).set(tokens.COLOR_MODE, tokens.SHADED_COLORMAP)
    material = scene.define(MATERIAL_PRIM, tokens.MATERIAL).set(tokens.ALBEDO, (0.5, 0.5, 0.5))
    scene.define(TERRAIN_PRIM, tokens.TERRAIN).write(tokens.HEIGHTS, np.ascontiguousarray(heights)).set(
        tokens.ORIGIN, origin
    ).set(tokens.CELL_SIZE, pixel_size).set(tokens.MATERIAL_BINDING, ernie.Path(MATERIAL_PRIM))
    if albedo is None:
        scene.clear(ernie.Path(MATERIAL_PRIM), tokens.ALBEDO_MAP)
    else:
        material.write(tokens.ALBEDO_MAP, np.ascontiguousarray(albedo)).set(
            tokens.ALBEDO_MAP_ORIGIN, origin
        ).set(tokens.ALBEDO_MAP_CELL_SIZE, pixel_size)


class ExampleBrowser:
    """The viewer's Examples panel: picks an example among the `example_*.py` files in the category
    folders, shows its first terrain and offers the others, and runs it again, checks it and saves
    its outputs. Examples generate on the GUI's thread, so the window waits while one does."""

    def __init__(self, viewer, example, args):
        from ernie.app import Dock

        self.viewer = viewer
        self.args = args
        self.example = example
        self.entries = discover_examples()
        self.switch_to = None
        self.panel = viewer.add_panel("Examples", Dock.LEFT)
        self._show(0, frame=True)

    def _label(self, path):
        name = path.stem.removeprefix("example_" + path.parent.name + "_").replace("_", " ")
        return f"{path.parent.name}: {name}"

    def _build_panel(self):
        panel = self.panel
        panel.clear()
        current = sys.modules[type(self.example).__module__].__file__
        selected = next((i for i, path in enumerate(self.entries) if path.samefile(current)), 0)
        panel.combo("Example", [self._label(path) for path in self.entries], selected, on_change=self._choose)
        panel.text(type(self.example).description())
        if len(self.example.views) > 1:
            panel.combo("View", [view[0] for view in self.example.views], self.view, on_change=self._show)
        panel.separator()
        panel.button("Run again", lambda: self._choose(selected))
        panel.button("Test", self.test)
        panel.button("Save", self.save)
        self.status = panel.text("")

    def _choose(self, index):
        self.switch_to = self.entries[index]

    def _show(self, index, frame=False):
        self.view = index
        if self.example.views:
            show_terrain(self.viewer.scene, self.example.views[index])
            if frame:
                self.viewer.reset_session()
        self._build_panel()

    def _report(self, text, level="info"):
        self.viewer.log(text, level)
        self.panel.set(self.status, text)

    def test(self):
        """Runs the example's checks, reporting to the log; returns whether they passed."""
        try:
            self.example.test_final()
        except Exception as error:  # noqa: BLE001 - shown in the Log panel
            self._report(f"{self.example.name()}: check failed: {error}", "error")
            return False
        self._report(f"{self.example.name()}: checks passed")
        return True

    def save(self):
        """Writes the example's outputs to `--output-dir`, reporting to the log."""
        out = pathlib.Path(self.args.output_dir)
        out.mkdir(parents=True, exist_ok=True)
        self.example.save(out)
        self._report(f"Saved {self.example.name()} to {out}")

    def update(self):
        """Switches to the example picked since the last frame, if any."""
        if self.switch_to is None:
            return
        path, self.switch_to = self.switch_to, None
        self.viewer.log(f"Generating {path.stem}...")
        self.viewer.step()
        try:
            example_class = load_example(path)
            args = example_class.create_parser().parse_args([])
            for name in ("viewer", "headless", "output_dir"):
                setattr(args, name, getattr(self.args, name))
            example = example_class(self.viewer, args)
            example.generate()
        except Exception as error:  # noqa: BLE001 - shown in the Log panel
            self._report(f"Could not run {path.name}: {error}", "error")
            return
        self.example = example
        self._show(0, frame=True)
        self._report(f"{example.name()}: {example.description()}")
