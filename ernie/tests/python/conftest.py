import numpy as np
import pytest

import ernie
from ernie import tokens


@pytest.fixture(scope="session")
def renderer():
    """A validating renderer, or a skip without a Vulkan device."""
    try:
        return ernie.RasterRenderer(validation=True)
    except RuntimeError as error:
        pytest.skip(f"No Vulkan device: {error}")


def rolling_heights(rows=65, cols=65):
    """About +/-40 m of smooth relief on a grid with 2 m cells."""
    y, x = np.mgrid[0:rows, 0:cols].astype(np.float32) * 2.0
    return (30.0 * np.sin(x / 45.0) * np.cos(y / 55.0) + 10.0 * np.sin((x + y) / 20.0)).astype(
        np.float32
    )


@pytest.fixture
def terrain_scene():
    return make_terrain_scene()


def make_terrain_scene():
    """A lit terrain seen from above at 48 x 48 pixels against a blue background."""
    heights = rolling_heights()
    rows, cols = heights.shape
    scene = ernie.Scene()
    scene.define("/World/Terrain", tokens.TERRAIN).write(tokens.HEIGHTS, heights).set(
        tokens.ORIGIN, (-cols, rows)
    ).set(tokens.CELL_SIZE, 2.0)
    scene.define("/World/Sun", tokens.DISTANT_LIGHT).set(
        tokens.TRANSFORM, ernie.distant_light_transform((1.0, 1.0, 0.6))
    )
    scene.define("/World/Camera", tokens.CAMERA).set(
        tokens.TRANSFORM, ernie.look_at_transform((0.0, -150.0, 200.0), (0.0, 0.0, 0.0))
    ).set(tokens.FOCAL_LENGTH, 18.0)
    scene.define("/Render/Settings", tokens.RENDER_SETTINGS).set(
        tokens.CAMERA_REL, ernie.Path("/World/Camera")
    ).set(tokens.RESOLUTION, (48, 48)).set(tokens.CLEAR_COLOR, (0.0, 0.0, 1.0))
    return scene
