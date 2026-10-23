# SPDX-License-Identifier: BSD-3-Clause

"""pipeline: building and evaluating terrains from configs and objects, outputs and body meshes."""

import pathlib

import numpy as np
import pytest

import terraingen

SPHERE_CONFIG = {
    "schema_version": 1,
    "body": {"type": "sphere", "radius": 1737400},
    "outputs": [
        {
            "type": "heightmap",
            "path": "h.tif",
            "pixel_size_m": 100,
            "extent_deg": {"center": [0, 0], "size": [0.05, 0.05]},
        }
    ],
    "layers": [{"type": "noise", "stack": [{"wavelength": 2000, "amplitude": 10}]}],
    "albedo": {"source": {"type": "synthetic", "value": 0.12}},
}

CRATER_COLUMNS = {
    "x",
    "y",
    "diameter",
    "age_gyr",
    "id",
    "manual",
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


def test_generate_from_config_fills_finite_varied_heights(generated):
    assert generated.grids[0].heights.shape == (65, 81)
    heights = generated.grids[0].heights.values
    assert np.isfinite(heights).all() and np.ptp(heights) > 0


def test_generate_from_config_returns_the_crater_table(generated):
    craters = generated.placed_craters().table
    assert set(craters) == CRATER_COLUMNS
    assert (craters["depth_ratio"] > 0).all() and (craters["freshness"] <= 1).all()
    assert len(craters["x"]) > 0
    assert craters["diameter"].min() >= 3 and craters["diameter"].max() < 15
    assert craters["id"].dtype == np.uint64 and (craters["manual"] == 0).all()


def test_generate_from_config_returns_the_boulder_table(generated):
    placed = generated.placed_boulders()
    assert len(placed.table["x"]) > 0 and placed.table["diameter"].min() >= 0.5
    assert len(placed.models) == 0 and generated.grids[0].albedo == []


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
    terrain = terraingen.build_generator(terraingen.TerrainGenConfig.from_dict(small_config))
    grid = terraingen.TerrainGenConfig.from_dict(small_config).output_grids()[0]
    cpu = terraingen.evaluate(terrain, grid=grid).values
    np.testing.assert_array_equal(
        terraingen.evaluate(terrain, grid=grid, backend=terraingen.Backend.CPU).values, cpu
    )
    auto = terraingen.evaluate(terrain, grid=grid, backend=terraingen.Backend.AUTO).values
    np.testing.assert_allclose(auto, cpu, rtol=1e-4, atol=1e-4)


def test_point_queries_keep_crater_datums_in_an_evaluation_cache(small_config):
    for layer in small_config["layers"]:
        if layer["type"] == "craters":
            layer.setdefault("apply", {})["datum"] = "plane"
    terrain = terraingen.build_generator(terraingen.TerrainGenConfig.from_dict(small_config))
    points = np.column_stack([np.linspace(-20.0, 20.0, 40), np.linspace(15.0, -15.0, 40)])
    fresh = terraingen.heights_at(terrain, points, 0.5)
    cache = terraingen.EvaluationCache()
    np.testing.assert_array_equal(terraingen.heights_at(terrain, points, 0.5, cache=cache), fresh)
    np.testing.assert_array_equal(terraingen.heights_at(terrain, points, 0.5, cache=cache), fresh)
    for (x, y), height in zip(points[::7], fresh[::7], strict=True):
        assert terraingen.height_at(terrain, x, y, 0.5, cache=cache) == height
    assert terraingen.height_at(terrain, *points[3], 0.5, cache=terraingen.EvaluationCache(16)) == fresh[3]


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
    assert np.array_equal(grid.craters.table["id"], outputs.placed_craters().table["id"])
    assert len(grid.boulders.boulders) == len(outputs.placed_boulders().boulders)
    assert grid.mesh.positions.shape == (65 * 81, 3)
    assert grid.albedo == [] and grid.materials == [] and grid.photometry == []
    assert terraingen.write_outputs(config, outputs, tmp_path) == []
    assert list(tmp_path.iterdir()) == []


def test_generate_from_config_needs_an_extent_without_a_raster_source(small_config):
    del small_config["outputs"][0]["extent_m"]
    with pytest.raises(terraingen.ConfigError) as missing:
        terraingen.generate_from_config(terraingen.TerrainGenConfig.from_dict(small_config))
    assert missing.value.path == "outputs[0].extent_m"


def test_build_generator_keeps_the_surface_and_layers():
    terrain = terraingen.build_generator(terraingen.TerrainGenConfig.from_dict(SPHERE_CONFIG))
    assert terrain.surface.is_body and len(terrain.layers) == 1


def test_evaluating_a_built_generator_matches_generate_from_config():
    config = terraingen.TerrainGenConfig.from_dict(SPHERE_CONFIG)
    grid = config.output_grids()[0]
    heights = terraingen.evaluate(terraingen.build_generator(config), grid=grid).values
    np.testing.assert_array_equal(heights, terraingen.generate_from_config(config).grids[0].heights.values)


def test_build_albedo_evaluates_the_configured_albedo():
    config = terraingen.TerrainGenConfig.from_dict(SPHERE_CONFIG)
    built_albedo = terraingen.build_albedo(config, terraingen.build_generator(config))
    assert terraingen.evaluate_albedo(built_albedo, config.output_grids()[0])[
        0
    ].values.mean() == pytest.approx(0.12)


def test_a_terrain_without_a_source_starts_at_the_datum():
    terrain = terraingen.TerrainGenerator()
    assert terrain.source is None
    grid = terraingen.FlatSource(4, 3, 2.0).grid()
    assert (terraingen.evaluate(terrain, grid=grid).values == 0.0).all()
    assert terraingen.height_at(terrain, 5.0, -2.0, 1.0) == 0.0
    with pytest.raises(ValueError):
        terraingen.evaluate(terrain)
    with pytest.raises(ValueError):
        terraingen.placed_craters(terrain)


@pytest.fixture
def lunar_site(moon, lunar_models, noise_relief):
    """Noise and 20 to 300 km craters on a 65 x 65 site at 4 km near Mons Mouton's longitude."""
    noise = noise_relief(1, 400000.0, 3000.0, surface=moon)
    generation = terraingen.CraterGenerationOptions(d_min=20000.0, d_max=300000.0)
    craters = terraingen.CraterLayer(
        lunar_models(3.8), terraingen.CraterLayerOptions(surface=moon, generation=generation)
    )
    site = terraingen.FlatSource(65, 65, 4000.0, center=(23.47, 0.67), surface=moon)
    return terraingen.TerrainGenerator(site, [noise, craters], surface=moon)


def test_a_site_on_a_sphere_has_pixels_in_degrees(moon):
    site = terraingen.FlatSource(65, 65, 4000.0, center=(23.47, 0.67), surface=moon)
    assert site.transform.pixel_width == pytest.approx(4000.0 / moon.unit_length)


def test_terrain_on_a_sphere_has_relief(lunar_site):
    assert lunar_site.surface.is_body
    heights = terraingen.evaluate(lunar_site)
    assert heights.shape == (65, 65) and np.ptp(heights.values) > 100.0


def test_point_query_on_a_sphere_matches_the_grid(lunar_site):
    center = terraingen.evaluate(lunar_site).values[32, 32]
    assert terraingen.height_at(lunar_site, 23.47, 0.67, 4000.0) == pytest.approx(center, abs=1e-3)


def test_point_batches_match_single_point_queries(lunar_site):
    points = np.array([[23.47, 0.67], [23.5, 0.6], [23.4, 0.7]])
    heights = terraingen.heights_at(lunar_site, points, 4000.0, threads=2)
    assert heights.dtype == np.float32 and heights.shape == (3,)
    for (x, y), height in zip(points, heights, strict=True):
        assert height == terraingen.height_at(lunar_site, x, y, 4000.0)
    with pytest.raises(ValueError):
        terraingen.heights_at(lunar_site, np.zeros((2, 3)), 4000.0)


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
    heights = terraingen.evaluate(terraingen.TerrainGenerator(paraboloid))
    mesh = terraingen.build_mesh(heights)
    assert mesh.vertex_count == heights.width * heights.height
    assert mesh.triangle_count == 2 * (heights.width - 1) * (heights.height - 1)
    assert not hasattr(terraingen, "height_mesh")


def test_build_mesh_views_its_arrays_without_copying(paraboloid):
    heights = terraingen.evaluate(terraingen.TerrainGenerator(paraboloid))
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
    heights = terraingen.evaluate(terraingen.TerrainGenerator(paraboloid))
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
    spherical = terraingen.evaluate(terraingen.TerrainGenerator(site, surface=moon))
    mesh = terraingen.build_mesh(spherical, surface=moon, site=(23.47, 0.67), skirt=3.0)
    _, positions, triangles = _fill(spherical, surface=moon, site=(23.47, 0.67), skirt=3.0)
    assert positions.tobytes() == mesh.positions.tobytes()
    np.testing.assert_array_equal(triangles, mesh.triangles.astype(np.int32))


def test_fill_mesh_rejects_arrays_it_cannot_fill_in_place(paraboloid):
    heights = terraingen.evaluate(terraingen.TerrainGenerator(paraboloid))
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


def test_fill_heights_writes_what_evaluate_gives_in_place(paraboloid):
    terrain = terraingen.TerrainGenerator(paraboloid)
    grid = paraboloid.grid()
    expected = terraingen.evaluate(terrain, spacing=2.0, threads=2, grid=grid)
    out = np.full((grid.height, grid.width), np.nan, dtype=np.float32)
    address = out.__array_interface__["data"][0]
    assert terraingen.fill_heights(terrain, grid, out, spacing=2.0, threads=2) is None
    assert out.__array_interface__["data"][0] == address
    assert out.tobytes() == np.asarray(expected).tobytes()
    with pytest.raises(ValueError):
        terraingen.fill_heights(terrain, grid, np.zeros((grid.width, grid.height + 1), dtype=np.float32))
    with pytest.raises(TypeError):
        terraingen.fill_heights(terrain, grid, np.zeros((grid.height, grid.width)))


def test_body_mesh_needs_a_sphere():
    with pytest.raises(ValueError):
        terraingen.body_mesh(terraingen.TerrainGenerator(terraingen.FlatSource(8, 8, 1.0)), 4)


def test_generated_grids_know_their_grid_and_list_features_over_any(generated):
    (grid,) = [g.grid for g in generated.grids]
    assert (grid.width, grid.height) == (81, 65)
    half = terraingen.FlatSource(40, 32, 0.5).grid()
    assert len(generated.placed_craters(half).craters) <= len(generated.placed_craters().craters)
    assert len(generated.placed_craters(grid, spacing=4.0).craters) <= len(
        generated.placed_craters(grid).craters
    )


def test_build_mesh_cuts_out_boxes_and_hangs_skirts():
    heights = terraingen.evaluate(
        terraingen.TerrainGenerator(None, []), grid=terraingen.FlatSource(6, 6, 1.0).grid()
    )
    whole = terraingen.build_mesh(heights)
    first = heights.cell_center(1, 4)
    last = heights.cell_center(3, 2)
    box = (first[0] - 0.01, first[1] - 0.01, last[0] + 0.01, last[1] + 0.01)
    holed = terraingen.build_mesh(heights, cutouts=[box])
    skirted = terraingen.build_mesh(heights, cutouts=[box], skirt=2.0)
    assert holed.triangle_count == whole.triangle_count - 8
    assert skirted.triangle_count == holed.triangle_count + 2 * 28
