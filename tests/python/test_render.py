# SPDX-License-Identifier: BSD-3-Clause

import gc
import math

import numpy as np
import pytest

import terraingen as tg

ernie = pytest.importorskip("ernie")
from terraingen import render  # noqa: E402


def make_raster(width=33, height=17, cell=2.0):
    raster = tg.Raster(width, height)
    raster.transform = tg.GeoTransform.north_up(100.0, 50.0, cell)
    y, x = np.mgrid[0:height, 0:width]
    raster.values[:] = (5.0 * np.sin(x / 4.0) * np.cos(y / 5.0)).astype(np.float32)
    return raster


def test_add_terrain_shares_cells_and_georeferencing():
    raster = make_raster()
    raster.no_data = -9999.0
    scene = ernie.Scene()
    prim = render.add_terrain(scene, raster, exaggeration=2.0, material="/Looks/Boulder")
    heights = scene.read(render.TERRAIN_PATH, ernie.tokens.HEIGHTS)
    assert np.shares_memory(heights, raster.values)
    assert heights.shape == (17, 33)
    assert prim.get(ernie.tokens.ORIGIN) == (100.0, 50.0)
    assert prim.get(ernie.tokens.CELL_SIZE) == 2.0
    assert prim.get(ernie.tokens.NO_DATA) == -9999.0
    assert prim.get(ernie.tokens.EXAGGERATION) == 2.0
    assert prim.get(ernie.tokens.MATERIAL_BINDING) == ernie.Path("/Looks/Boulder")

    raster.no_data = None
    render.add_terrain(scene, raster)
    assert scene.get(render.TERRAIN_PATH, ernie.tokens.NO_DATA) is None


def test_the_scene_keeps_the_raster_alive():
    scene = ernie.Scene()
    raster = make_raster()
    render.add_terrain(scene, raster)
    expected = raster.values.copy()
    del raster
    gc.collect()
    np.testing.assert_array_equal(scene.read(render.TERRAIN_PATH, ernie.tokens.HEIGHTS), expected)


def test_rejects_rotated_and_rectangular_rasters():
    raster = make_raster()
    raster.transform = tg.GeoTransform.from_gdal((0.0, 1.0, 0.2, 0.0, 0.0, -1.0))
    with pytest.raises(ValueError, match="rotation"):
        render.add_terrain(ernie.Scene(), raster)
    raster.transform = tg.GeoTransform.from_gdal((0.0, 1.0, 0.0, 0.0, 0.0, -2.0))
    with pytest.raises(ValueError, match="square"):
        render.add_terrain(ernie.Scene(), raster)


def test_bounds_and_directions():
    raster = make_raster()
    raster.values[0, 0] = np.nan
    (x0, y0, z0), (x1, y1, z1) = render.terrain_bounds(raster, exaggeration=2.0)
    assert (x0, x1) == (101.0, 100.0 + 33 * 2.0 - 1.0)
    assert (y0, y1) == (50.0 - 17 * 2.0 + 1.0, 49.0)
    assert z0 >= -10.0 and z1 <= 10.0 and z1 > z0
    east = render.direction(90.0, 0.0)
    assert east == pytest.approx((1.0, 0.0, 0.0), abs=1e-12)
    assert render.direction(0.0, 90.0)[2] == pytest.approx(1.0)


def test_sun_and_camera_prims():
    raster = make_raster()
    scene = ernie.Scene()
    render.add_sun(scene, azimuth_deg=90.0, elevation_deg=30.0, intensity=2.0)
    transform = scene.world_transform(render.SUN_PATH)
    np.testing.assert_allclose(transform[:3, 2], render.direction(90.0, 30.0), atol=1e-12)
    assert scene.get(render.SUN_PATH, ernie.tokens.INTENSITY) == 2.0

    render.frame_terrain(scene, raster, azimuth_deg=180.0, elevation_deg=45.0)
    camera = scene.world_transform(render.CAMERA_PATH)
    eye = camera[:3, 3]
    (x0, y0, _), (x1, y1, _) = render.terrain_bounds(raster)
    assert eye[1] < 0.5 * (y0 + y1), "the camera stands south of the terrain"
    focal = scene.get(render.CAMERA_PATH, ernie.tokens.FOCAL_LENGTH)
    aperture = scene.get(render.CAMERA_PATH, ernie.tokens.VERTICAL_APERTURE)
    assert 2.0 * math.degrees(math.atan(aperture / (2.0 * focal))) == pytest.approx(40.0)
    near, far = scene.get(render.CAMERA_PATH, ernie.tokens.CLIPPING_RANGE)
    assert 0.0 < near < far


def test_renders_a_generated_terrain_headlessly():
    try:
        renderer = ernie.Renderer()
    except RuntimeError as error:
        pytest.skip(f"No Vulkan device: {error}")
    raster = make_raster(65, 65)
    scene = ernie.Scene()
    render.add_terrain(scene, raster)
    render.add_sun(scene)
    render.frame_terrain(scene, raster)
    scene.define("/Render/Settings", ernie.tokens.RENDER_SETTINGS).set(ernie.tokens.RESOLUTION, (32, 32))
    image = np.asarray(renderer.render(scene))
    assert image.shape == (32, 32, 4)
    lit = image[..., :3].sum(axis=-1) > 0
    assert lit[16, 16], "the terrain is centered"
    assert not lit[0, 0] and not lit[-1, -1], "the whole terrain fits the frame"


def boulder_models():
    options = tg.BoulderLayerOptions(
        sizes=tg.BoulderSizes(distribution=None, coverage=0.1, min_diameter=0.5, max_diameter=2.0),
        rendering=tg.BoulderRendering(
            shape_prototypes=4, model_threshold=tg.BoulderModelThreshold(min_diameter=0.5)
        ),
    )
    terrain = tg.TerrainGenerator(None, [tg.BoulderLayer(options)])
    grid = tg.RasterGrid.centered((0, 0), (80 * 0.25, 80 * 0.25), 0.25)
    return tg.placed_boulders(terrain, grid, 0.25).models


def test_quaternions_match_rotation_matrices():
    for angle in (0.0, 40.0, 180.0):
        turn = tg.RigidTransform.rotation_z(angle).after(tg.RigidTransform.rotation_x(17.0))
        x, y, z, w = render._quaternion(turn.rotation)
        m = np.asarray(turn.rotation).reshape(3, 3)
        rebuilt = np.array(
            [
                [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
            ]
        )
        assert rebuilt == pytest.approx(m, abs=1e-9)


def test_add_boulders_instances_shared_shapes_or_bakes_each():
    models = boulder_models()
    assert len(models) > 3
    scene = ernie.Scene()
    instancer = render.add_boulders(scene, models)
    assert len(instancer.positions) == len(models)
    assert len(instancer.prototypes) <= 4
    baked = ernie.Scene()
    render.add_boulders(baked, models, instanced=False)
    first = ernie.Path(render.BOULDERS_PATH).child("Boulder0")
    points = np.asarray(ernie.Mesh.at(baked, first).points)
    split, _ = models[0].shape.mesh.with_creases(60.0)
    expected = models[0].placement.apply(tuple(split.arrays[0][0]))
    assert points[0] == pytest.approx(expected, abs=1e-4)
