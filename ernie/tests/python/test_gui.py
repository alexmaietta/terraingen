import pytest

import ernie
from ernie import tokens

gui = pytest.importorskip("ernie.gui", reason="Ernie was built without its GUI")


def cube_scene(lit=True):
    scene = ernie.Scene()
    scene.define("/World/Cube", tokens.CUBE).set(tokens.SIZE, 2.0)
    if lit:
        scene.define("/World/Sun", tokens.DISTANT_LIGHT).set(
            tokens.TRANSFORM, ernie.distant_light_transform((0.4, -0.6, 1.0))
        )
    return scene


@pytest.fixture
def viewer():
    scene = cube_scene()
    try:
        return gui.Viewer(scene, hidden=True, width=480, height=320)
    except RuntimeError as error:
        pytest.skip(f"No Vulkan device: {error}")


def test_opens_with_its_own_camera_and_lights_the_scene(viewer):
    assert viewer.active_camera == gui.Viewer.perspective_camera
    assert viewer.cameras[0] == gui.Viewer.perspective_camera
    assert str(gui.Viewer.session_root) == "/__session"
    assert viewer.lighting == gui.Lighting.STAGE
    assert viewer.scene.has(gui.Viewer.perspective_camera)
    assert viewer.device_name


def test_step_runs_frame_callbacks_and_close_ends_run(viewer):
    frames = []

    def tick(v, info):
        frames.append(info.frame)
        if len(frames) == 3:
            v.close()

    callback = viewer.on_frame(tick)
    viewer.run()
    assert frames == [0, 1, 2]
    assert not viewer.is_open
    assert not viewer.step()
    viewer.remove_callback(callback)


def test_python_exceptions_in_callbacks_propagate(viewer):
    def fail(v, info):
        raise ValueError("boom")

    callback = viewer.on_frame(fail)
    with pytest.raises(ValueError, match="boom"):
        viewer.step()
    viewer.remove_callback(callback)
    assert viewer.step()


def test_cameras_lighting_and_selection(viewer):
    viewer.camera_from_view("/Cameras/Saved")
    viewer.active_camera = "/Cameras/Saved"
    assert viewer.active_camera == ernie.Path("/Cameras/Saved")
    viewer.lighting = gui.Lighting.CAMERA
    assert viewer.step()
    seen = []
    viewer.on_selection(lambda v, paths: seen.append(paths))
    viewer.selection = [ernie.Path("/World/Cube")]
    assert seen == [[ernie.Path("/World/Cube")]]
    viewer.frame([ernie.Path("/World/Cube")])
    with pytest.raises(ValueError):
        viewer.active_camera = "/World/Cube"


def test_widget_panels_and_menus(viewer):
    panel = viewer.add_panel("Sim")
    slider = panel.slider("Sun", 0.0, 90.0, 10.0, on_change=lambda value: None)
    check = panel.checkbox("Shadows", True)
    combo = panel.combo("Mode", ["a", "b"], 1)
    text = panel.text("ready")
    panel.plot("History", lambda: [1.0, 2.0, 3.0])
    panel.set(slider, 45.0)
    panel.set(check, False)
    panel.set(combo, 0)
    panel.set(text, "running")
    with pytest.raises(ValueError):
        panel.set(slider, True)
    viewer.add_menu_item("Tools", "Reset", lambda v: None)
    assert viewer.step()
    assert panel.title == "Sim"
    viewer.remove_panel("Sim")


def test_capture_writes_a_png(viewer, tmp_path):
    viewer.step()
    path = tmp_path / "view.png"
    viewer.capture(path)
    viewer.step()
    assert path.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"


def test_layouts_and_themes(viewer, tmp_path):
    viewer.step()
    layout = tmp_path / "layout.ini"
    viewer.save_layout(layout)
    assert "[Docking]" in layout.read_text()
    viewer.load_layout(layout)
    theme = gui.light_theme()
    theme.accent = (0.9, 0.3, 0.1, 1.0)
    viewer.theme = theme
    assert viewer.theme.accent == pytest.approx((0.9, 0.3, 0.1, 1.0))
    assert viewer.step()


def test_an_unlit_scene_opens_with_the_default_light():
    try:
        viewer = gui.Viewer(cube_scene(lit=False), hidden=True, width=160, height=120)
    except RuntimeError as error:
        pytest.skip(f"No Vulkan device: {error}")
    assert viewer.lighting == gui.Lighting.DEFAULT


def test_pre_and_post_render_callbacks_run_in_order(viewer):
    order = []
    viewer.on_post_render(lambda v, info: order.append(("post", info.frame)))
    viewer.on_pre_render(lambda v, info: order.append(("pre", info.frame)))
    viewer.on_frame(lambda v, info: order.append(("frame", info.frame)))
    viewer.step()
    assert order == [("frame", 0), ("pre", 0), ("post", 0)]


def test_panels_take_a_dock(viewer):
    panel = viewer.add_panel("Left", gui.Dock.LEFT)
    assert panel.dock == gui.Dock.LEFT
    assert viewer.add_panel("Default").dock == gui.Dock.RIGHT
    assert viewer.step()


def test_renderer_is_an_option_and_switchable(viewer):
    assert "raster" in gui.Viewer.renderers
    assert viewer.renderer == "raster"
    with pytest.raises(ValueError):
        viewer.renderer = "no_such_backend"
    viewer.renderer = "raster"
    viewer.sync()
    assert viewer.step()
