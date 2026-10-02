import numpy as np
import pytest

import ernie
from ernie import tokens


def test_renders_ldr_color_as_a_uint8_array(renderer, terrain_scene):
    assert renderer.device_name
    assert "hapke" in renderer.brdfs
    image = renderer.render(terrain_scene, "/Render/Settings")
    assert image.shape == (48, 48, 4) and image.dtype == np.uint8
    assert not image.flags.writeable
    background = np.all(image[..., :3] == (0, 0, 255), axis=-1)
    assert background[0, 0], "clear colour from ernie:clearColor"
    assert (~background).mean() > 0.25, "the terrain covers the view"
    assert renderer.take_validation_messages() == []


def test_material_edits_change_the_image(renderer, terrain_scene):
    scene = terrain_scene
    bright = renderer.render(scene).astype(np.int32)
    scene.define("/Looks/Dark", tokens.MATERIAL).set(tokens.ALBEDO, (0.2, 0.2, 0.2))
    scene.set("/World/Terrain", tokens.MATERIAL_BINDING, ernie.Path("/Looks/Dark"))
    dark = renderer.render(scene).astype(np.int32)
    terrain = np.any(bright[..., :3] != (0, 0, 255), axis=-1)
    assert dark[terrain, :3].sum() < bright[terrain, :3].sum()
    assert renderer.take_validation_messages() == []


def test_upload_errors_name_the_prim_and_attribute(renderer, terrain_scene):
    terrain_scene.write("/World/Terrain", tokens.HEIGHTS, np.zeros((4, 4), dtype=np.float64))
    with pytest.raises(ValueError, match="heights of '/World/Terrain'"):
        renderer.sync(terrain_scene)


def test_missing_camera_and_bad_settings_raise(renderer):
    scene = ernie.Scene()
    with pytest.raises(ValueError, match="no Camera"):
        renderer.render(scene)
    scene.define("/NotSettings", tokens.XFORM)
    with pytest.raises(ValueError, match="RenderSettings"):
        renderer.render(scene, "/NotSettings")
