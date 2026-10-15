import numpy as np
import pytest

import terraingen


def test_surfaces_and_bodies():
    moon = terraingen.body_sphere(terraingen.Body.MOON)
    assert moon.is_sphere and moon.radius == pytest.approx(1737400.0)
    assert terraingen.mean_radius(terraingen.Body.MARS) == pytest.approx(3389500.0)
    assert moon.unit_length == pytest.approx(moon.radius * np.pi / 180.0)
    assert moon.distance((0.0, 0.0), (1.0, 0.0)) == pytest.approx(moon.unit_length)
    x, y, z = moon.position((0.0, 90.0))
    assert z == pytest.approx(moon.radius) and abs(x) < 1e-6
    east, north = moon.offset((10.0, 0.0), moon.locate((10.0, 0.0), (500.0, -200.0)))
    assert (east, north) == pytest.approx((500.0, -200.0), abs=1e-6)
    plane = terraingen.Surface()
    assert not plane.is_sphere and plane.unit_length == 1.0
    assert terraingen.Surface.plane().distance((0.0, 0.0), (3.0, 4.0)) == pytest.approx(5.0)


def test_terrain_on_a_sphere_and_body_mesh(tmp_path, lunar_models):
    moon = terraingen.body_sphere(terraingen.Body.MOON)
    noise = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=400000.0, amplitude=3000.0))], surface=moon
    )
    generation = terraingen.CraterGenerationOptions(min_diameter=20000.0, max_diameter=300000.0)
    craters = terraingen.CraterLayer(lunar_models(3.8), terraingen.CraterLayerOptions(surface=moon, generation=generation))
    site = terraingen.FlatSource(65, 65, 4000.0, centre=(23.47, 0.67), surface=moon)
    assert site.transform.pixel_width == pytest.approx(4000.0 / moon.unit_length)
    terrain = terraingen.Terrain(site, [noise, craters], surface=moon)
    assert terrain.surface.is_sphere
    evaluated = terraingen.evaluate(terrain)
    assert evaluated.heights.shape == (65, 65) and np.ptp(evaluated.heights.values) > 100.0
    centre = evaluated.heights.values[32, 32]
    assert terraingen.evaluate_at(terrain, 23.47, 0.67, 4000.0) == pytest.approx(centre, abs=1e-3)

    mesh = terraingen.body_mesh(terrain, 8)
    positions, triangles = mesh.arrays
    assert positions.shape == (mesh.vertex_count, 3) and triangles.shape == (mesh.triangle_count, 3)
    radii = np.linalg.norm(positions, axis=1)
    assert radii.min() > moon.radius - 10000.0 and radii.max() < moon.radius + 10000.0
    mesh.write_ply(tmp_path / "moon.ply")
    mesh.write_glb(tmp_path / "moon.glb")
    assert (tmp_path / "moon.glb").read_bytes()[:4] == b"glTF"
    with pytest.raises(ValueError):
        terraingen.body_mesh(terraingen.Terrain(terraingen.FlatSource(8, 8, 1.0)), 4)

    region = terraingen.RegionLayer(
        terraingen.ScaleLayer(1.0, 100.0), terraingen.RegionShape.circle(23.47, 0.67, 1.0), surface=moon
    )
    assert region.weight(23.47, 0.67) == 1.0
    rough = terraingen.RoughnessLayer(terraingen.RoughnessOptions(), surface=moon)
    boulders = terraingen.BoulderLayer(terraingen.BoulderLayerOptions(), craters, surface=moon)
    smoothing = terraingen.RimSmoothingFilter(surface=moon)
    bedforms = terraingen.BedformLayer(terraingen.mars_large_ripples(), surface=moon)
    assert all(part is not None for part in (rough, boulders, smoothing, bedforms))


def test_tiles_match_the_whole_grid(lunar_models):
    noise = terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=2, wavelength=100.0, amplitude=5.0))])
    generation = terraingen.CraterGenerationOptions(min_diameter=6.0, max_diameter=60.0)
    craters = terraingen.CraterLayer(lunar_models(3.8), terraingen.CraterLayerOptions(generation=generation))
    grid = terraingen.RasterGrid.centred(128, 128, 2.0)
    terrain = terraingen.Terrain(terraingen.FlatSource(128, 128, 2.0), [noise, craters])
    whole = terraingen.evaluate(terrain, grid=grid).heights.values
    scheme = terraingen.TileScheme.covering(grid, 64)
    level = scheme.level_for(2.0)
    assert scheme.spacing(level) == pytest.approx(2.0) and scheme.tile_size(level) == pytest.approx(128.0)
    keys = scheme.tiles_covering(grid.bounds, level)
    assert len(keys) == 4 and keys[0] == scheme.tile_at(-127.0, 127.0, level)
    tiles = terraingen.evaluate_tiles(terrain, scheme, keys)
    for key, tile in zip(keys, tiles):
        tile_grid = scheme.grid(key)
        col = int(round((tile_grid.transform.origin_x - grid.transform.origin_x) / 2.0))
        row = int(round((grid.transform.origin_y - tile_grid.transform.origin_y) / 2.0))
        np.testing.assert_array_equal(tile.heights.values, whole[row : row + 64, col : col + 64])
    single = terraingen.evaluate_tile(terrain, scheme, keys[0])
    np.testing.assert_array_equal(single.heights.values, tiles[0].heights.values)
    assert keys[0].children[0].parent == keys[0] and keys[0].level == level
    body = terraingen.TileScheme.whole_body(16)
    assert len(terraingen.TileScheme.body_tiles(1)) == 8 and body.root_size == 180.0
    custom = terraingen.TileScheme((0.0, 0.0), 100.0, cells=10)
    assert custom.spacing(1) == pytest.approx(5.0) and custom.origin == (0.0, 0.0)
    with pytest.raises(ValueError):
        terraingen.TileScheme((0.0, 0.0), -1.0)


def test_sources_and_albedo_options(tmp_path):
    coarse = terraingen.evaluate(
        terraingen.Terrain(
            terraingen.FlatSource(33, 33, 8.0),
            [terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=3, wavelength=200.0, amplitude=20.0))])],
        )
    ).heights
    terraingen.write_geotiff(tmp_path / "dem.tif", coarse)
    raster = terraingen.GeoTiffSource(tmp_path / "dem.tif", pixel_size=2.0, resampling=terraingen.ResampleMethod.BILINEAR, scale=2.0)
    assert raster.width == 132
    heights = terraingen.evaluate(terraingen.Terrain(raster)).heights.values
    assert np.nanmax(heights) == pytest.approx(2 * coarse.values.max(), rel=0.05)
    chain = terraingen.SourceChain([raster, terraingen.FlatSource(200, 200, 2.0, elevation=-5.0)])
    assert chain.width == raster.width

    detail = terraingen.RasterAlbedoOptions(method=terraingen.ResampleMethod.CUBIC, detail=terraingen.AlbedoDetail(octaves=3, seed=2))
    values = 0.12 + 0.01 * np.random.default_rng(1).random((33, 33))
    albedo_map = terraingen.Raster.from_array(values.astype(np.float32), coarse.transform)
    source = terraingen.RasterAlbedo([albedo_map], detail)
    assert 0.0 <= source.hurst <= 1.0 and source.cell_roughness > 0
    band = terraingen.evaluate_albedo(terraingen.Albedo(source), raster.grid())[0].values
    assert np.nanmean(band) == pytest.approx(0.125, abs=0.01)

    rays = terraingen.CraterRays(oblique=terraingen.ObliqueImpacts(heading=90.0), excess=0.06)
    options = terraingen.CraterAlbedoOptions(seed=3, rays=rays, retention=terraingen.RayRetention(scale=2.0), interior_excess=0.2)
    assert options.rays.oblique.heading == 90.0 and options.retention.myr(500.0) == pytest.approx(10.0)
    assert rays.median_length(2000.0) > 2000.0
    rgb = terraingen.SyntheticAlbedo(rgb=True, hue=30.0, saturation=0.3, surface=terraingen.Surface())
    assert rgb.bands() == 3
