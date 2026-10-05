"""Ernie's interactive viewer: a window onto a Scene that code and people both drive.

The viewer keeps no scene state of its own: its panels read the Scene each frame and write edits
back to it, so changes from code and from the GUI are one history. Its own cameras and lights live
under ``Viewer.session_root`` (``/__session``), which headless renders of the scene ignore::

    import ernie
    from ernie.gui import Viewer

    scene = ernie.Scene()
    # ... define terrain, meshes, lights and cameras ...
    viewer = Viewer(scene, title="Shackleton rim")

    viewer.run()  # hand over control until the window closes

    # or own the loop, e.g. inside a simulation
    while viewer.step():
        scene.set("/World/Rover", ernie.tokens.TRANSFORM, sim.advance())

    # or add callbacks and panels, then run()
    viewer.on_frame(lambda v, info: ...)
    panel = viewer.add_panel("Sim")
    panel.slider("Sun elevation", 0.0, 90.0, 5.0, on_change=lambda deg: ...)

The viewer opens with its own perspective camera framing the scene (``Viewer.perspective_camera``);
``viewer.active_camera = "/Cameras/NAC"`` looks through a camera of the scene instead.
``viewer.lighting`` picks the scene's lights (``Lighting.STAGE``), a light along the view
(``Lighting.CAMERA``) or a default sun (``Lighting.DEFAULT``, chosen when the scene has no lights).

Pass ``hidden=True`` to render without a window, for tests and screenshots (``viewer.capture``).
Callbacks run on the thread that calls run() or step(); keep that the main thread on macOS.
"""

from . import _ernie

if not hasattr(_ernie, "gui"):
    raise ImportError("This build of Ernie has no GUI; configure it with -DERNIE_BUILD_GUI=ON")

FrameInfo = _ernie.gui.FrameInfo
Lighting = _ernie.gui.Lighting
Theme = _ernie.gui.Theme
Viewer = _ernie.gui.Viewer
WidgetPanel = _ernie.gui.WidgetPanel
dark_theme = _ernie.gui.dark_theme
light_theme = _ernie.gui.light_theme

__all__ = [
    "FrameInfo",
    "Lighting",
    "Theme",
    "Viewer",
    "WidgetPanel",
    "dark_theme",
    "light_theme",
]
