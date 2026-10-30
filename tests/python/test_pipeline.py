# SPDX-License-Identifier: BSD-3-Clause

"""pipeline: building and evaluating terrains from configs and objects, outputs and body meshes."""

import pathlib

import numpy as np
import pytest

import terraingen

SPHERE_CONFIG = {
    "schema_version": 1,
    "body": {"type": "sphere", "radius_m": 1737400},
    "outputs": [
        {
            "type": "heightmap",
            "path": "h.tif",
            "pixel_size_m": 100,
            "extent_deg": {"center": [0, 0], "size": [0.05, 0.05]},
        }
    ],
    "layers": [{"type": "noise", "stack": [{"wavelength_m": 2000, "amplitude_m": 10}]}],
    "albedo": {"source": {"type": "synthetic", "value": 0.12}},
}

CRATER_COLUMNS = {
    "x",
    "y",
    "diameter",
    "age_gyr",
    "id",
    "manual",
    "replace",
    "floor_depth",
    "rim_height",
    "depth_ratio",
    "freshness",
    "complex",
}


@pytest.fixture
def generated(small_config):
    """The small config's terrain, generated on two threads."""
    return terraingen.generate_from_config(terraingen.TerrainGenConfig.from_dict(small_config), threads=2)


@pytest.fixture
def ejecta_terrain(small_config):
    """The small config's terrain with boulders thrown from its craters, so they read them."""
    small_config["layers"][2]["ejecta"] = {"min_crater_diameter_m": 3}
    return terraingen.build_terrain(terraingen.TerrainGenConfig.from_dict(small_config))


def test_generate_from_config_fills_finite_varied_heights(generated):
    assert generated.grids[0].heights.shape == (65, 81)
    heights = generated.grids[0].heights.values
    assert np.isfinite(heights).all() and np.ptp(heights) > 0


def test_generate_from_config_returns_the_crater_table(generated):
    craters = generated.terrain.craters(generated.grids[0].grid).table
    assert set(craters) == CRATER_COLUMNS
    assert (craters["depth_ratio"] > 0).all() and (craters["freshness"] <= 1).all()
    assert len(craters["x"]) > 0
    assert craters["diameter"].min() >= 3 and craters["diameter"].max() < 15
    assert craters["id"].dtype == np.uint64 and (craters["manual"] == 0).all()


def test_generate_from_config_returns_the_boulder_table(generated):
    placed = generated.terrain.boulders(generated.grids[0].grid)
    assert len(placed.table["x"]) > 0 and placed.table["diameter"].min() >= 0.5
    assert len(placed.models) == 0 and generated.grids[0].albedo == []


def test_generated_outputs_list_features_only_through_the_terrain(generated):
    assert not hasattr(generated, "placed_craters") and not hasattr(generated, "placed_boulders")
    assert not hasattr(generated, "generator")
    for name in ("placed_craters", "placed_boulders", "evaluate", "fill_heights", "apply_layers"):
        assert not hasattr(terraingen, name)
    for name in ("height_at", "heights_at", "EvaluationCache", "TerrainGenerator", "build_generator"):
        assert not hasattr(terraingen, name)


def test_generate_from_config_is_the_same_on_one_thread(generated, small_config):
    again = terraingen.generate_from_config(terraingen.TerrainGenConfig.from_dict(small_config), threads=1)
    np.testing.assert_array_equal(again.grids[0].heights.values, generated.grids[0].heights.values)


def test_generate_from_config_on_auto_matches_the_cpu_to_float_rounding(generated, small_config):
    config = terraingen.TerrainGenConfig.from_dict(small_config)
    auto = terraingen.generate_from_config(config, threads=2, backend=terraingen.Backend.AUTO)
    assert generated.backend == terraingen.Backend.CPU and generated.summary().endswith(" on cpu")
    assert auto.backend == terraingen.resolve_backend(terraingen.Backend.AUTO)
    np.testing.assert_allclose(
        auto.grids[0].heights.values, generated.grids[0].heights.values, rtol=1e-4, atol=1e-4
    )


def test_evaluate_takes_a_backend_and_cpu_is_exact(small_config):
    terrain = terraingen.build_terrain(terraingen.TerrainGenConfig.from_dict(small_config))
    grid = terraingen.TerrainGenConfig.from_dict(small_config).output_grids()[0]
    cpu = terrain.evaluate(grid).values
    np.testing.assert_array_equal(terrain.evaluate(grid, backend=terraingen.Backend.CPU).values, cpu)
    auto = terrain.evaluate(grid, backend=terraingen.Backend.AUTO).values
    np.testing.assert_allclose(auto, cpu, rtol=1e-4, atol=1e-4)


def test_point_queries_give_the_same_bits_every_time(small_config):
    for layer in small_config["layers"]:
        if layer["type"] == "craters":
            layer.setdefault("apply", {})["datum"] = "plane"
    terrain = terraingen.build_terrain(terraingen.TerrainGenConfig.from_dict(small_config))
    points = np.column_stack([np.linspace(-20.0, 20.0, 40), np.linspace(15.0, -15.0, 40)])
    fresh = terrain.heights_at(points, 0.5)
    np.testing.assert_array_equal(terrain.heights_at(points, 0.5, threads=2), fresh)
    np.testing.assert_array_equal(terrain.ground(len(terrain.layers())).heights_at(points, 0.5), fresh)
    for (x, y), height in zip(points[::7], fresh[::7], strict=True):
        assert terrain.height_at(x, y, 0.5) == height


def albedo_config(small_config):
    small_config["albedo"] = terraingen.config_catalog()["albedo"]
    heightmap = small_config["outputs"][0]
    area = {key: heightmap[key] for key in ("pixel_size_m", "extent_m")}
    small_config["outputs"] = small_config["outputs"] + [{"type": "albedo", "path": "albedo.tif", **area}]
    return terraingen.TerrainGenConfig.from_dict(small_config)


def test_generate_from_config_evaluates_the_albedo(tmp_path, small_config):
    generated = terraingen.generate_from_config(albedo_config(small_config), tmp_path)
    albedo = generated.grids[0].albedo
    assert len(albedo) >= 1 and albedo[0].shape == (65, 81)


def test_write_outputs_writes_every_output(tmp_path, small_config):
    config = albedo_config(small_config)
    written = terraingen.write_outputs(config, terraingen.generate_from_config(config, tmp_path), tmp_path)
    assert all(isinstance(path, pathlib.Path) for path in written)
    assert sorted(path.name for path in written) == ["albedo.tif", "craters.csv", "heights.tif"]
    assert (tmp_path / "craters.csv").read_text().startswith("x,y,diameter")


def test_generate_from_config_returns_each_output_once_per_grid(tmp_path, small_config):
    area = {key: small_config["outputs"][0][key] for key in ("pixel_size_m", "extent_m")}
    small_config["outputs"] = [
        {"type": "heightmap", **area},
        {"type": "crater_list", **area},
        {"type": "boulder_list", **area},
        {"type": "terrain_mesh", **area},
        {"type": "report"},
    ]
    config = terraingen.TerrainGenConfig.from_dict(small_config)
    outputs = terraingen.generate_from_config(config)
    assert len(outputs.grids) == 1
    grid = outputs.grid()
    assert outputs.find(grid.grid) is not None and outputs.body_mesh is None
    expected = config.output_grids()[0]
    assert (grid.grid.width, grid.grid.height, grid.grid.bounds) == (81, 65, expected.bounds)
    heights = grid.heights.values
    heights[0, 0] = 1234.0
    assert outputs.grid().heights.values[0, 0] == 1234.0, "a view of the result, not a copy"
    craters = outputs.terrain.craters(grid.grid)
    boulders = outputs.terrain.boulders(grid.grid)
    assert np.array_equal(grid.craters.table["id"], craters.table["id"])
    assert len(grid.boulders.boulders) == len(boulders.boulders)
    assert grid.mesh.positions.shape == (65 * 81, 3)
    assert grid.albedo == [] and grid.materials == [] and grid.photometry == []
    assert terraingen.write_outputs(config, outputs, tmp_path) == []
    assert list(tmp_path.iterdir()) == []


def test_generate_from_config_needs_an_extent_without_a_raster_source(small_config):
    del small_config["outputs"][0]["extent_m"]
    with pytest.raises(terraingen.ConfigError) as missing:
        terraingen.generate_from_config(terraingen.TerrainGenConfig.from_dict(small_config))
    assert missing.value.path == "outputs[0].extent_m"


def test_build_terrain_keeps_the_surface_and_layers():
    terrain = terraingen.build_terrain(terraingen.TerrainGenConfig.from_dict(SPHERE_CONFIG))
    assert terrain.surface().is_body and len(terrain.layers()) == 1
    assert isinstance(terrain.source(), terraingen.SyntheticSource) and terrain.grid() is None
    assert terrain.keys().is_geographic


def test_evaluating_a_built_terrain_matches_generate_from_config():
    config = terraingen.TerrainGenConfig.from_dict(SPHERE_CONFIG)
    grid = config.output_grids()[0]
    heights = terraingen.build_terrain(config).evaluate(grid).values
    np.testing.assert_array_equal(heights, terraingen.generate_from_config(config).grids[0].heights.values)


def test_build_albedo_evaluates_the_configured_albedo():
    config = terraingen.TerrainGenConfig.from_dict(SPHERE_CONFIG)
    terrain = terraingen.build_terrain(config)
    built_albedo = terraingen.build_albedo(config, terrain)
    bands = terraingen.evaluate_albedo(built_albedo, config.output_grids()[0], ground=terrain)
    assert bands[0].values.mean() == pytest.approx(0.12)


def test_build_albedo_rejects_a_terrain_changed_after_building():
    config = terraingen.TerrainGenConfig.from_dict(
        {
            **SPHERE_CONFIG,
            "layers": [
                {
                    "type": "craters",
                    "generate_random": False,
                    "manual": [{"x": 0.0, "y": 0.0, "diameter_m": 500.0}],
                }
            ],
            "albedo": {"source": {"type": "synthetic", "value": 0.12}, "layers": [{"type": "craters"}]},
        }
    )
    terrain = terraingen.build_terrain(config)
    terraingen.build_albedo(config, terrain)
    without_layers = terraingen.Terrain(terrain.surface(), terrain.source(), [])
    with pytest.raises(terraingen.ConfigError):
        terraingen.build_albedo(config, without_layers)


def test_a_terrain_without_a_source_starts_at_the_datum():
    terrain = terraingen.Terrain()
    assert terrain.source() is None and terrain.layers() == [] and terrain.grid() is None
    grid = terraingen.FlatSource(4, 3, 2.0).grid()
    assert (terrain.evaluate(grid).values == 0.0).all()
    assert terrain.height_at(5.0, -2.0, 1.0) == 0.0
    with pytest.raises(ValueError):
        terrain.evaluate()
    with pytest.raises(ValueError):
        terrain.craters()
    with pytest.raises(ValueError):
        terrain.height_at(5.0, -2.0, 0.0)


def test_a_terrain_is_checked_once_when_built(moon, paraboloid):
    with pytest.raises(ValueError):
        terraingen.Terrain(layers=[None])
    craters = terraingen.CraterLayer(None, terraingen.CraterLayerOptions(), moon)
    with pytest.raises(ValueError):
        terraingen.Terrain(terraingen.Surface(), paraboloid, [craters])
    terrain = terraingen.Terrain(moon, None, [craters])
    assert isinstance(terrain, terraingen.Ground) and terrain.layers()[0] is craters


@pytest.fixture
def lunar_site(moon, lunar_models, noise_layer):
    """Noise and 20 to 300 km craters on a 65 x 65 site at 4 km near Mons Mouton's longitude."""
    noise = noise_layer(1, 400000.0, 3000.0)
    generation = terraingen.CraterGenerationOptions(d_min=20000.0, d_max=300000.0)
    craters = terraingen.CraterLayer(
        lunar_models(3.8), terraingen.CraterLayerOptions(generation=generation), moon
    )
    site = terraingen.FlatSource(65, 65, 4000.0, center=(23.47, 0.67), surface=moon)
    return terraingen.Terrain(moon, site, [noise, craters])


def test_a_site_on_a_sphere_has_pixels_in_degrees(moon):
    site = terraingen.FlatSource(65, 65, 4000.0, center=(23.47, 0.67), surface=moon)
    assert site.transform.pixel_width == pytest.approx(4000.0 / moon.unit_length)


def test_terrain_on_a_sphere_has_relief(lunar_site):
    assert lunar_site.surface().is_body
    heights = lunar_site.evaluate()
    assert heights.shape == (65, 65) and np.ptp(heights.values) > 100.0


def test_point_query_on_a_sphere_matches_the_grid(lunar_site):
    center = lunar_site.evaluate().values[32, 32]
    assert lunar_site.height_at(23.47, 0.67, 4000.0) == pytest.approx(center, abs=1e-3)


def test_point_batches_match_single_point_queries(lunar_site):
    points = np.array([[23.47, 0.67], [23.5, 0.6], [23.4, 0.7]])
    heights = lunar_site.heights_at(points, 4000.0, threads=2)
    assert heights.dtype == np.float32 and heights.shape == (3,)
    for (x, y), height in zip(points, heights, strict=True):
        assert height == lunar_site.height_at(x, y, 4000.0)
    with pytest.raises(ValueError):
        lunar_site.heights_at(np.zeros((2, 3)), 4000.0)


def test_body_mesh_arrays_match_its_counts(lunar_site):
    mesh = terraingen.body_mesh(lunar_site, 8)
    positions, triangles = mesh.arrays
    assert positions.shape == (mesh.vertex_count, 3) and triangles.shape == (mesh.triangle_count, 3)


def test_body_mesh_lies_near_the_body_radius(lunar_site, moon):
    radii = np.linalg.norm(terraingen.body_mesh(lunar_site, 8).arrays[0], axis=1)
    assert radii.min() > moon.radius - 10000.0 and radii.max() < moon.radius + 10000.0


def test_body_mesh_writes_ply_and_glb(tmp_path, lunar_site):
    mesh = terraingen.body_mesh(lunar_site, 8)
    for name in ("moon.stl", "moon.obj", "moon.glb"):
        mesh.write(tmp_path / name)
    assert (tmp_path / "moon.glb").read_bytes()[:4] == b"glTF"
    assert terraingen.read_mesh(tmp_path / "moon.obj").mesh.triangle_count == mesh.triangle_count


def test_build_mesh_has_a_vertex_per_cell(paraboloid):
    heights = terraingen.Terrain(source=paraboloid).evaluate()
    mesh = terraingen.build_mesh(heights)
    assert mesh.vertex_count == heights.width * heights.height
    assert mesh.triangle_count == 2 * (heights.width - 1) * (heights.height - 1)
    assert not hasattr(terraingen, "height_mesh")


def test_build_mesh_views_its_arrays_without_copying(paraboloid):
    heights = terraingen.Terrain(source=paraboloid).evaluate()
    mesh = terraingen.build_mesh(heights, skirt=1.0)
    positions, triangles = mesh.positions, mesh.triangles
    assert positions.shape == (mesh.vertex_count, 3) and positions.dtype == np.float64
    assert triangles.shape == (mesh.triangle_count, 3) and triangles.dtype == np.uint32
    assert np.shares_memory(positions, mesh.positions)
    assert np.shares_memory(triangles, mesh.triangles)
    copied_positions, copied_triangles = mesh.arrays
    np.testing.assert_array_equal(positions, copied_positions)
    np.testing.assert_array_equal(triangles, copied_triangles)
    positions[0, 2] = 1234.5
    assert mesh.arrays[0][0, 2] == 1234.5
    del mesh
    assert positions[0, 2] == 1234.5


def _fill(heights, surface=None, site=(0.0, 0.0), cutouts=(), skirt=0.0):
    """get_mesh_size, then fill_mesh into fresh NumPy arrays of that size."""
    size = terraingen.get_mesh_size(heights, cutouts=list(cutouts), skirt=skirt)
    positions = np.full((size.vertices, 3), np.nan)
    triangles = np.full((size.triangles, 3), -1, dtype=np.int32)
    surface = surface or terraingen.Surface()
    terraingen.fill_mesh(heights, positions, triangles, surface, site, list(cutouts), skirt)
    return size, positions, triangles


def test_fill_mesh_writes_what_build_mesh_makes_into_numpy_arrays(paraboloid, moon):
    heights = terraingen.Terrain(source=paraboloid).evaluate()
    heights.values[3, 4] = -9999.0
    heights.no_data = -9999.0
    first = heights.cell_center(1, 4)
    last = heights.cell_center(3, 2)
    box = (first[0] - 0.01, first[1] - 0.01, last[0] + 0.01, last[1] + 0.01)
    for options in ({}, {"cutouts": [box]}, {"skirt": 2.0}, {"cutouts": [box], "skirt": 0.5}):
        mesh = terraingen.build_mesh(heights, **options)
        size, positions, triangles = _fill(heights, **options)
        assert (size.vertices, size.triangles) == (mesh.vertex_count, mesh.triangle_count)
        assert size == terraingen.get_mesh_size(heights, **options)
        assert positions.tobytes() == mesh.positions.tobytes()
        np.testing.assert_array_equal(triangles, mesh.triangles.astype(np.int32))
    site = terraingen.FlatSource(9, 9, 100.0, center=(23.47, 0.67), surface=moon)
    spherical = terraingen.Terrain(moon, site).evaluate()
    mesh = terraingen.build_mesh(spherical, surface=moon, site=(23.47, 0.67), skirt=3.0)
    _, positions, triangles = _fill(spherical, surface=moon, site=(23.47, 0.67), skirt=3.0)
    assert positions.tobytes() == mesh.positions.tobytes()
    np.testing.assert_array_equal(triangles, mesh.triangles.astype(np.int32))


def test_fill_mesh_rejects_arrays_it_cannot_fill_in_place(paraboloid):
    heights = terraingen.Terrain(source=paraboloid).evaluate()
    size = terraingen.get_mesh_size(heights)
    positions = np.zeros((size.vertices, 3))
    triangles = np.zeros((size.triangles, 3), dtype=np.int32)
    with pytest.raises(ValueError):
        terraingen.fill_mesh(heights, positions[:-1], triangles)
    with pytest.raises(ValueError):
        terraingen.fill_mesh(heights, positions, np.zeros((size.triangles + 1, 3), dtype=np.int32))
    with pytest.raises(TypeError):
        terraingen.fill_mesh(heights, positions.astype(np.float32), triangles)
    with pytest.raises(TypeError):
        terraingen.fill_mesh(heights, positions, triangles.astype(np.int64))
    with pytest.raises(TypeError):
        terraingen.fill_mesh(heights, np.asfortranarray(positions), triangles)
    positions.flags.writeable = False
    with pytest.raises(TypeError):
        terraingen.fill_mesh(heights, positions, triangles)
    with pytest.raises((TypeError, ValueError)):
        terraingen.fill_mesh(heights, np.zeros((size.vertices, 4)), triangles)


def test_fill_writes_what_evaluate_gives_in_place(paraboloid):
    terrain = terraingen.Terrain(source=paraboloid)
    grid = paraboloid.grid()
    expected = terrain.evaluate(grid, spacing=2.0, threads=2)
    out = np.full((grid.height, grid.width), np.nan, dtype=np.float32)
    address = out.__array_interface__["data"][0]
    assert terrain.fill(grid, out, spacing=2.0, threads=2) is None
    assert out.__array_interface__["data"][0] == address
    assert out.tobytes() == np.asarray(expected).tobytes()
    with pytest.raises(ValueError):
        terrain.fill(grid, np.zeros((grid.width, grid.height + 1), dtype=np.float32))
    with pytest.raises(TypeError):
        terrain.fill(grid, np.zeros((grid.height, grid.width)))


def test_body_mesh_needs_a_sphere():
    with pytest.raises(ValueError):
        terraingen.body_mesh(terraingen.Terrain(source=terraingen.FlatSource(8, 8, 1.0)), 4)


def test_generated_grids_know_their_grid_and_list_features_over_any(generated):
    (grid,) = [g.grid for g in generated.grids]
    assert (grid.width, grid.height) == (81, 65)
    half = terraingen.FlatSource(40, 32, 0.5).grid()
    terrain = generated.terrain
    everywhere = len(terrain.craters(grid).craters)
    assert len(terrain.craters(half).craters) <= everywhere
    assert len(terrain.craters(grid, spacing=4.0).craters) <= everywhere


def assert_same_bits(a, b):
    assert np.asarray(a).tobytes() == np.asarray(b).tobytes()


def test_each_ground_of_a_terrain_is_the_terrain_of_its_first_layers(generated):
    terrain = generated.terrain
    grid = generated.grids[0].grid
    layers = terrain.layers()
    assert len(layers) >= 2
    for stages in range(len(layers) + 1):
        ground = terrain.ground(stages)
        built = terraingen.Terrain(terrain.surface(), terrain.source(), layers[:stages])
        assert isinstance(ground, terraingen.Ground) and len(ground.layers()) == stages
        assert_same_bits(ground.evaluate(grid, threads=2), built.evaluate(grid, threads=2))
    assert_same_bits(terrain.ground(len(layers)).evaluate(grid), terrain.evaluate(grid))
    with pytest.raises(ValueError):
        terrain.ground(len(layers) + 1)


def test_a_stage_cache_restarts_an_evaluation_from_its_layer(generated):
    terrain = generated.terrain
    grid = generated.grids[0].grid
    expected = terrain.evaluate(grid, threads=2)
    cache = terraingen.StageCache(2)
    assert cache.stage() == 2 and cache.empty()
    assert_same_bits(terrain.evaluate(grid, threads=2, stage_cache=cache), expected)
    assert not cache.empty()
    assert_same_bits(terrain.evaluate(grid, threads=2, stage_cache=cache), expected)
    out = np.empty((grid.height, grid.width), dtype=np.float32)
    terrain.fill(grid, out, threads=2, stage_cache=cache)
    assert_same_bits(out, expected)
    cache.clear()
    assert cache.empty()
    with pytest.raises(ValueError):
        terrain.evaluate(grid, stage_cache=terraingen.StageCache(len(terrain.layers()) + 1))


def test_a_stage_cache_applies_a_replaced_layer_to_the_bits_of_a_fresh_terrain(generated):
    terrain = generated.terrain
    grid = generated.grids[0].grid
    cache = terraingen.StageCache(2)
    terrain.evaluate(grid, threads=2, stage_cache=cache)
    scale = terraingen.ScaleLayer(2.0, 1.0)
    terrain.set_layer(2, scale)
    fresh = terraingen.Terrain(terrain.surface(), terrain.source(), terrain.layers()[:2] + [scale])
    assert_same_bits(terrain.evaluate(grid, threads=2, stage_cache=cache), fresh.evaluate(grid, threads=2))
    terrain.set_layer(1, terraingen.ScaleLayer(1.0))
    assert_same_bits(
        terrain.evaluate(grid, threads=2, stage_cache=cache),
        terraingen.Terrain(terrain.surface(), terrain.source(), terrain.layers()).evaluate(grid, threads=2),
    )


def test_set_layer_renews_the_versions_of_the_layer_and_those_reading_it(
    ejecta_terrain, lunar_models, crater_layer
):
    terrain = ejecta_terrain
    versions = [terrain.layer_version(n) for n in range(3)]
    source_version = terrain.source_version()
    noise = terraingen.NoiseLayer(seed=9, wavelength=20.0, amplitude=0.5)
    terrain.set_layer(0, noise)
    assert terrain.layers()[0] is noise
    assert terrain.layer_version(0) != versions[0]
    assert [terrain.layer_version(n) for n in (1, 2)] == versions[1:]
    old_boulders = terrain.layers()[2]
    terrain.set_layer(1, crater_layer(lunar_models(), 3.0, 15.0, seed=5))
    assert terrain.layer_version(1) != versions[1] and terrain.layer_version(2) != versions[2]
    assert terrain.layers()[2] is not old_boulders, "rebound to the new craters"
    assert terrain.source_version() == source_version
    with pytest.raises(ValueError):
        terrain.set_layer(3, noise)
    with pytest.raises(IndexError):
        terrain.layer_version(3)


def test_set_layer_leaves_the_terrain_unchanged_when_it_fails(moon, generated):
    terrain = generated.terrain
    before = terrain.layer_version(1)
    with pytest.raises(ValueError):
        terrain.set_layer(1, terraingen.CraterLayer(None, terraingen.CraterLayerOptions(), moon))
    assert terrain.layer_version(1) == before


def test_set_source_renews_the_source_version(paraboloid):
    terrain = terraingen.Terrain(source=paraboloid, layers=[terraingen.ScaleLayer(2.0)])
    version = terrain.source_version()
    layer_version = terrain.layer_version(0)
    terrain.set_source(terraingen.FlatSource(65, 65, 2.0, 1.5))
    assert terrain.source_version() != version and terrain.layer_version(0) == layer_version
    assert (terrain.evaluate().values == 3.0).all()
    terrain.set_source(None)
    assert terrain.source() is None and terrain.grid() is None


def test_reseeding_draws_every_seeded_part_again(generated):
    terrain = generated.terrain
    grid = generated.grids[0].grid
    first = terrain.reseed(7)
    assert_same_bits(first.evaluate(grid, threads=2), terrain.reseed(7).evaluate(grid, threads=2))
    other = terrain.reseed(8).evaluate(grid, threads=2)
    assert np.asarray(first.evaluate(grid, threads=2)).tobytes() != np.asarray(other).tobytes()
    assert_same_bits(terrain.evaluate(grid, threads=2), generated.grids[0].heights)


def test_reseeding_keeps_measured_heights_and_unseeded_layers(paraboloid):
    terrain = terraingen.Terrain(source=paraboloid, layers=[terraingen.ScaleLayer(2.0)])
    reseeded = terrain.reseed(3)
    assert_same_bits(reseeded.evaluate(), terrain.evaluate())
    assert reseeded.source() is paraboloid and reseeded.layers()[0] is terrain.layers()[0]
    assert reseeded.source_version() == terrain.source_version()
    assert reseeded.layer_version(0) == terrain.layer_version(0)


def test_layers_and_sources_say_how_they_reseed_and_rebind(ejecta_terrain):
    noise, craters, boulders = ejecta_terrain.layers()
    assert terraingen.ScaleLayer(2.0).reseeded(4) is None
    assert terraingen.FlatSource(4, 4, 1.0).reseeded(4) is None
    assert isinstance(noise.reseeded(4), terraingen.NoiseLayer)
    replacement = craters.reseeded(4)
    assert isinstance(replacement, terraingen.CraterLayer)
    assert isinstance(boulders.rebound([(craters, replacement)]), terraingen.BoulderLayer)
    assert noise.rebound([(craters, replacement)]) is None
    assert boulders.rebound([(noise, noise.reseeded(4))]) is None


class Tilt(terraingen.Ground):
    """A ground rising 1 m per meter east."""

    def fill_samples(self, samples, spacing):
        samples.heights()[:] = samples.positions()[:, 0]


def test_grounds_written_in_python_answer_point_queries():
    ground = Tilt()
    assert ground.height_at(3.0, -1.0, 1.0) == 3.0
    heights = ground.heights_at(np.array([[1.0, 0.0], [-2.0, 5.0]]), 1.0)
    assert heights.dtype == np.float32
    np.testing.assert_array_equal(heights, [1.0, -2.0])


def test_python_sources_give_a_terrain_its_heights(paraboloid):
    terrain = terraingen.Terrain(source=paraboloid, layers=[terraingen.ScaleLayer(1.0, 2.0)])
    heights = terrain.evaluate()
    assert terrain.grid().width == 65 and heights.shape == (65, 65)
    assert heights.values[32, 32] == pytest.approx(2.0)
    assert terrain.height_at(10.0, 0.0, 2.0) == pytest.approx(0.001 * 100.0 + 2.0)


def test_build_mesh_cuts_out_boxes_and_hangs_skirts():
    heights = terraingen.Terrain().evaluate(terraingen.FlatSource(6, 6, 1.0).grid())
    whole = terraingen.build_mesh(heights)
    first = heights.cell_center(1, 4)
    last = heights.cell_center(3, 2)
    box = (first[0] - 0.01, first[1] - 0.01, last[0] + 0.01, last[1] + 0.01)
    holed = terraingen.build_mesh(heights, cutouts=[box])
    skirted = terraingen.build_mesh(heights, cutouts=[box], skirt=2.0)
    assert holed.triangle_count == whole.triangle_count - 8
    assert skirted.triangle_count == holed.triangle_count + 2 * 28
