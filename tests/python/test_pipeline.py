"""pipeline: building and evaluating terrains from configs and objects, outputs and body meshes."""

import json
import pathlib

import numpy as np
import pytest

import terraingen

SPHERE_CONFIG = {
    "schema_version": 1,
    "body": {"type": "sphere", "radius": 1737400},
    "outputs": [
        {"type": "heightmap", "path": "h.tif", "region": {"width": 17, "height": 17, "pixel_size": 100}}
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
    return terraingen.generate_terrain(terraingen.Config.from_dict(small_config), threads=2)


def test_generate_terrain_fills_finite_varied_heights(generated):
    assert generated.grids[0].heights.shape == (65, 81)
    heights = generated.grids[0].heights.values
    assert np.isfinite(heights).all() and np.ptp(heights) > 0


def test_generate_terrain_returns_the_crater_table(generated):
    craters = generated.placed_craters().table
    assert set(craters) == CRATER_COLUMNS
    assert (craters["depth_ratio"] > 0).all() and (craters["freshness"] <= 1).all()
    assert len(craters["x"]) > 0
    assert craters["diameter"].min() >= 3 and craters["diameter"].max() < 15
    assert craters["id"].dtype == np.uint64 and (craters["manual"] == 0).all()


def test_generate_terrain_returns_the_boulder_table(generated):
    placed = generated.placed_boulders()
    assert len(placed.table["x"]) > 0 and placed.table["diameter"].min() >= 0.5
    assert len(placed.models) == 0 and generated.grids[0].albedo == []


def test_generate_terrain_is_the_same_on_one_thread(generated, small_config):
    again = terraingen.generate_terrain(terraingen.Config.from_dict(small_config), threads=1)
    np.testing.assert_array_equal(again.grids[0].heights.values, generated.grids[0].heights.values)


def test_generate_terrain_on_auto_matches_the_cpu_to_float_rounding(generated, small_config):
    config = terraingen.Config.from_dict(small_config)
    auto = terraingen.generate_terrain(config, threads=2, backend=terraingen.Backend.AUTO)
    assert generated.backend == terraingen.Backend.CPU and generated.summary().endswith(" on cpu")
    assert auto.backend == terraingen.resolve_backend(terraingen.Backend.AUTO)
    np.testing.assert_allclose(
        auto.grids[0].heights.values, generated.grids[0].heights.values, rtol=1e-4, atol=1e-4
    )


def test_evaluate_takes_a_backend_and_cpu_is_exact(small_config):
    terrain = terraingen.build_terrain(terraingen.Config.from_dict(small_config))
    grid = terraingen.Config.from_dict(small_config).output_grids()[0]
    cpu = terraingen.evaluate(terrain, grid=grid).values
    np.testing.assert_array_equal(
        terraingen.evaluate(terrain, grid=grid, backend=terraingen.Backend.CPU).values, cpu
    )
    auto = terraingen.evaluate(terrain, grid=grid, backend=terraingen.Backend.AUTO).values
    np.testing.assert_allclose(auto, cpu, rtol=1e-4, atol=1e-4)


def albedo_config(small_config):
    small_config["albedo"] = terraingen.config_catalog()["albedo"]
    region = small_config["outputs"][0]["region"]
    small_config["outputs"] = small_config["outputs"] + [
        {"type": "albedo", "path": "albedo.tif", "region": region}
    ]
    return terraingen.Config.from_dict(small_config)


def test_generate_terrain_evaluates_the_albedo(tmp_path, small_config):
    terrain = terraingen.generate_terrain(albedo_config(small_config), tmp_path)
    albedo = terrain.grids[0].albedo
    assert len(albedo) >= 1 and albedo[0].shape == (65, 81)


def test_write_outputs_writes_every_output(tmp_path, small_config):
    config = albedo_config(small_config)
    written = terraingen.write_outputs(config, terraingen.generate_terrain(config, tmp_path), tmp_path)
    assert all(isinstance(path, pathlib.Path) for path in written)
    assert sorted(path.name for path in written) == ["albedo.tif", "craters.csv", "heights.tif"]
    assert (tmp_path / "craters.csv").read_text().startswith("x,y,diameter")


def test_build_terrain_keeps_the_surface_and_layers():
    terrain = terraingen.build_terrain(terraingen.Config.from_dict(SPHERE_CONFIG))
    assert terrain.surface.is_body and len(terrain.layers) == 1


def test_evaluating_a_built_terrain_matches_generate_terrain():
    config = terraingen.Config.from_dict(SPHERE_CONFIG)
    grid = config.output_grids()[0]
    heights = terraingen.evaluate(terraingen.build_terrain(config), grid=grid).values
    np.testing.assert_array_equal(heights, terraingen.generate_terrain(config).grids[0].heights.values)


def test_build_albedo_evaluates_the_configured_albedo():
    config = terraingen.Config.from_dict(SPHERE_CONFIG)
    built_albedo = terraingen.build_albedo(config, terraingen.build_terrain(config))
    assert terraingen.evaluate_albedo(built_albedo, config.output_grids()[0])[
        0
    ].values.mean() == pytest.approx(0.12)


def test_a_terrain_without_a_source_starts_at_the_datum():
    terrain = terraingen.Terrain()
    assert terrain.source is None
    grid = terraingen.FlatSource(4, 3, 2.0).grid()
    assert (terraingen.evaluate(terrain, grid=grid).values == 0.0).all()
    assert terraingen.evaluate_at(terrain, 5.0, -2.0, 1.0) == 0.0
    with pytest.raises(ValueError):
        terraingen.evaluate(terrain)
    with pytest.raises(ValueError):
        terraingen.placed_craters(terrain)


@pytest.fixture
def moon():
    return terraingen.Surface.sphere(1737400.0)


@pytest.fixture
def lunar_site(moon, lunar_models, noise_relief):
    """Noise and 20 to 300 km craters on a 65 x 65 site at 4 km near Mons Mouton's longitude."""
    noise = noise_relief(1, 400000.0, 3000.0, surface=moon)
    generation = terraingen.CraterGenerationOptions(min_diameter=20000.0, max_diameter=300000.0)
    craters = terraingen.CraterLayer(
        lunar_models(3.8), terraingen.CraterLayerOptions(surface=moon, generation=generation)
    )
    site = terraingen.FlatSource(65, 65, 4000.0, centre=(23.47, 0.67), surface=moon)
    return terraingen.Terrain(site, [noise, craters], surface=moon)


def test_a_site_on_a_sphere_has_pixels_in_degrees(moon):
    site = terraingen.FlatSource(65, 65, 4000.0, centre=(23.47, 0.67), surface=moon)
    assert site.transform.pixel_width == pytest.approx(4000.0 / moon.unit_length)


def test_terrain_on_a_sphere_has_relief(lunar_site):
    assert lunar_site.surface.is_body
    heights = terraingen.evaluate(lunar_site)
    assert heights.shape == (65, 65) and np.ptp(heights.values) > 100.0


def test_point_query_on_a_sphere_matches_the_grid(lunar_site):
    centre = terraingen.evaluate(lunar_site).values[32, 32]
    assert terraingen.evaluate_at(lunar_site, 23.47, 0.67, 4000.0) == pytest.approx(centre, abs=1e-3)


def test_point_batches_match_single_point_queries(lunar_site):
    points = np.array([[23.47, 0.67], [23.5, 0.6], [23.4, 0.7]])
    heights = terraingen.evaluate_points(lunar_site, points, 4000.0, threads=2)
    assert heights.dtype == np.float32 and heights.shape == (3,)
    for (x, y), height in zip(points, heights, strict=True):
        assert height == terraingen.evaluate_at(lunar_site, x, y, 4000.0)
    with pytest.raises(ValueError):
        terraingen.evaluate_points(lunar_site, np.zeros((2, 3)), 4000.0)


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


def test_height_mesh_has_a_vertex_per_cell(paraboloid):
    heights = terraingen.evaluate(terraingen.Terrain(paraboloid))
    mesh = terraingen.height_mesh(heights)
    assert mesh.vertex_count == heights.width * heights.height
    assert mesh.triangle_count == 2 * (heights.width - 1) * (heights.height - 1)


def test_body_mesh_needs_a_sphere():
    with pytest.raises(ValueError):
        terraingen.body_mesh(terraingen.Terrain(terraingen.FlatSource(8, 8, 1.0)), 4)


def test_generated_grids_know_their_grid_and_list_features_over_any(generated):
    (grid,) = [g.grid for g in generated.grids]
    assert (grid.width, grid.height) == (81, 65)
    half = terraingen.FlatSource(40, 32, 0.5).grid()
    assert len(generated.placed_craters(half).craters) <= len(generated.placed_craters().craters)
    assert len(generated.placed_craters(grid, spacing=4.0).craters) <= len(
        generated.placed_craters(grid).craters
    )


def test_a_hierarchy_writes_nested_grids_meshes_and_a_manifest(tmp_path):
    config = terraingen.Config.from_dict(
        {
            "schema_version": 1,
            "layers": [{"type": "noise", "stack": [{"wavelength": 40, "amplitude": 2}]}],
            "hierarchy": {
                "path": "site/region_{region}_level_{level}",
                "base": {"width": 33, "height": 33, "pixel_size": 2},
                "regions": [{"centre_x": 5, "centre_y": -3, "size": 16, "levels": 2}],
                "products": ["heightmap", "terrain_mesh"],
                "skirt": 1.0,
                "manifest": "site/manifest.json",
            },
        }
    )
    assert len(config.output_grids()) == 6
    written = terraingen.write_outputs(config, terraingen.generate_terrain(config, tmp_path), tmp_path)
    assert len(written) == 7 and written[-1].name == "manifest.json"
    manifest = json.loads((tmp_path / "site" / "manifest.json").read_text())
    assert [grid["level"] for grid in manifest["grids"]] == [0, 1, 2]
    assert manifest["grids"][2]["parent"] == 1
    assert (tmp_path / manifest["grids"][2]["files"]["terrain_mesh"]).exists()


def test_height_mesh_cuts_out_boxes_and_hangs_skirts():
    heights = terraingen.evaluate(terraingen.Terrain(None, []), grid=terraingen.FlatSource(6, 6, 1.0).grid())
    whole = terraingen.height_mesh(heights)
    first = heights.cell_center(1, 4)
    last = heights.cell_center(3, 2)
    box = (first[0] - 0.01, first[1] - 0.01, last[0] + 0.01, last[1] + 0.01)
    holed = terraingen.height_mesh(heights, cutouts=[box])
    skirted = terraingen.height_mesh(heights, cutouts=[box], skirt=2.0)
    assert holed.triangle_count == whole.triangle_count - 8
    assert skirted.triangle_count == holed.triangle_count + 2 * 28
