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


def quad(scene, path, half=2.0):
    """A square of side 2 * half in the x-y plane, from plain NumPy arrays."""
    points = np.array([[-half, -half, 0], [half, -half, 0], [half, half, 0], [-half, half, 0]])
    return (
        scene.define(path, tokens.MESH)
        .write(tokens.POINTS, points)
        .write(tokens.FACE_VERTEX_COUNTS, np.array([4]))
        .write(tokens.FACE_VERTEX_INDICES, np.arange(4))
    )


def test_renders_meshes_and_point_instancers(renderer):
    scene = ernie.Scene()
    scene.define("/World/Camera", tokens.CAMERA).set(
        tokens.TRANSFORM, ernie.look_at_transform((0, 0, 60), (0, 0, 0), up=(0, 1, 0))
    ).set(tokens.FOCAL_LENGTH, 18.0)
    scene.define("/Render/Settings", tokens.RENDER_SETTINGS).set(tokens.RESOLUTION, (48, 48)).set(
        tokens.AMBIENT, 0.0
    )
    scene.define("/Looks/Red", tokens.MATERIAL).set(tokens.ALBEDO, (1.0, 0.0, 0.0))
    move = np.eye(4)
    move[0, 3] = -12.0
    quad(scene, "/World/Quad").set(tokens.TRANSFORM, move).set(
        tokens.MATERIAL_BINDING, ernie.Path("/Looks/Red")
    )
    positions = np.array([[12.0, -10.0, 0.0], [12.0, 0.0, 0.0], [12.0, 10.0, 0.0]], np.float32)
    scene.define("/World/Rocks", tokens.POINT_INSTANCER).write(tokens.POSITIONS, positions).set(
        tokens.PROTOTYPES, [ernie.Path("/World/Rocks/Rock")]
    )
    quad(scene, "/World/Rocks/Rock", 1.5).set(tokens.DISPLAY_COLOR, (0.0, 1.0, 0.0))

    image = renderer.render(scene)
    pixels = image.astype(np.int32)
    red = (pixels[..., 0] > 200) & (2 * (pixels[..., 1] + pixels[..., 2]) < pixels[..., 0])
    green = (pixels[..., 1] > 200) & (2 * (pixels[..., 0] + pixels[..., 2]) < pixels[..., 1])
    assert red[:, :24].sum() > 8, "the red quad on the left"
    assert green[:, 24:].sum() > 15, "three green instances on the right"
    assert not green[:, 20:28].any(), "the prototype is not drawn on its own"

    positions[:, 0] = -12.0
    positions[:, 1] += 20.0
    scene.write("/World/Rocks", tokens.POSITIONS, positions)
    moved = renderer.render(scene)
    moved_pixels = moved.astype(np.int32)
    moved_green = (moved_pixels[..., 1] > 200) & (2 * moved_pixels[..., 0] < moved_pixels[..., 1])
    assert not moved_green[:, 32:].any(), "instances left the right side"
    assert renderer.take_validation_messages() == []


def test_sphere_lights_alone_light_the_scene(renderer, terrain_scene):
    terrain_scene.remove("/World/Sun")
    terrain_scene.define("/World/Lamp", tokens.SPHERE_LIGHT).set(
        tokens.TRANSFORM, ernie.look_at_transform((0.0, 0.0, 60.0), (0.0, 0.0, 0.0), (0.0, 1.0, 0.0))
    ).set(tokens.LIGHT_POWER, 2.0e6).set(tokens.SHAPING_CONE_ANGLE, 20.0).set(
        tokens.TREAT_AS_POINT, True
    )
    image = renderer.render(terrain_scene)
    centre = image[24, 24, :3].astype(int)
    corner = image[4, 4, :3].astype(int)
    assert centre.sum() > 30, "the spot lights the terrain under it"
    assert corner[0] < centre[0] / 4, "outside the cone stays dark (or blue background)"
    assert renderer.take_validation_messages() == []


def test_primitives_stand_on_a_checkered_ground_plane(renderer):
    scene = ernie.Scene()
    scene.define("/World/Camera", tokens.CAMERA).set(
        tokens.TRANSFORM, ernie.look_at_transform((0.0, -30.0, 8.0), (0.0, 0.0, 4.0))
    ).set(tokens.FOCAL_LENGTH, 18.0)
    scene.define("/World/Sun", tokens.DISTANT_LIGHT).set(
        tokens.TRANSFORM, ernie.distant_light_transform((0.3, -0.5, 1.0))
    )
    scene.define("/World/Ground", tokens.GROUND_PLANE).set(tokens.CHECKER, True).set(
        tokens.CHECKER_SIZE, 2.0
    )
    lift = np.eye(4)
    lift[2, 3] = 4.0
    scene.define("/World/Ball", tokens.SPHERE).set(tokens.GEOM_RADIUS, 4.0).set(
        tokens.DISPLAY_COLOR, (1.0, 0.0, 0.0)
    ).set(tokens.TRANSFORM, lift)
    scene.define("/Render/Settings", tokens.RENDER_SETTINGS).set(
        tokens.CAMERA_REL, ernie.Path("/World/Camera")
    ).set(tokens.RESOLUTION, (64, 48)).set(tokens.CLEAR_COLOR, (0.0, 0.0, 1.0))

    image = renderer.render(scene).astype(int)
    centre = image[24, 32, :3]
    assert centre[0] > 2 * (centre[1] + centre[2]), "the red sphere in the middle"
    floor = image[44, :, :3]
    assert not np.all(floor[:, 2] == 255), "the ground covers the bottom row"
    assert floor[:, 1].std() > 10, "with a checkerboard"
    assert np.all(image[0, :, :3] == (0, 0, 255)), "sky above the horizon"
    assert renderer.take_validation_messages() == []
