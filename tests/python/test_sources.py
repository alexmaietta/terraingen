"""sources: flat, GeoTIFF, chained, synthetic and Python height sources."""

import numpy as np
import pytest

import terraingen


def synthetic_params():
    return terraingen.SyntheticTerrainParams(
        [
            terraingen.NoiseLayer(terraingen.FractalParams(seed=3, wavelength=50.0)),
            terraingen.NoiseLayer(
                terraingen.FractalParams(
                    basis=terraingen.NoiseBasis.SIMPLEX,
                    type=terraingen.FractalType.RIDGED,
                    wavelength=10.0,
                    amplitude=2.0,
                )
            ),
        ]
    )


@pytest.fixture
def coarse_dem(tmp_path, noise_relief):
    """A 33 x 33 noise DEM at 8 m written to dem.tif; returns its heights."""
    terrain = terraingen.Terrain(terraingen.FlatSource(33, 33, 8.0), [noise_relief(3, 200.0, 20.0)])
    coarse = terraingen.evaluate(terrain)
    terraingen.write_geotiff(tmp_path / "dem.tif", coarse)
    return coarse


def resampled_source(tmp_path):
    return terraingen.GeoTiffSource(
        tmp_path / "dem.tif", pixel_size=2.0, resampling=terraingen.ResampleMethod.BILINEAR, scale=2.0
    )


def test_flat_source_describes_its_grid():
    source = terraingen.FlatSource(65, 33, 2.0, elevation=5.0, centre=(100.0, -50.0))
    assert (source.width, source.height) == (65, 33)
    assert source.transform.pixel_width == 2.0
    assert source.height_at(100.0, -50.0) == 5.0


def test_flat_source_rejects_an_empty_grid():
    with pytest.raises(ValueError):
        terraingen.FlatSource(0, 10, 1.0)


def test_flat_source_on_a_body_takes_its_cell_size_in_metres():
    moon = terraingen.Surface.sphere(1737400.0)
    source = terraingen.FlatSource(9, 9, 30.0, centre=(10.0, -20.0), surface=moon)
    assert source.transform.pixel_width == pytest.approx(30.0 / moon.unit_length)


def test_python_source_without_its_methods_raises_type_error():
    class Empty(terraingen.Source):
        pass

    with pytest.raises(TypeError, match="Empty must define grid"):
        Empty().grid()


def test_geotiff_source_resamples_to_its_pixel_size(tmp_path, coarse_dem):
    assert resampled_source(tmp_path).width == 132


def test_geotiff_source_scales_heights(tmp_path, coarse_dem):
    heights = terraingen.evaluate(terraingen.Terrain(resampled_source(tmp_path))).values
    assert np.nanmax(heights) == pytest.approx(2 * coarse_dem.values.max(), rel=0.05)


def test_source_chain_takes_the_grid_of_its_first_source(tmp_path, coarse_dem):
    raster = resampled_source(tmp_path)
    chain = terraingen.SourceChain([raster, terraingen.FlatSource(200, 200, 2.0, elevation=-5.0)])
    assert chain.width == raster.width


def test_python_source_describes_its_grid(paraboloid):
    assert paraboloid.width == 65 and paraboloid.reference_point() == (0.0, 0.0)


def test_python_source_fills_the_heights(paraboloid):
    plain = terraingen.evaluate(terraingen.Terrain(paraboloid)).values
    assert plain[32, 32] == pytest.approx(0.0, abs=1e-6) and plain[0, 0] > 4.0


def test_synthetic_terrain_is_centred_on_the_origin():
    raster = terraingen.generate_synthetic_terrain(17, 9, 1.5, synthetic_params())
    assert raster.shape == (9, 17)
    assert raster.transform == terraingen.GeoTransform.north_up(-12.75, 6.75, 1.5)


def test_synthetic_terrain_matches_fill():
    params = synthetic_params()
    raster = terraingen.generate_synthetic_terrain(17, 9, 1.5, params)
    filled = terraingen.Raster(17, 9)
    filled.transform = raster.transform
    terraingen.fill_synthetic_terrain(filled, params)
    np.testing.assert_array_equal(raster.values, filled.values)


def test_synthetic_terrain_samples_the_noise_layers_at_cell_centres():
    params = synthetic_params()
    raster = terraingen.generate_synthetic_terrain(17, 9, 1.5, params)
    cols, rows = np.meshgrid(np.arange(17), np.arange(9))
    x = (-12.75 + (cols + 0.5) * 1.5).astype(np.float32)
    y = (6.75 - (rows + 0.5) * 1.5).astype(np.float32)
    np.testing.assert_array_equal(raster.values, terraingen.evaluate_noise_layers(x, y, params))


def test_synthetic_terrain_rejects_an_empty_grid():
    with pytest.raises(ValueError):
        terraingen.generate_synthetic_terrain(0, 4, 1.0, terraingen.SyntheticTerrainParams())


def test_a_low_passed_dem_plus_its_detail_restores_it(tmp_path, coarse_dem):
    full = terraingen.GeoTiffSource(tmp_path / "dem.tif")
    smooth = terraingen.GeoTiffSource(tmp_path / "dem.tif", low_pass=24.0)
    low = terraingen.evaluate(terraingen.Terrain(smooth)).values
    assert not np.array_equal(low, coarse_dem.values)
    detail = terraingen.DetailLayer(full, smooth)
    restored = terraingen.evaluate(terraingen.Terrain(smooth, [detail])).values
    np.testing.assert_allclose(restored, coarse_dem.values, atol=1e-3)


def test_raster_fields_interpolate_and_fall_back():
    raster = terraingen.Raster.from_array(np.array([[1.0, 3.0], [5.0, np.nan]], dtype=np.float32))
    raster.transform = terraingen.GeoTransform.north_up(0.0, 2.0, 1.0)
    field = terraingen.RasterField(raster, terraingen.RasterFieldOptions(fallback=-1.0))
    assert field.value_at(1.0, 1.5) == pytest.approx(2.0)
    assert field.value_at(9.0, 9.0) == -1.0 and field.max() == 5.0
    assert terraingen.ConstantField(2.0).value_at(0.0, 0.0) == 2.0


def test_fields_can_be_written_in_python():
    class East(terraingen.SurfaceField):
        def value_at(self, x, y):
            return 1.0 if x >= 0.0 else 0.0

        def max(self):
            return 1.0

    options = terraingen.CraterGenerationOptions(min_diameter=4.0, max_diameter=32.0)
    east = East()
    options.keep_share = lambda crater: east.value_at(crater.x, crater.y)
    craters = terraingen.generate_craters(
        terraingen.PlanarCells(),
        (-200.0, -100.0, 200.0, 100.0),
        terraingen.PowerLawSfd(0.05, 2.0),
        terraingen.FreshAges(),
        1,
        options,
    )
    assert len(craters) > 0 and min(c.x for c in craters) >= 0.0


def test_raster_source_matches_a_geotiff_source_of_the_same_heights(tmp_path, coarse_dem):
    from_file = terraingen.GeoTiffSource(tmp_path / "dem.tif", pixel_size=2.0, low_pass=10.0)
    in_memory = terraingen.RasterSource(
        coarse_dem.values, coarse_dem.transform, pixel_size=2.0, low_pass=10.0
    )
    from_raster = terraingen.RasterSource(coarse_dem, pixel_size=2.0, low_pass=10.0)
    expected = terraingen.evaluate(terraingen.Terrain(from_file)).values
    np.testing.assert_array_equal(terraingen.evaluate(terraingen.Terrain(in_memory)).values, expected)
    np.testing.assert_array_equal(terraingen.evaluate(terraingen.Terrain(from_raster)).values, expected)
    x, y = coarse_dem.cell_center(10, 12)
    assert in_memory.height_at(x + 1.3, y - 0.7) == from_file.height_at(x + 1.3, y - 0.7)
    assert in_memory.resolution_at(x, y) == 20.0 and from_file.resolution_at(x, y) == 20.0
    assert in_memory.resolution_at(-1e6, 0.0) == float("inf")


def test_raster_source_honours_nodata_and_rejects_bad_options():
    heights = np.arange(12, dtype=np.float32).reshape(3, 4)
    heights[1, 2] = -9999.0
    source = terraingen.RasterSource(heights, terraingen.GeoTransform.north_up(0.0, 3.0, 1.0), nodata=-9999.0)
    assert np.isnan(source.height_at(2.5, 1.5)) and source.height_at(0.5, 2.5) == 0.0
    with pytest.raises(ValueError):
        terraingen.RasterSource(heights, pixel_size=0.0)
    with pytest.raises(ValueError):
        terraingen.RasterSource(heights, low_pass=-1.0)


def test_geotiff_source_region_reads_the_same_heights_over_its_grid(tmp_path, coarse_dem):
    full = terraingen.GeoTiffSource(tmp_path / "dem.tif", pixel_size=2.0)
    left, top = full.transform.origin_x, full.transform.origin_y
    region = (left + 40.0, top - 120.0, left + 100.0, top - 60.0)
    part = terraingen.GeoTiffSource(tmp_path / "dem.tif", pixel_size=2.0, region=region)
    assert (part.width, part.height) == (30, 30)
    grid = part.grid()
    expected = terraingen.evaluate(terraingen.Terrain(full), grid=grid).values
    np.testing.assert_array_equal(terraingen.evaluate(terraingen.Terrain(part), grid=grid).values, expected)
    assert np.isnan(part.height_at(left + 1.0, top - 1.0))
    with pytest.raises(ValueError):
        terraingen.GeoTiffSource(tmp_path / "dem.tif", region=(1e7, 1e7, 1e7 + 1.0, 1e7 + 1.0))


def test_unresolved_crater_limit_drops_the_craters_a_dem_resolves():
    heights = np.zeros((26, 26), dtype=np.float32)
    dem = terraingen.RasterSource(heights, terraingen.GeoTransform.north_up(-65.0, 65.0, 5.0), pixel_size=2.0)
    limit = terraingen.unresolved_crater_limit(dem, 4.0)
    assert limit.value_at(0.0, 0.0) == 20.0 and limit.value_at(1e6, 0.0) == float("inf")
    both = terraingen.MinimumField(limit, terraingen.ConstantField(12.0))
    assert both.value_at(0.0, 0.0) == 12.0 and both.value_at(1e6, 0.0) == 12.0
    options = terraingen.CraterGenerationOptions(min_diameter=6.0, max_diameter=40.0)
    region = (-60.0, -60.0, 60.0, 60.0)
    density = terraingen.PowerLawSfd(0.05, 2.0)

    def generate():
        return terraingen.generate_craters(
            terraingen.PlanarCells(), region, density, terraingen.FreshAges(), 3, options
        )

    every = generate()
    options.completeness = limit
    kept = generate()
    assert any(c.diameter >= 20.0 for c in every)
    assert kept and all(c.diameter < 20.0 for c in kept)
    with pytest.raises(ValueError):
        terraingen.unresolved_crater_limit(dem, 0.0)


def test_python_sources_resolve_nothing_unless_they_say():
    class Level(terraingen.Source):
        def grid(self):
            return terraingen.FlatSource(4, 4, 1.0).grid()

        def height_at(self, x, y):
            return 0.0

    class Measured(Level):
        def resolution_at(self, x, y):
            return 3.0

    assert Level().resolution_at(0.0, 0.0) == float("inf")
    assert Measured().resolution_at(0.0, 0.0) == 3.0
    assert terraingen.unresolved_crater_limit(Measured(), 2.0).value_at(0.0, 0.0) == 6.0
