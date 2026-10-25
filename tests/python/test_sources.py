# SPDX-License-Identifier: BSD-3-Clause

"""sources: flat, GeoTIFF, chained, synthetic and Python height sources."""

import numpy as np
import pytest

import terraingen


def synthetic_fractals():
    return [
        terraingen.Fractal(seed=3, wavelength=50.0),
        terraingen.Fractal(
            basis=terraingen.NoiseBasis.SIMPLEX,
            type=terraingen.FractalType.RIDGED,
            wavelength=10.0,
            amplitude=2.0,
        ),
    ]


@pytest.fixture
def coarse_dem(tmp_path, noise_layer):
    """A 33 x 33 noise DEM at 8 m written to dem.tif; returns its heights."""
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(33, 33, 8.0), [noise_layer(3, 200.0, 20.0)])
    coarse = terraingen.evaluate(terrain)
    terraingen.write_geotiff(tmp_path / "dem.tif", coarse)
    return coarse


def bilinear_source(tmp_path):
    return terraingen.GeoTiffSource(
        tmp_path / "dem.tif",
        upsampler=terraingen.KernelUpsampler(terraingen.ResampleMethod.BILINEAR),
        scale=2.0,
    )


def fine_grid(source, pixel_size=2.0):
    """The source's grid divided into cells of `pixel_size`."""
    transform = source.transform
    factor = transform.pixel_width / pixel_size
    return terraingen.RasterGrid(
        round(source.width * factor),
        round(source.height * factor),
        terraingen.GeoTransform.north_up(transform.origin_x, transform.origin_y, pixel_size),
    )


def test_flat_source_describes_its_grid():
    source = terraingen.FlatSource(65, 33, 2.0, elevation=5.0, center=(100.0, -50.0))
    assert (source.width, source.height) == (65, 33)
    assert source.transform.pixel_width == 2.0
    assert source.height_at(100.0, -50.0, 1.0) == 5.0 and source.bounded


def test_flat_source_rejects_an_empty_grid():
    with pytest.raises(ValueError):
        terraingen.FlatSource(0, 10, 1.0)


def test_flat_source_on_a_body_takes_its_cell_size_in_meters():
    moon = terraingen.Surface.sphere(1737400.0)
    source = terraingen.FlatSource(9, 9, 30.0, center=(10.0, -20.0), surface=moon)
    assert source.transform.pixel_width == pytest.approx(30.0 / moon.unit_length)


def test_python_source_without_its_methods_raises_type_error():
    class Empty(terraingen.Source):
        pass

    with pytest.raises(TypeError, match="Empty must define grid"):
        Empty().grid()


def test_geotiff_source_upsamples_onto_finer_grids(tmp_path, coarse_dem):
    source = bilinear_source(tmp_path)
    assert source.width == 33
    grid = fine_grid(source)
    heights = terraingen.evaluate(terraingen.TerrainGenerator(source), grid=grid).values
    assert heights.shape == (132, 132)
    t = grid.transform
    expected = [
        [
            terraingen.sample_raster(
                coarse_dem,
                t.origin_x + (col + 0.5) * t.pixel_width,
                t.origin_y + (row + 0.5) * t.pixel_height,
                terraingen.ResampleMethod.BILINEAR,
            )
            for col in range(grid.width)
        ]
        for row in range(grid.height)
    ]
    np.testing.assert_allclose(heights, 2 * np.array(expected), rtol=1e-5, atol=1e-4)


def test_geotiff_source_scales_heights(tmp_path, coarse_dem):
    heights = terraingen.evaluate(terraingen.TerrainGenerator(bilinear_source(tmp_path))).values
    assert np.nanmax(heights) == pytest.approx(2 * coarse_dem.values.max(), rel=1e-5)


def test_source_chain_takes_the_grid_of_its_first_source(tmp_path, coarse_dem):
    raster = bilinear_source(tmp_path)
    chain = terraingen.SourceChain([raster, terraingen.FlatSource(200, 200, 2.0, elevation=-5.0)])
    assert chain.width == raster.width


def holed_plane():
    """A 32 x 32 raster of 1 m cells around the origin holding 0.1 x + 0.05 y + 8, NaN in a 6 m hole."""
    x = -15.5 + np.arange(32.0)
    y = 15.5 - np.arange(32.0)
    heights = (0.1 * x[np.newaxis, :] + 0.05 * y[:, np.newaxis] + 8.0).astype(np.float32)
    heights[13:19, 13:19] = np.nan
    return terraingen.RasterSource(heights, terraingen.GeoTransform.north_up(-16.0, 16.0, 1.0))


def test_source_chain_seams_shift_the_later_source_to_meet_the_earlier_one():
    dem = holed_plane()
    datum = terraingen.FlatSource(1, 1, 1.0, elevation=7.5)
    assert terraingen.SourceChain([dem, datum]).seams == terraingen.ChainSeams()
    assert terraingen.ChainSeams().mode == terraingen.ChainSeamMode.NONE
    assert terraingen.SourceChain([dem, datum]).height_at(0.5, 0.5, 1.0) == 7.5
    seams = terraingen.ChainSeams(mode=terraingen.ChainSeamMode.HEIGHT, decay_length=20.0)
    chain = terraingen.SourceChain([dem, datum], seams=seams)
    assert chain.seams == seams
    for x, y in [(0.5, 0.5), (-2.5, 1.5), (2.5, -2.5)]:
        assert chain.height_at(x, y, 1.0) == pytest.approx(0.1 * x + 0.05 * y + 8.0, abs=0.05)
    assert chain.height_at(90.0, 0.0, 1.0) == 7.5, "nothing beyond the reach of the open edge"
    raster = terraingen.Raster(40, 40)
    raster.transform = terraingen.GeoTransform.north_up(-20.0, 20.0, 1.0)
    chain.fill(raster, 0.5)
    centers = [(-19.5 + col, 19.5 - row) for row in range(40) for col in range(40)]
    expected = np.array([chain.height_at(x, y, 0.5) for x, y in centers], dtype=np.float32)
    np.testing.assert_array_equal(raster.values, expected.reshape(40, 40))


def test_chain_seams_validate_and_give_their_lattice():
    seams = terraingen.ChainSeams(mode=terraingen.ChainSeamMode.SLOPE, decay_length=4.0)
    seams.validate()
    assert terraingen.seam_reach(seams) == terraingen.SEAM_REACH_DECAYS * 4.0
    lattice = terraingen.seam_lattice(holed_plane().grid(), seams)
    assert (lattice.width, lattice.height) == (62, 62)
    with pytest.raises(ValueError, match="width"):
        terraingen.validate_chain_seams(terraingen.ChainSeams(width=0.0))
    with pytest.raises(ValueError):
        terraingen.SourceChain(
            [holed_plane(), terraingen.FlatSource(1, 1, 1.0)], terraingen.ChainSeams(decay_length=-1.0)
        )
    config = terraingen.TerrainGenConfig.from_dict(
        {
            "schema_version": 1,
            "source": {
                "type": "chain",
                "sources": [{"type": "synthetic"}, {"type": "synthetic"}],
                "seams": {"mode": "feather", "width_m": 12},
            },
        }
    )
    assert config.to_dict()["source"]["seams"] == {"mode": "feather", "width_m": 12.0, "decay_length_m": 50.0}


def test_python_source_describes_its_grid(paraboloid):
    assert paraboloid.width == 65 and paraboloid.reference_point() == (0.0, 0.0)


def test_python_source_fills_the_heights(paraboloid):
    plain = terraingen.evaluate(terraingen.TerrainGenerator(paraboloid)).values
    assert plain[32, 32] == pytest.approx(0.0, abs=1e-6) and plain[0, 0] > 4.0


def test_synthetic_source_samples_the_fractals_at_cell_centers():
    fractals = synthetic_fractals()
    raster = terraingen.Raster(17, 9)
    raster.transform = terraingen.GeoTransform.north_up(-12.75, 6.75, 1.5)
    terraingen.SyntheticSource(fractals).fill(raster)
    cols, rows = np.meshgrid(np.arange(17), np.arange(9))
    x = (-12.75 + (cols + 0.5) * 1.5).astype(np.float32)
    y = (6.75 - (rows + 0.5) * 1.5).astype(np.float32)
    np.testing.assert_array_equal(raster.values, terraingen.evaluate_fractals(x, y, fractals))


def test_synthetic_sources_evaluate_noise_or_upsample_it_from_posts():
    fractals = synthetic_fractals()
    noise = terraingen.SyntheticSource(fractals, elevation=2.0)
    assert noise.fractals == fractals
    assert not noise.bounded and noise.upsampler is None
    assert noise.resolution_at(0.0, 0.0) == float("inf")
    expected = 2.0 + float(terraingen.evaluate_fractals(np.float32([7.25]), np.float32([-3.5]), fractals)[0])
    assert noise.height_at(7.25, -3.5, 1.0) == pytest.approx(expected, rel=1e-6)
    with pytest.raises(ValueError):
        terraingen.evaluate(terraingen.TerrainGenerator(noise))
    grid = terraingen.RasterGrid(16, 8, terraingen.GeoTransform.north_up(-8.0, 4.0, 1.0))
    heights = terraingen.evaluate(terraingen.TerrainGenerator(noise), grid=grid).values
    assert heights[0, 0] == noise.height_at(-7.5, 3.5, 1.0)
    posts = terraingen.SyntheticSource(fractals, pixel_size=4.0)
    assert posts.resolution_at(0.0, 0.0) == 4.0
    assert isinstance(posts.upsampler, terraingen.KernelUpsampler)
    assert posts.height_at(8.0, -4.0, 0.5) == posts.height_at(8.0, -4.0, 4.0)
    flat = terraingen.SyntheticSource()
    assert flat.height_at(123.0, 4.0, 1.0) == 0.0
    with pytest.raises(ValueError):
        terraingen.SyntheticSource(fractals, upsampler=terraingen.KernelUpsampler())


def test_upsamplers_written_in_python_refine_a_raster_source():
    class Lifted(terraingen.Upsampler):
        def height_at(self, posts, x, y, spacing):
            return terraingen.KernelUpsampler().height_at(posts, x, y, spacing) + 1.0

    heights = np.arange(16, dtype=np.float32).reshape(4, 4)
    source = terraingen.RasterSource(
        heights, terraingen.GeoTransform.north_up(0.0, 4.0, 1.0), upsampler=Lifted()
    )
    assert source.height_at(0.5, 3.5, 1.0) == 0.0
    assert source.height_at(0.5, 3.5, 0.5) == 1.0
    assert source.height_at(2.0, 3.5, 0.5) == pytest.approx(2.5)
    posts = terraingen.RasterPosts(terraingen.Raster.from_array(heights))
    np.testing.assert_array_equal(posts.read(-1, 0, 2, 1), [[0.0, 0.0]])
    assert posts.extent() == (0, 4, 0, 4)


def test_fills_written_in_python_take_the_threads_and_python_fills_run_on_threads():
    class Level(terraingen.Source):
        threads = []

        def grid(self):
            return terraingen.RasterGrid(8, 8, terraingen.GeoTransform.north_up(0.0, 8.0, 1.0))

        def height_at(self, x, y, spacing):
            return 3.0

        def fill(self, heights, spacing, threads):
            Level.threads.append(threads)
            heights.values[:] = 3.0

    level = Level()
    terraingen.evaluate(terraingen.TerrainGenerator(level), threads=2)
    assert Level.threads == [2]

    class Lifted(terraingen.Upsampler):
        def height_at(self, posts, x, y, spacing):
            return terraingen.KernelUpsampler().height_at(posts, x, y, spacing) + 1.0

    heights = np.arange(64, dtype=np.float32).reshape(8, 8)
    posts = terraingen.RasterPosts(terraingen.Raster.from_array(heights))
    serial = terraingen.Raster(32, 32)
    serial.transform = terraingen.GeoTransform.north_up(0.0, 8.0, 0.25)
    every = terraingen.Raster(32, 32)
    every.transform = serial.transform
    Lifted().fill(posts, serial, 0.25, threads=1)
    Lifted().fill(posts, every, 0.25)
    np.testing.assert_array_equal(serial.values, every.values)
    assert serial.values[0, 0] == Lifted().height_at(posts, 0.125, 7.875, 0.25)


def test_kernel_upsamplers_match_sample_raster():
    raster = terraingen.Raster.from_array(
        np.random.default_rng(1).random((6, 7), dtype=np.float32),
        terraingen.GeoTransform.north_up(0.0, 6.0, 1.0),
    )
    posts = terraingen.RasterPosts(raster)
    for method in terraingen.ResampleMethod:
        upsampler = terraingen.KernelUpsampler(method)
        assert upsampler.height_at(posts, 2.3, 3.1, 0.1) == terraingen.sample_raster(raster, 2.3, 3.1, method)
    assert terraingen.kernel_upsampler_names()[2] == "cubic"
    cubic = terraingen.sample_raster(raster, 2.3, 3.1, terraingen.ResampleMethod.CUBIC)
    assert terraingen.default_upsampler().height_at(posts, 2.3, 3.1, 0.1) == cubic


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

    options = terraingen.CraterGenerationOptions(d_min=4.0, d_max=32.0)
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
    upsampler = terraingen.DiamondSquareUpsampler(
        terraingen.DiamondSquareOptions(fractal_dimension=2.4, scale=0.5)
    )
    from_file = terraingen.GeoTiffSource(tmp_path / "dem.tif", effective_pixel_size=20.0, upsampler=upsampler)
    in_memory = terraingen.RasterSource(
        coarse_dem.values, coarse_dem.transform, effective_pixel_size=20.0, upsampler=upsampler
    )
    from_raster = terraingen.RasterSource(coarse_dem, effective_pixel_size=20.0, upsampler=upsampler)
    grid = fine_grid(from_file)
    expected = terraingen.evaluate(terraingen.TerrainGenerator(from_file), grid=grid).values
    np.testing.assert_array_equal(
        terraingen.evaluate(terraingen.TerrainGenerator(in_memory), grid=grid).values, expected
    )
    np.testing.assert_array_equal(
        terraingen.evaluate(terraingen.TerrainGenerator(from_raster), grid=grid).values, expected
    )
    x, y = coarse_dem.cell_center(10, 12)
    assert in_memory.height_at(x + 1.3, y - 0.7, 0.5) == from_file.height_at(x + 1.3, y - 0.7, 0.5)
    assert in_memory.resolution_at(x, y) == 20.0 and from_file.resolution_at(x, y) == 20.0
    assert in_memory.resolution_at(-1e6, 0.0) == float("inf")
    assert in_memory.upsampler.options.scale == 0.5


def test_raster_source_honors_nodata_and_rejects_bad_options():
    heights = np.arange(12, dtype=np.float32).reshape(3, 4)
    heights[1, 2] = -9999.0
    source = terraingen.RasterSource(
        heights, terraingen.GeoTransform.north_up(0.0, 3.0, 1.0), no_data=-9999.0
    )
    assert np.isnan(source.height_at(2.5, 1.5, 1.0)) and source.height_at(0.5, 2.5, 1.0) == 0.0
    with pytest.raises(ValueError):
        terraingen.RasterSource(heights, effective_pixel_size=0.0)


def test_geotiff_source_crop_reads_the_same_heights_over_its_grid(tmp_path, coarse_dem):
    full = terraingen.GeoTiffSource(tmp_path / "dem.tif")
    left, top = full.transform.origin_x, full.transform.origin_y
    crop = (left + 40.0, top - 120.0, left + 100.0, top - 60.0)
    part = terraingen.GeoTiffSource(tmp_path / "dem.tif", crop=crop)
    assert (part.width, part.height) == (8, 8)
    grid = fine_grid(part)
    expected = terraingen.evaluate(terraingen.TerrainGenerator(full), grid=grid).values
    np.testing.assert_array_equal(
        terraingen.evaluate(terraingen.TerrainGenerator(part), grid=grid).values, expected
    )
    assert np.isnan(part.height_at(left + 1.0, top - 1.0, 2.0))
    with pytest.raises(ValueError):
        terraingen.GeoTiffSource(tmp_path / "dem.tif", crop=(1e7, 1e7, 1e7 + 1.0, 1e7 + 1.0))


def test_geotiff_source_reads_longitude_and_latitude_on_a_body(tmp_path):
    heights = np.arange(32, dtype=np.float32).reshape(4, 8)
    raster = terraingen.Raster.from_array(
        heights, terraingen.GeoTransform.from_gdal((-180.0, 45.0, 0.0, 90.0, 0.0, -45.0))
    )
    terraingen.write_geotiff(tmp_path / "moon.tif", raster, terraingen.geographic_sphere_keys(1737400.0))
    source = terraingen.GeoTiffSource(tmp_path / "moon.tif")
    assert source.resolution_at(-157.5, 67.5) == 45.0
    moon = terraingen.TerrainGenerator(source, [], surface=terraingen.Surface.sphere(1737400.0))
    np.testing.assert_array_equal(terraingen.evaluate(moon).values, heights)
    with pytest.raises(ValueError, match="longitude and latitude"):
        terraingen.evaluate(terraingen.TerrainGenerator(source))


def test_source_resolved_limit_drops_the_craters_a_dem_resolves():
    heights = np.zeros((26, 26), dtype=np.float32)
    dem = terraingen.RasterSource(heights, terraingen.GeoTransform.north_up(-65.0, 65.0, 5.0))
    limit = terraingen.source_resolved_limit(dem, 4.0)
    assert limit.value_at(0.0, 0.0) == 20.0 and limit.value_at(1e6, 0.0) == float("inf")
    both = terraingen.MinimumField(limit, terraingen.ConstantField(12.0))
    assert both.value_at(0.0, 0.0) == 12.0 and both.value_at(1e6, 0.0) == 12.0
    options = terraingen.CraterGenerationOptions(d_min=6.0, d_max=40.0)
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
        terraingen.source_resolved_limit(dem, 0.0)


def test_python_sources_resolve_nothing_unless_they_say():
    class Level(terraingen.Source):
        def grid(self):
            return terraingen.FlatSource(4, 4, 1.0).grid()

        def height_at(self, x, y, spacing):
            return 0.0

    class Measured(Level):
        def resolution_at(self, x, y):
            return 3.0

    assert Level().resolution_at(0.0, 0.0) == float("inf")
    assert Measured().resolution_at(0.0, 0.0) == 3.0
    assert terraingen.source_resolved_limit(Measured(), 2.0).value_at(0.0, 0.0) == 6.0
