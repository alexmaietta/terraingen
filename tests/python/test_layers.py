# SPDX-License-Identifier: BSD-3-Clause

"""layers: noise relief, regions, clamps, roughness, bedforms, exposure, fades and Python layers."""

import json
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


def plain_heights(source):
    return terraingen.evaluate(terraingen.TerrainGenerator(source)).values


def test_noise_relief_matches_the_kernels():
    layers = [
        terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=200.0, amplitude=10.0, octaves=5)),
        terraingen.NoiseLayer(terraingen.FractalParams(seed=2, wavelength=50.0, amplitude=2.0, octaves=5)),
    ]
    terrain = terraingen.TerrainGenerator(
        terraingen.FlatSource(65, 65, 2.0), [terraingen.NoiseRelief(layers)]
    )
    heights = terraingen.evaluate(terrain, threads=2).values
    assert heights.shape == (65, 65) and np.ptp(heights) > 0
    expected = terraingen.evaluate_noise_layers(
        np.array([0.0], dtype=np.float32),
        np.array([0.0], dtype=np.float32),
        terraingen.SyntheticTerrainParams(layers),
    )
    assert heights[32, 32] == pytest.approx(float(expected[0]))
    assert terraingen.height_at(terrain, 0.0, 0.0, 2.0) == heights[32, 32]


def test_noise_relief_places_no_craters_or_boulders(noise_relief):
    terrain = terraingen.TerrainGenerator(
        terraingen.FlatSource(65, 65, 2.0), [noise_relief(1, 200.0, 10.0, octaves=5)]
    )
    craters = terraingen.placed_craters(terrain).table
    assert craters["x"].size == 0 and terraingen.placed_boulders(terrain).table["x"].size == 0


def test_noise_relief_needs_a_layer():
    with pytest.raises(ValueError):
        terraingen.NoiseRelief([])


def test_masked_layer_feathers_its_inner_layer():
    region = terraingen.MaskedLayer(
        terraingen.ScaleLayer(1.0, 5.0), terraingen.RegionShape.circle(0.0, 0.0, 10.0), feather=5.0
    )
    heights = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(65, 65, 1.0), [region])
    ).values
    assert heights[32, 32] == pytest.approx(5.0) and heights[0, 0] == 0.0
    assert region.weight(0.0, 0.0) == 1.0 and region.weight(30.0, 0.0) == 0.0


def test_masked_layer_keeps_its_inner_layer():
    region = terraingen.MaskedLayer(terraingen.ScaleLayer(2.0), terraingen.RegionShape.circle(0.0, 0.0, 5.0))
    assert isinstance(region.inner, terraingen.ScaleLayer)
    assert not hasattr(terraingen, "RegionLayer")
    config = terraingen.TerrainGenConfig.from_dict(
        {
            "schema_version": 1,
            "layers": [
                {
                    "type": "masked",
                    "shape": {"type": "circle", "radius": 5},
                    "layer": {"type": "scale", "scale": 2},
                }
            ],
        }
    )
    assert config.to_dict()["layers"][0]["type"] == "masked"
    with pytest.raises(terraingen.ConfigError):
        terraingen.TerrainGenConfig.from_dict({"schema_version": 1, "layers": [{"type": "region"}]})


def test_rectangle_distance_outside():
    assert terraingen.RegionShape.rectangle(0, 0, 10, 10).distance_outside(15.0, 5.0) == pytest.approx(5.0)


def test_clamp_layer_caps_heights():
    flat = terraingen.FlatSource(65, 65, 1.0)
    layers = [terraingen.ScaleLayer(1.0, 5.0), terraingen.ClampLayer(max=2.0)]
    assert terraingen.evaluate(terraingen.TerrainGenerator(flat, layers)).values.max() == 2.0


def test_function_layer_runs_a_python_callable_on_any_thread():
    shifted = terraingen.FunctionLayer(lambda x, y, spacing, height: height + x)
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(8, 8, 1.0), [shifted])
    heights = terraingen.evaluate(terrain, threads=4).values
    np.testing.assert_allclose(heights[0], np.arange(-3.5, 4.5))


def test_roughness_matches_its_rms_slope():
    rough = terraingen.RoughnessLayer(terraingen.RoughnessOptions(rms_slope=0.2, seed=3))
    assert rough.rms_slope(0.25) == pytest.approx(0.2, rel=0.05)
    heights = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(65, 65, 0.05), [rough])
    ).values
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
    ground = terraingen.TerrainGenerator(terraingen.FlatSource(64, 64, 0.5), [noise_relief(3, 60.0, 5.0)])
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
    return terraingen.TerrainGenerator(terraingen.FlatSource(256, 256, 0.25), [noise_relief(1, 30.0, 3.0)])


def test_exposure_is_a_fraction_that_varies_over_relief(noise_relief):
    field = terraingen.ExposureField(terraingen.ExposureParams(reach=3.0))
    grid = terraingen.RasterGrid.centered((0, 0), (256 * 0.25, 256 * 0.25), 0.25)
    exposure = field.fill(exposure_terrain(noise_relief), 1, grid, 0.25).values
    assert exposure.shape == (256, 256) and 0.0 <= exposure.min() and exposure.max() <= 1.0
    assert np.ptp(exposure) > 0.1


def test_exposure_at_a_point_matches_the_filled_raster(noise_relief):
    field = terraingen.ExposureField(terraingen.ExposureParams(reach=3.0))
    terrain = exposure_terrain(noise_relief)
    exposure = field.fill(
        terrain, 1, terraingen.RasterGrid.centered((0, 0), (256 * 0.25, 256 * 0.25), 0.25), 0.25
    ).values
    assert field.exposure(0.125, 0.125, 0.25, terrain, 1) == pytest.approx(exposure[127, 128], abs=1e-6)
    with pytest.raises(ValueError, match="need the ground"):
        field.exposure(0.125, 0.125)


def test_exposure_of_flat_ground_is_uniform():
    field = terraingen.ExposureField(terraingen.ExposureParams(reach=3.0))
    flat = field.fill(
        terraingen.TerrainGenerator(terraingen.FlatSource(16, 16, 0.25)),
        0,
        terraingen.RasterGrid.centered((0, 0), (16 * 0.25, 16 * 0.25), 0.25),
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
    bumped = terraingen.evaluate(terraingen.TerrainGenerator(paraboloid, [Bump()])).values
    assert bumped[32, 32] - plain[32, 32] == pytest.approx(1.0, abs=1e-3)


def test_python_point_layer_answers_point_queries(paraboloid):
    assert terraingen.height_at(
        terraingen.TerrainGenerator(paraboloid, [Bump()]), 0.0, 0.0, 2.0
    ) == pytest.approx(1.0, abs=1e-3)


def test_python_raster_layer_edits_the_heights(paraboloid):
    plain = plain_heights(paraboloid)
    tilted = terraingen.evaluate(terraingen.TerrainGenerator(paraboloid, [Tilt()])).values
    np.testing.assert_allclose(tilted - plain, np.broadcast_to(np.linspace(0, 1, 65), (65, 65)), atol=1e-5)


def test_base_layer_without_overrides_raises(paraboloid):
    with pytest.raises(TypeError):
        terraingen.evaluate(terraingen.TerrainGenerator(paraboloid, [terraingen.Layer()]))


def test_region_weight_on_a_sphere_uses_degrees():
    moon = terraingen.Surface.sphere(1737400.0)
    region = terraingen.MaskedLayer(
        terraingen.ScaleLayer(1.0, 100.0), terraingen.RegionShape.circle(23.47, 0.67, 1.0), base=moon
    )
    assert region.weight(23.47, 0.67) == 1.0


def test_layers_are_built_on_one_terrain_base(lunar_models):
    moon = terraingen.Surface.sphere(1737400.0)
    base = terraingen.TerrainBase(moon)
    generation = terraingen.CraterGenerationOptions(d_min=20000.0, d_max=300000.0)
    craters = terraingen.CraterLayer(
        lunar_models(3.8), terraingen.CraterLayerOptions(generation=generation), base
    )
    rough = terraingen.RoughnessLayer(terraingen.RoughnessOptions(), base)
    boulders = terraingen.BoulderLayer(terraingen.BoulderLayerOptions(), craters, base)
    bedforms = terraingen.BedformLayer(terraingen.mars_large_ripples(), base=moon)
    assert all(part.layer_surface() == moon for part in (craters, rough, boulders, bedforms))
    assert not hasattr(terraingen.CraterLayerOptions(), "surface")
    assert base.source is None and base.reference_point() == (0.0, 0.0)
    with pytest.raises(ValueError):
        terraingen.BoulderLayer(terraingen.BoulderLayerOptions(), craters)


def test_angles_are_degrees_in_options_and_configs():
    dunes = terraingen.DuneOptions(slip_face_angle_degrees=30.0, wind_azimuth_degrees=90.0)
    assert (dunes.slip_face_angle_degrees, dunes.wind_azimuth_degrees) == (30.0, 90.0)
    assert not hasattr(dunes, "slip_face_angle")
    assert terraingen.BoulderVentifacts().second_wind_azimuth_degrees is None
    config = terraingen.TerrainGenConfig.from_dict(
        {"schema_version": 1, "layers": [{"type": "dunes", "wind_azimuth_deg": 90}, {"type": "craters"}]}
    )
    written = config.to_dict()["layers"]
    assert written[0]["wind_azimuth_deg"] == 90.0 and written[0]["slip_face_angle_deg"] == 32.0
    assert "roughness" in json.dumps(written[1]) and "edges" not in json.dumps(written[1])


def test_dune_presets_build_barchans_with_slip_faces_and_ripples():
    barchans = terraingen.mars_barchans()
    assert barchans.shape == terraingen.DuneShape.BARCHAN
    assert barchans.slip_face_angle_degrees == 29.0
    assert barchans.ripples.wavelength == pytest.approx(2.1)
    assert terraingen.mars_transverse_dunes().wavelength == pytest.approx(248.0)
    options = terraingen.DuneOptions(width=400.0, width_spread=0.0, lattice_spacing=2000.0, density=1.0)
    layer = terraingen.DuneLayer(options)
    terrain = terraingen.TerrainGenerator(None, [layer])
    raster = terraingen.evaluate(terrain, grid=terraingen.FlatSource(100, 100, 20.0).grid())
    heights = raster.values
    assert heights.max() == pytest.approx(400.0 / 15.0, rel=0.05)
    row, col = np.unravel_index(np.argmax(heights), heights.shape)
    assert layer.dune_height_at(*raster.cell_center(int(col), int(row))) > 0.0
    with pytest.raises(ValueError):
        terraingen.validate_dune_options(terraingen.DuneOptions(stoss_slope_degrees=60.0))


def test_dune_ripples_and_grainflows_are_none_until_set():
    options = terraingen.DuneOptions()
    assert options.ripples is None and options.grainflows is None
    options.ripples = terraingen.DuneRipples()
    options.grainflows = terraingen.DuneGrainflows()
    assert options.ripples.wavelength == pytest.approx(2.1) and options.grainflows.cover == 1.0
    terraingen.validate_dune_options(options)
    options.ripples = None
    assert options.ripples is None
    with pytest.raises(ValueError):
        terraingen.validate_dune_options(
            terraingen.DuneOptions(ripples=terraingen.DuneRipples(wavelength=0.0))
        )


def test_dune_ripples_ride_on_crest_fields():
    options = terraingen.DuneOptions(shape=terraingen.DuneShape.TRANSVERSE, wavelength=200.0, height=15.0)
    options.ripples = terraingen.mars_dune_ripples()
    layer = terraingen.DuneLayer(options)
    context = terraingen.LayerContext(spacing=0.05)
    rippled = [layer.apply_at(0.0, 10.0 + 0.1 * i, 3.0, context) for i in range(200)]
    bare = [layer.dune_height_at(10.0 + 0.1 * i, 3.0) for i in range(200)]
    assert max(r - b for r, b in zip(rippled, bare, strict=True)) > 0.05
    config = terraingen.TerrainGenConfig.from_dict(
        {
            "schema_version": 1,
            "layers": [{"type": "dunes", "shape": "linear", "ripples": {"wavelength": 2.0, "height": 0.2}}],
        }
    )
    assert config.to_dict()["layers"][0]["shape"] == "linear"
    config.validate()


def test_sand_fill_pools_sand_and_lays_it_on_the_material_map():
    pit = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=4, wavelength=20.0, amplitude=1.0, octaves=3))]
    )
    fill = terraingen.SandFillLayer(terraingen.SandFillOptions(reach=6.0, fill=0.7, max_thickness=0.4))
    assert fill.material == terraingen.MaterialClass.SAND
    terrain = terraingen.TerrainGenerator(None, [pit, fill])
    grid = terraingen.FlatSource(80, 80, 0.5).grid()
    filled = terraingen.evaluate(terrain, grid=grid).values
    bare = terraingen.evaluate(terraingen.TerrainGenerator(None, [pit]), grid=grid).values
    thickness = filled - bare
    assert thickness.min() >= 0.0 and 0.05 < thickness.max() <= 0.4
    row, col = np.unravel_index(np.argmax(thickness), thickness.shape)
    x, y = terraingen.evaluate(terrain, grid=grid).cell_center(int(col), int(row))
    assert fill.thickness_at(x, y, 0.5, terrain, 1) == pytest.approx(thickness[row, col], abs=1e-5)
    assert fill.material_cover_at(x, y, 0.5, terrain, 1) == pytest.approx(1.0)
    assert fill.thickness_at(x, y, spacing=0.5, generator=terrain, layers=1) >= 0.0
    with pytest.raises(ValueError, match="need the ground"):
        fill.thickness_at(x, y)
    with pytest.raises(ValueError):
        terraingen.SandFillLayer(terraingen.SandFillOptions(reach=0.0))

    shares = terraingen.materials_at(terrain, terraingen.MaterialOptions(), x, y, 0.5)
    assert len(shares) == terraingen.MATERIAL_CLASSES and sum(shares) == pytest.approx(1.0)
    assert shares[terraingen.MaterialClass.SAND.value] == pytest.approx(1.0)
    bands = terraingen.evaluate_materials(terrain, terraingen.MaterialOptions(dust_cover=0.2), grid)
    assert len(bands) == 5
    np.testing.assert_allclose(sum(b.values for b in bands), 1.0, atol=1e-6)


def test_pancam_photometry_mixes_and_needs_every_present_class():
    pancam = terraingen.mars_pancam_photometry()
    assert pancam[terraingen.MaterialClass.BEDROCK.value].albedo == pytest.approx(0.90)
    assert pancam[terraingen.MaterialClass.SAND.value].albedo == pytest.approx(0.52)
    mixed = terraingen.mix_photometry([0.5, 0.5, 0.0, 0.0, 0.0], pancam)
    assert mixed.albedo == pytest.approx(0.64) and mixed.roughness_degrees == pytest.approx(12.0)
    partial = list(pancam)
    partial[terraingen.MaterialClass.BEDROCK.value] = None
    with pytest.raises(ValueError):
        terraingen.mix_photometry([0.0, 0.0, 0.0, 1.0, 0.0], partial)
    grid = terraingen.FlatSource(8, 8, 1.0).grid()
    bands = terraingen.evaluate_materials(terraingen.TerrainGenerator(), terraingen.MaterialOptions(), grid)
    photometry = terraingen.evaluate_photometry(bands, pancam)
    assert len(photometry) == 4 and photometry[0].values.mean() == pytest.approx(0.76)


def test_pavement_plates_crack_in_octaves_and_lay_bedrock():
    options = terraingen.PavementOptions(
        plate_size=1.0, crack_width=0.04, crack_depth=0.1, max_step=0.0, max_tilt_degrees=0.0
    )
    single = terraingen.PavementLayer(options)
    options.octaves = 3
    layered = terraingen.PavementLayer(options)
    assert layered.material == terraingen.MaterialClass.BEDROCK
    assert layered.material_cover_at(0.2, 0.3) == 1.0
    xs = [0.01 * i for i in range(600)]
    fine = terraingen.LayerContext(1e-4, 1)
    assert min(single.apply_at(0.0, x, 1.7, fine) for x in xs) < -0.05
    assert sum(layered.apply_at(0.0, x, 1.7, fine) < -0.01 for x in xs) > sum(
        single.apply_at(0.0, x, 1.7, fine) < -0.01 for x in xs
    )
    ridges = terraingen.mars_desiccation_ridges()
    assert ridges.raised and ridges.plate_size == pytest.approx(0.02)
    with pytest.raises(ValueError):
        terraingen.PavementLayer(terraingen.PavementOptions(octaves=0))
    config = terraingen.TerrainGenConfig.from_dict(
        {
            "schema_version": 1,
            "layers": [{"type": "pavement", "plate_size": 0.5, "octaves": 3, "raised": True}],
        }
    )
    assert config.to_dict()["layers"][0]["octaves"] == 3
    config.validate()


def test_strata_terrace_hills_into_benches_and_cliffs():
    options = terraingen.StrataOptions(bed_thickness=2.0, thickness_variation=0.0, sharpness=4.0)
    assert options.hard_share == 1.0 and options.soft_sharpness == 1.0
    layer = terraingen.StrataLayer(options)
    assert layer.material == terraingen.MaterialClass.BEDROCK
    context = terraingen.LayerContext(1.0, 1)
    assert layer.apply_at(5.0, 0.0, 0.0, context) == pytest.approx(4.0 + 2.0 * 0.5**4)
    assert layer.apply_at(6.0, 0.0, 0.0, context) == pytest.approx(6.0)
    with pytest.raises(ValueError):
        terraingen.StrataLayer(terraingen.StrataOptions(sharpness=0.5))
    config = terraingen.TerrainGenConfig.from_dict(
        {
            "schema_version": 1,
            "layers": [{"type": "strata", "bed_thickness": 3.0, "dip_deg": 3.0}],
        }
    )
    assert config.to_dict()["layers"][0]["bed_thickness"] == 3.0
    config.validate()


def test_sand_shadows_trail_boulders_and_boulders_are_rock():
    boulders = terraingen.BoulderLayer(
        terraingen.BoulderLayerOptions(
            sizes=terraingen.BoulderSizes(
                distribution=None, coverage=0.05, min_diameter=1.0, max_diameter=2.0
            )
        )
    )
    table = boulders.boulders((-50.0, -50.0, 50.0, 50.0)).table
    x, y, d = table["x"][0], table["y"][0], table["diameter"][0]
    assert boulders.material == terraingen.MaterialClass.ROCK
    assert boulders.material_cover_at(x, y, 0.05) == 1.0
    shadows = terraingen.SandShadowLayer(boulders, terraingen.SandShadowOptions(length_ratio=2.0))
    assert shadows.material == terraingen.MaterialClass.SAND
    assert shadows.thickness_at(x + 0.5 * d + 0.01, y) > 0.0
    with pytest.raises(ValueError):
        terraingen.SandShadowLayer(boulders, terraingen.SandShadowOptions(length_ratio=0.0))
    config = terraingen.TerrainGenConfig.from_dict(
        {
            "schema_version": 1,
            "layers": [{"type": "boulders"}, {"type": "sand_shadow", "length_ratio": 2.0}],
        }
    )
    assert config.to_dict()["layers"][1]["length_ratio"] == 2.0
    config.validate()


def test_ventifacts_plane_the_windward_side_of_boulders():
    gale = terraingen.gale_ventifacts()
    assert gale.share == pytest.approx(0.39)
    ventifacts = terraingen.BoulderVentifacts(share=1.0, wind_azimuth_degrees=0.0)
    boulders = terraingen.BoulderLayer(
        terraingen.BoulderLayerOptions(
            sizes=terraingen.BoulderSizes(
                distribution=None, coverage=0.05, min_diameter=1.0, max_diameter=2.0
            ),
            ventifacts=ventifacts,
        )
    )
    table = boulders.boulders((-50.0, -50.0, 50.0, 50.0)).table
    x, y, d = table["x"][0], table["y"][0], table["diameter"][0]
    boulder = terraingen.Boulder(x, y, d, 0.5 * d)
    boulder.facets = [terraingen.BoulderFacet(180.0, 50.0, 0.5 * d)]
    assert len(boulder.facets) == 1 and boulder.facets[0].azimuth_deg == 180.0
    assert boulder.facets[0].angle_deg == 50.0
    assert boulder.facets[0].height_at(-0.5 * d, 0.0) == pytest.approx(0.0, abs=1e-5)
    with pytest.raises(ValueError):
        terraingen.validate_boulder_ventifacts(terraingen.BoulderVentifacts(share=2.0))
    config = terraingen.TerrainGenConfig.from_dict(
        {
            "schema_version": 1,
            "layers": [{"type": "boulders", "ventifacts": {"share": 0.39, "keel_share": 0.3}}],
        }
    )
    assert config.to_dict()["layers"][0]["ventifacts"]["share"] == pytest.approx(0.39)
    config.validate()


def test_dune_grainflows_and_straightened_ripples():
    flows = terraingen.mars_dune_grainflows()
    assert flows.min_lobe_length == pytest.approx(2.9) and flows.thickness == pytest.approx(0.015)
    assert terraingen.mars_dune_ripples().flank_straightening == 1.0
    options = terraingen.mars_transverse_dunes()
    options.grainflows = flows
    layer = terraingen.DuneLayer(options)
    assert layer.options.grainflows.cover == 1.0
    with pytest.raises(ValueError):
        terraingen.DuneLayer(terraingen.DuneOptions(grainflows=terraingen.DuneGrainflows(cover=2.0)))
    config = terraingen.TerrainGenConfig.from_dict(
        {
            "schema_version": 1,
            "layers": [{"type": "dunes", "shape": "transverse", "grainflows": {"cover": 0.5}}],
        }
    )
    assert config.to_dict()["layers"][0]["grainflows"]["cover"] == 0.5
    config.validate()


def test_diamond_square_upsampling_expands_posts_keeping_them():
    posts = terraingen.Raster.from_array(
        np.zeros((16, 16), dtype=np.float32), terraingen.GeoTransform.north_up(0.0, 64.0, 4.0)
    )
    options = terraingen.DiamondSquareOptions(fractal_dimension=2.3, scale=0.5)
    options.validate()
    upsampler = terraingen.DiamondSquareUpsampler(options)
    assert upsampler.reach() == 4
    assert upsampler.options.interp == terraingen.DiamondSquareInterp.BILINEAR
    source = terraingen.RasterSource(posts, upsampler=upsampler)
    grid = terraingen.RasterGrid(63, 63, terraingen.GeoTransform.north_up(0.5, 63.5, 1.0))
    heights = terraingen.evaluate(terraingen.TerrainGenerator(source), grid=grid)
    values = heights.values
    assert np.all(values[1::4, 1::4] == 0.0)
    assert np.abs(values).max() > 0.1
    roughness = terraingen.estimate_midpoint_roughness(heights)
    assert roughness.scale > 0.0 and 2.0 <= roughness.fractal_dimension <= 3.0
    with pytest.raises(ValueError):
        terraingen.validate_diamond_square_options(terraingen.DiamondSquareOptions(fractal_dimension=3.5))
    fitted = terraingen.DiamondSquareUpsampler().fit_to(terraingen.RasterPosts(heights))
    assert fitted.options.scale > 0.0 and fitted.options.fractal_dimension is not None


def test_spectral_continuation_adds_detail_below_the_base_following_its_spectrum():
    grid = terraingen.FlatSource(128, 128, 0.125).grid()
    options = terraingen.SpectralContinuationOptions(
        pixel_size=1.0, slope=3.0, octave_rms=0.2, min_wavelength=0.25
    )
    options.validate()
    layer = terraingen.SpectralContinuationLayer(options)
    assert layer.options.slope == 3.0
    heights = terraingen.evaluate(terraingen.TerrainGenerator(None, [layer]), grid=grid)
    assert heights.values.std() > 0.05
    at = heights.cell_center(5, 7)
    point = layer.apply_at(0.0, at[0], at[1], terraingen.LayerContext(0.125, 1))
    assert point == heights.values[7, 5]
    coarse_grid = terraingen.FlatSource(32, 32, 1.0).grid()
    coarse = terraingen.evaluate(terraingen.TerrainGenerator(None, [layer]), grid=coarse_grid)
    assert np.all(coarse.values == 0.0)

    band = terraingen.SpectrumFitBand(min_pixels=3.0, max_pixels=6.0)
    fit = terraingen.estimate_power_law_spectrum(heights, band)
    assert 2.0 <= fit.slope <= 4.0 and fit.octave_rms > 0.0
    with pytest.raises(ValueError):
        terraingen.validate_spectral_continuation_options(terraingen.SpectralContinuationOptions(slope=4.5))
    with pytest.raises(ValueError):
        terraingen.validate_spectrum_fit_band(terraingen.SpectrumFitBand(min_pixels=1.0))
    config = terraingen.TerrainGenConfig.from_dict(
        {
            "schema_version": 1,
            "layers": [
                {
                    "type": "spectral_continuation",
                    "pixel_size": 1.0,
                    "slope": 2.6,
                    "octave_rms": 0.1,
                    "min_wavelength": 0.2,
                }
            ],
        }
    )
    config.validate()
    spec = config.to_dict()["layers"][0]
    assert spec["slope"] == 2.6 and spec["fit_min_pixels"] == 4.0


def self_affine_heights(hurst, size=96):
    """Cosines halving in wavelength by sqrt(2) from 128 m to 1/8 m, amplitudes as wavelength^hurst."""
    rng = np.random.default_rng(4)
    y, x = np.mgrid[0:size, 0:size].astype(np.float64) + 0.5
    heights = np.zeros((size, size))
    for wavelength in 128.0 * 2.0 ** (-0.5 * np.arange(21)):
        for turn, phase in rng.uniform(0.0, 2.0 * math.pi, (8, 2)):
            k = 2.0 * math.pi / wavelength
            heights += 0.1 * wavelength**hurst * np.cos(k * (x * math.cos(turn) + y * math.sin(turn)) + phase)
    return heights.astype(np.float32)


def test_base_roughness_recovers_the_hurst_exponent_and_continues_it():
    heights = terraingen.Raster.from_array(
        self_affine_heights(0.7), terraingen.GeoTransform.north_up(0.0, 96.0, 1.0)
    )
    roughness = terraingen.estimate_source_roughness(terraingen.RasterSource(heights))
    assert roughness.hurst == pytest.approx(0.7, abs=0.1)
    assert roughness.cell_size == 1.0 and roughness.deviation > 0.0
    assert roughness.deviation_at(0.25) == pytest.approx(roughness.deviation * 0.25**roughness.hurst)
    assert roughness.rms_slope(0.25) == pytest.approx(roughness.deviation_at(0.25) / 0.25)
    with pytest.raises(ValueError):
        flat = terraingen.Raster.from_array(
            np.zeros((8, 8), np.float32), terraingen.GeoTransform.north_up(0.0, 8.0, 1.0)
        )
        terraingen.estimate_source_roughness(terraingen.RasterSource(flat))

    layer = terraingen.NoiseLayer()
    layer.fractal.wavelength = 1.0
    layer.fractal.octaves = 3
    amplitude = terraingen.matched_noise_amplitude(layer, roughness)
    assert amplitude > 0.0
    doubled = terraingen.BaseRoughness()
    doubled.hurst, doubled.deviation, doubled.cell_size = roughness.hurst, 2.0 * roughness.deviation, 1.0
    assert terraingen.matched_noise_amplitude(layer, doubled) == pytest.approx(2.0 * amplitude, rel=1e-5)


def test_roughness_and_noise_layers_estimate_their_settings_from_a_dem(tmp_path):
    heights = terraingen.Raster.from_array(
        self_affine_heights(0.8, 64), terraingen.GeoTransform.north_up(1000.0, 2064.0, 1.0)
    )
    terraingen.write_geotiff(str(tmp_path / "dem.tif"), heights)
    config = terraingen.TerrainGenConfig.from_dict(
        {
            "schema_version": 1,
            "source": {"type": "raster", "path": "dem.tif"},
            "layers": [
                {"type": "noise", "estimate_from_base": True, "stack": [{"wavelength": 1.0, "octaves": 2}]},
                {"type": "roughness", "estimate_from_base": True, "max_wavelength": 1.0, "rms_slope": 0.3},
            ],
        }
    )
    config.validate(str(tmp_path))
    layers = config.to_dict()["layers"]
    assert layers[1]["estimate_from_base"] and layers[1]["hurst"] is None and layers[1]["rms_slope"] == 0.3
    assert terraingen.TerrainGenConfig.from_dict(config.to_dict()) == config
    terrain = terraingen.build_generator(config, str(tmp_path))
    base = terraingen.estimate_source_roughness(terrain.source)
    roughness = terrain.layers[1].options
    assert roughness.hurst == pytest.approx(base.hurst) and roughness.rms_slope == 0.3
    noise = terrain.layers[0].options.layers[0]
    assert noise.fractal.gain == pytest.approx(terraingen.gain_from_hurst(base.hurst, 2.0), rel=1e-6)
    assert noise.fractal.amplitude > 0.0
    without_base = terraingen.TerrainGenConfig.from_dict(
        {"schema_version": 1, "layers": [{"type": "roughness", "estimate_from_base": True}]}
    )
    with pytest.raises(terraingen.ConfigError) as error:
        without_base.validate()
    assert error.value.path == "layers[0].estimate_from_base"


def test_layers_leave_out_what_the_source_resolves():
    dem = terraingen.RasterSource(
        np.zeros((20, 20), dtype=np.float32), terraingen.GeoTransform.north_up(0.0, 2000.0, 100.0)
    )
    assert terraingen.source_resolved_limit(dem, 4.0).value_at(50.0, 50.0) == 400.0
    dunes = terraingen.DuneOptions(shape=terraingen.DuneShape.TRANSVERSE)
    assert dunes.source_resolves_pixels == 4.0
    held = terraingen.DuneLayer(dunes, base=dem)
    free = terraingen.DuneLayer(dunes)
    context = terraingen.LayerContext(spacing=1.0)
    xs = np.arange(10.0, 1000.0, 37.0)
    assert all(held.apply_at(0.0, x, 700.0, context) == 0.0 for x in xs), "550 m dunes over 400 m"
    assert any(free.apply_at(0.0, x, 700.0, context) != 0.0 for x in xs)
    dunes.source_resolves_pixels = None
    assert terraingen.DuneLayer(dunes, base=dem).apply_at(0.0, 400.0, 700.0, context) == free.apply_at(
        0.0, 400.0, 700.0, context
    )
    assert terraingen.BedformOptions().source_resolves_pixels == 4.0
    assert terraingen.BoulderLayerOptions().source_resolves_pixels is None
    assert terraingen.SecondaryCraterOptions().source_resolves_pixels == 4.0
    assert terraingen.CraterLayerOptions().source_resolves_pixels == 4.0
    big = terraingen.NoiseLayer()
    big.fractal.wavelength = 1000.0
    noise = terraingen.NoiseRelief(
        terraingen.NoiseReliefOptions(layers=[big], source_resolves_pixels=4.0), dem
    )
    assert noise.options.source_resolves_pixels == 4.0
    assert noise.apply_at(0.0, 300.0, 300.0, context) == 0.0
    assert terraingen.NoiseRelief([big], base=dem).apply_at(0.0, 300.0, 300.0, context) != 0.0
    apply = terraingen.CraterApplyOptions(fade_px=(1.0, 3.0))
    assert apply.fade_px == [1.0, 3.0] and not hasattr(apply, "fade")
    config = terraingen.TerrainGenConfig.from_dict(
        {"schema_version": 1, "layers": [{"type": "craters", "source_resolves_px": None}]}
    )
    assert config.to_dict()["layers"][0]["source_resolves_px"] is None
