import numpy as np
import pytest

import terraingen


def test_boulder_styles_and_shapes(tmp_path):
    styles = terraingen.default_boulder_styles()
    assert [s.name for s in styles] == ["block", "slab", "rounded", "wedge", "pyramid", "shard"]
    assert styles[0].solid == terraingen.BaseSolid.CUBE
    options = terraingen.BoulderShapeOptions(subdivisions=2, seed=3)
    shape = terraingen.generate_boulder_shape(7, options)
    again = terraingen.generate_boulder_shape(7, options)
    positions, _ = shape.mesh.arrays
    np.testing.assert_array_equal(positions, again.mesh.arrays[0])
    assert np.ptp(positions[:, 0]) == pytest.approx(1.0, abs=0.05)
    assert 0 < shape.short_axis <= shape.intermediate <= 1.0
    egg = terraingen.BoulderStyle(
        name="egg", rounding=terraingen.ValueRange(1.0, 1.0), noise_amplitude=0.0, intermediate=terraingen.ValueRange(0.8, 0.8)
    )
    smooth = terraingen.generate_boulder_shape(1, terraingen.BoulderShapeOptions(styles=[egg], subdivisions=3))
    assert smooth.style == 0 and smooth.intermediate == pytest.approx(0.8)
    smooth.mesh.write_glb(tmp_path / "egg.glb")
    assert terraingen.boulder_shape_seed(12345) == 12345
    assert terraingen.boulder_shape_seed(12345, 16) < 16
    with pytest.raises(ValueError):
        terraingen.generate_boulder_shape(1, terraingen.BoulderShapeOptions(styles=[]))
    boulders = terraingen.BoulderLayerOptions(shapes=terraingen.BoulderShapeOptions(styles=[egg]), shape_prototypes=8)
    assert boulders.shapes.styles[0].name == "egg" and boulders.shape_prototypes == 8


def test_rim_smoothing_options():
    surface = terraingen.lunar_surface(3.5)
    models = terraingen.CraterModels(surface.density, surface.ages, terraingen.NoDegradation(), terraingen.FassettThomsonProfile())
    craters = terraingen.CraterLayer(
        models, terraingen.CraterLayerOptions(generation=terraingen.CraterGenerationOptions(min_diameter=10.0, max_diameter=60.0))
    )
    source = terraingen.FlatSource(129, 129, 1.0)
    plain = terraingen.evaluate(terraingen.Terrain(source, [craters])).heights.values
    soft = terraingen.RimSmoothing(sigma_radii=0.15, inner_radii=0.7, outer_radii=1.3, shape=terraingen.BlendShape.SMOOTHSTEP)
    smoothed = terraingen.evaluate(terraingen.Terrain(source, [craters], [terraingen.RimSmoothingFilter(soft)])).heights.values
    off = terraingen.RimSmoothing(strength=0.0)
    unchanged = terraingen.evaluate(terraingen.Terrain(source, [craters], [terraingen.RimSmoothingFilter(off)])).heights.values
    assert smoothed.max() < plain.max()
    np.testing.assert_array_equal(unchanged, plain)
    assert soft.shape == terraingen.BlendShape.SMOOTHSTEP and soft.outer_radii == 1.3


def test_tile_cache_and_exposure():
    noise = terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=30.0, amplitude=3.0))])
    terrain = terraingen.Terrain(terraingen.FlatSource(256, 256, 0.25), [noise])
    grid = terraingen.RasterGrid.centred(256, 256, 0.25)
    scheme = terraingen.TileScheme.covering(grid, 16)
    keys = scheme.tiles_covering(grid.bounds, scheme.level_for(0.25))
    cache = terraingen.TileCache(terrain, scheme, max_bytes=16 * 16 * 4 * 16)
    first = cache.get(keys[0]).heights.values.copy()
    for key in keys:
        cache.get(key)
    assert cache.evictions > 0 and cache.max_bytes == 16 * 16 * 4 * 16
    np.testing.assert_array_equal(cache.get(keys[0]).heights.values, first)
    np.testing.assert_array_equal(first, terraingen.evaluate_tile(terrain, scheme, keys[0]).heights.values)

    field = terraingen.ExposureField(terraingen.ExposureParams(reach=3.0))
    exposure = field.fill(terrain, 1, grid, 0.25).values
    assert exposure.shape == (256, 256) and 0.0 <= exposure.min() and exposure.max() <= 1.0 and np.ptp(exposure) > 0.1
    assert field.exposure(terrain, 1, 0.125, 0.125, 0.25) == pytest.approx(exposure[127, 128], abs=1e-6)
    flat = field.fill(terraingen.Terrain(terraingen.FlatSource(16, 16, 0.25)), 0, terraingen.RasterGrid.centred(16, 16, 0.25), 0.25)
    assert np.ptp(flat.values) == 0.0


def test_bedform_cover_and_boulder_abundance_read_the_ground():
    relief = terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=3, wavelength=60.0, amplitude=5.0))])
    exposure = terraingen.ExposureParams(reach=8.0)
    sand = terraingen.BedformLayer(terraingen.BedformOptions(wavelength=2.5, height=0.25, cover=0.8, field_size=30.0, exposure=exposure))
    boulders = terraingen.BoulderLayer(terraingen.BoulderLayerOptions(exposure=exposure, bedrock_abundance=4.0))
    ground = terraingen.Terrain(terraingen.FlatSource(64, 64, 0.5), [relief])
    with pytest.raises(ValueError):
        sand.cover_at(0.0, 0.0)
    covers = [sand.cover_at(x, 0.0, ground, 1) for x in np.linspace(-15.0, 15.0, 31)]
    assert all(0.0 <= c <= 1.0 for c in covers)
    abundances = [boulders.abundance_at(x, 0.0, ground, 1)[0] for x in np.linspace(-15.0, 15.0, 31)]
    assert min(abundances) < max(abundances) <= 4.0 + 1e-9
