"""layers: noise relief, regions, clamps, roughness, bedforms, exposure, fades and Python layers."""

import math

import numpy as np
import pytest

import terraingen


class Bump(terraingen.Layer):
    """A 1 m Gaussian bump at the origin, evaluated per point."""

    def apply_at(self, height, x, y, context):
        return height + math.exp(-(x * x + y * y) / 2000.0)


class Tilt(terraingen.Layer):
    """A 0 to 1 m ramp across the raster, evaluated per raster."""

    def apply(self, heights, context):
        heights.values[:] += np.linspace(0, 1, heights.width, dtype=np.float32)

    def max_feature_diameter(self):
        return 5.0


def plain_heights(source):
    return terraingen.evaluate(terraingen.Terrain(source)).values


def test_noise_relief_matches_the_kernels():
    layers = [
        terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=200.0, amplitude=10.0, octaves=5)),
        terraingen.NoiseLayer(terraingen.FractalParams(seed=2, wavelength=50.0, amplitude=2.0, octaves=5)),
    ]
    terrain = terraingen.Terrain(terraingen.FlatSource(65, 65, 2.0), [terraingen.NoiseRelief(layers)])
    heights = terraingen.evaluate(terrain, threads=2).values
    assert heights.shape == (65, 65) and np.ptp(heights) > 0
    expected = terraingen.evaluate_noise_layers(
        np.array([0.0], dtype=np.float32),
        np.array([0.0], dtype=np.float32),
        terraingen.SyntheticTerrainParams(layers),
    )
    assert heights[32, 32] == pytest.approx(float(expected[0]))
    assert terraingen.evaluate_at(terrain, 0.0, 0.0, 2.0) == heights[32, 32]


def test_noise_relief_places_no_craters_or_boulders(noise_relief):
    terrain = terraingen.Terrain(
        terraingen.FlatSource(65, 65, 2.0), [noise_relief(1, 200.0, 10.0, octaves=5)]
    )
    craters = terraingen.placed_craters(terrain).table
    assert craters["x"].size == 0 and terraingen.placed_boulders(terrain).table["x"].size == 0


def test_noise_relief_needs_a_layer():
    with pytest.raises(ValueError):
        terraingen.NoiseRelief([])


def test_region_layer_feathers_its_inner_layer():
    region = terraingen.RegionLayer(
        terraingen.ScaleLayer(1.0, 5.0), terraingen.RegionShape.circle(0.0, 0.0, 10.0), feather=5.0
    )
    heights = terraingen.evaluate(terraingen.Terrain(terraingen.FlatSource(65, 65, 1.0), [region])).values
    assert heights[32, 32] == pytest.approx(5.0) and heights[0, 0] == 0.0
    assert region.weight(0.0, 0.0) == 1.0 and region.weight(30.0, 0.0) == 0.0


def test_region_layer_keeps_its_inner_layer():
    region = terraingen.RegionLayer(terraingen.ScaleLayer(2.0), terraingen.RegionShape.circle(0.0, 0.0, 5.0))
    assert isinstance(region.inner, terraingen.ScaleLayer)


def test_rectangle_distance_outside():
    assert terraingen.RegionShape.rectangle(0, 0, 10, 10).distance_outside(15.0, 5.0) == pytest.approx(5.0)


def test_clamp_layer_caps_heights():
    flat = terraingen.FlatSource(65, 65, 1.0)
    layers = [terraingen.ScaleLayer(1.0, 5.0), terraingen.ClampLayer(max=2.0)]
    assert terraingen.evaluate(terraingen.Terrain(flat, layers)).values.max() == 2.0


def test_function_layer_runs_a_python_callable_on_any_thread():
    shifted = terraingen.FunctionLayer(lambda x, y, spacing, height: height + x)
    terrain = terraingen.Terrain(terraingen.FlatSource(8, 8, 1.0), [shifted])
    heights = terraingen.evaluate(terrain, threads=4).values
    np.testing.assert_allclose(heights[0], np.arange(-3.5, 4.5))


def test_roughness_matches_its_rms_slope():
    rough = terraingen.RoughnessLayer(terraingen.RoughnessOptions(rms_slope=0.2, seed=3))
    assert rough.rms_slope(0.25) == pytest.approx(0.2, rel=0.05)
    heights = terraingen.evaluate(terraingen.Terrain(terraingen.FlatSource(65, 65, 0.05), [rough])).values
    assert np.ptp(heights) > 0


def test_roughness_spans_several_wavelengths():
    assert len(terraingen.RoughnessLayer(terraingen.RoughnessOptions()).wavelengths()) > 1


def test_perlin_structure_functions_grow_with_lag():
    assert terraingen.perlin_structure_function(1.0) > terraingen.perlin_structure_function(0.1)
    assert terraingen.perlin3_structure_function(1.0) > 0


def test_bedform_cover_is_non_negative():
    ripples = terraingen.mars_large_ripples()
    ripples.cover = 0.9
    assert terraingen.BedformLayer(ripples).cover_at(0.0, 0.0) >= 0.0


def test_mars_transverse_aeolian_ridges_are_longer_than_large_ripples():
    ripples = terraingen.mars_large_ripples()
    assert terraingen.mars_transverse_aeolian_ridges().wavelength > ripples.wavelength


def bedforms_on_noise(noise_relief):
    exposure = terraingen.ExposureParams(reach=8.0)
    sand = terraingen.BedformLayer(
        terraingen.BedformOptions(wavelength=2.5, height=0.25, cover=0.8, field_size=30.0, exposure=exposure)
    )
    ground = terraingen.Terrain(terraingen.FlatSource(64, 64, 0.5), [noise_relief(3, 60.0, 5.0)])
    return sand, ground


def test_bedform_cover_with_exposure_needs_the_ground(noise_relief):
    sand, _ = bedforms_on_noise(noise_relief)
    with pytest.raises(ValueError):
        sand.cover_at(0.0, 0.0)


def test_bedform_cover_on_the_ground_is_a_fraction(noise_relief):
    sand, ground = bedforms_on_noise(noise_relief)
    covers = [sand.cover_at(x, 0.0, ground, 1) for x in np.linspace(-15.0, 15.0, 31)]
    assert all(0.0 <= c <= 1.0 for c in covers)


def exposure_terrain(noise_relief):
    return terraingen.Terrain(terraingen.FlatSource(256, 256, 0.25), [noise_relief(1, 30.0, 3.0)])


def test_exposure_is_a_fraction_that_varies_over_relief(noise_relief):
    field = terraingen.ExposureField(terraingen.ExposureParams(reach=3.0))
    grid = terraingen.RasterGrid.centred(256, 256, 0.25)
    exposure = field.fill(exposure_terrain(noise_relief), 1, grid, 0.25).values
    assert exposure.shape == (256, 256) and 0.0 <= exposure.min() and exposure.max() <= 1.0
    assert np.ptp(exposure) > 0.1


def test_exposure_at_a_point_matches_the_filled_raster(noise_relief):
    field = terraingen.ExposureField(terraingen.ExposureParams(reach=3.0))
    terrain = exposure_terrain(noise_relief)
    exposure = field.fill(terrain, 1, terraingen.RasterGrid.centred(256, 256, 0.25), 0.25).values
    assert field.exposure(terrain, 1, 0.125, 0.125, 0.25) == pytest.approx(exposure[127, 128], abs=1e-6)


def test_exposure_of_flat_ground_is_uniform():
    field = terraingen.ExposureField(terraingen.ExposureParams(reach=3.0))
    flat = field.fill(
        terraingen.Terrain(terraingen.FlatSource(16, 16, 0.25)),
        0,
        terraingen.RasterGrid.centred(16, 16, 0.25),
        0.25,
    )
    assert np.ptp(flat.values) == 0.0


def test_smoothstep_is_half_way_at_one_half_and_clamps():
    assert terraingen.smoothstep(0.5) == 0.5 and terraingen.smoothstep(2.0) == 1.0


def test_linear_falloff_is_half_way_mid_range():
    assert terraingen.falloff(1.0, 0.8, 1.2, terraingen.BlendShape.LINEAR) == pytest.approx(0.5)


def test_resolution_weight_is_zero_at_one_sample():
    assert terraingen.resolution_weight(1.0, 1.0, terraingen.ResolutionFade()) == 0.0


@pytest.mark.parametrize(
    ("validate", "bad"),
    [
        (terraingen.validate_resolution_fade, terraingen.ResolutionFade(start_samples=3.0, end_samples=1.0)),
        (terraingen.validate_exposure_params, terraingen.ExposureParams(reach=-1.0)),
    ],
    ids=["resolution_fade", "exposure_params"],
)
def test_layer_validators_reject_bad_options(validate, bad):
    with pytest.raises(ValueError):
        validate(bad)


def test_python_point_layer_adds_its_bump(paraboloid):
    plain = plain_heights(paraboloid)
    bumped = terraingen.evaluate(terraingen.Terrain(paraboloid, [Bump()])).values
    assert bumped[32, 32] - plain[32, 32] == pytest.approx(1.0, abs=1e-3)


def test_python_point_layer_answers_point_queries(paraboloid):
    assert terraingen.evaluate_at(terraingen.Terrain(paraboloid, [Bump()]), 0.0, 0.0, 2.0) == pytest.approx(
        1.0, abs=1e-3
    )


def test_python_raster_layer_edits_the_heights(paraboloid):
    plain = plain_heights(paraboloid)
    tilted = terraingen.evaluate(terraingen.Terrain(paraboloid, [Tilt()])).values
    np.testing.assert_allclose(tilted - plain, np.broadcast_to(np.linspace(0, 1, 65), (65, 65)), atol=1e-5)


def test_python_layers_report_their_feature_size():
    assert Tilt().max_feature_diameter() == 5.0 and Bump().max_feature_diameter() == 0.0


def test_base_layer_without_overrides_raises(paraboloid):
    with pytest.raises(RuntimeError):
        terraingen.evaluate(terraingen.Terrain(paraboloid, [terraingen.Layer()]))


def test_region_weight_on_a_sphere_uses_degrees():
    moon = terraingen.Surface.sphere(1737400.0)
    region = terraingen.RegionLayer(
        terraingen.ScaleLayer(1.0, 100.0), terraingen.RegionShape.circle(23.47, 0.67, 1.0), surface=moon
    )
    assert region.weight(23.47, 0.67) == 1.0


def test_layers_and_filters_accept_a_sphere_surface(lunar_models):
    moon = terraingen.Surface.sphere(1737400.0)
    generation = terraingen.CraterGenerationOptions(min_diameter=20000.0, max_diameter=300000.0)
    craters = terraingen.CraterLayer(
        lunar_models(3.8), terraingen.CraterLayerOptions(surface=moon, generation=generation)
    )
    rough = terraingen.RoughnessLayer(terraingen.RoughnessOptions(), surface=moon)
    boulders = terraingen.BoulderLayer(terraingen.BoulderLayerOptions(), craters, surface=moon)
    smoothing = terraingen.RimSmoothingFilter(surface=moon)
    bedforms = terraingen.BedformLayer(terraingen.mars_large_ripples(), surface=moon)
    assert all(part is not None for part in (rough, boulders, smoothing, bedforms))
