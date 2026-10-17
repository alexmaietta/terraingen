"""albedo: maturity, synthetic and raster albedo, crater albedo and rays, and Python albedo parts."""

import numpy as np
import pytest

import terraingen


class Stripes(terraingen.AlbedoSource):
    """Single-band stripes 10 m wide, alternating 0.2 and 0.1."""

    def bands(self):
        return 1

    def value_at(self, x, y, context):
        return 0.1 if int(x // 10) % 2 else 0.2


class Halve(terraingen.AlbedoLayer):
    """Halves every band."""

    def apply_at(self, value, bands, x, y, context):
        return [v * 0.5 for v in value]


def young_crater():
    return terraingen.Crater(0.0, 0.0, 250.0, age_gyr=0.002)


def manual_crater_layer(lunar_models):
    """A layer holding only the young 250 m crater."""
    return terraingen.CraterLayer(
        lunar_models(),
        terraingen.CraterLayerOptions(
            generate=False, generation=terraingen.CraterGenerationOptions(manual=[young_crater()])
        ),
    )


def rayed_terrain(lunar_models):
    """Craters 60 to 300 m plus the young crater on a 129 x 129 grid at 8 m, and the crater layer."""
    craters = terraingen.CraterLayer(
        lunar_models(3.2),
        terraingen.CraterLayerOptions(
            generation=terraingen.CraterGenerationOptions(
                min_diameter=60.0, max_diameter=300.0, manual=[young_crater()]
            )
        ),
    )
    return terraingen.Terrain(terraingen.FlatSource(129, 129, 8.0), [craters]), craters


def noisy_albedo():
    variation = terraingen.NoiseLayer(
        terraingen.FractalParams(seed=4, wavelength=800.0, amplitude=0.02, octaves=3)
    )
    return terraingen.Albedo(terraingen.SyntheticAlbedo(value=0.11, value_noise=[variation]))


def rayed_albedo(craters):
    rays = terraingen.CraterAlbedoOptions(rays=terraingen.CraterRays(enabled=True))
    return terraingen.Albedo(noisy_albedo().source, [terraingen.CraterAlbedo(craters, rays)])


def test_optical_maturity_is_zero_at_the_reference_ratio():
    assert terraingen.optical_maturity(0.08, 0.08 * 1.19) == pytest.approx(0.0, abs=1e-9)


def test_immature_soil_is_brighter():
    assert terraingen.reflectance_at_maturity(0.11, 0.2, 0.4) > 0.11


def test_optical_maturity_origin_keeps_its_ratio():
    assert terraingen.OpticalMaturityOrigin(ratio=1.2).ratio == 1.2


def test_hsv_to_rgb_of_half_bright_red():
    assert terraingen.hsv_to_rgb(0.0, 1.0, 0.5) == pytest.approx((0.5, 0.0, 0.0))


def test_crater_albedo_brightens_a_young_crater_beyond_its_rim(lunar_models):
    bright = terraingen.CraterAlbedo(manual_crater_layer(lunar_models))
    assert bright.excess(young_crater(), 0.0, 0.0, 8.0) > 0 and bright.reach(young_crater()) > 125.0


def test_crater_albedo_impact_has_a_valid_angle_and_speed(lunar_models):
    impact = terraingen.CraterAlbedo(manual_crater_layer(lunar_models)).impact(young_crater())
    assert 0.0 <= impact.angle_degrees <= 90.0 and impact.speed_ratio >= 1.0


def test_albedo_at_a_point_matches_the_evaluated_band(lunar_models):
    bright = terraingen.CraterAlbedo(manual_crater_layer(lunar_models))
    albedo = terraingen.Albedo(terraingen.SyntheticAlbedo(value=0.11), [bright])
    band = terraingen.evaluate_albedo(albedo, terraingen.RasterGrid.centred(9, 9, 8.0))[0].values
    assert terraingen.evaluate_albedo_at(albedo, 0.0, 0.0, 8.0) == pytest.approx([band[4, 4]])


def test_synthetic_albedo_varies_around_its_value(lunar_models):
    terrain, _ = rayed_terrain(lunar_models)
    base = terraingen.evaluate_albedo(noisy_albedo(), terrain.source.grid())[0].values
    assert base.shape == (129, 129) and abs(base.mean() - 0.11) < 0.01


def test_crater_rays_brighten_the_young_crater(lunar_models):
    terrain, craters = rayed_terrain(lunar_models)
    base = terraingen.evaluate_albedo(noisy_albedo(), terrain.source.grid())[0].values
    rayed = terraingen.evaluate_albedo(rayed_albedo(craters), terrain.source.grid(), threads=2)[0].values
    assert rayed[64, 64] > base[64, 64]


def test_albedo_keeps_its_layers(lunar_models):
    _, craters = rayed_terrain(lunar_models)
    assert len(rayed_albedo(craters).layers) == 1


def test_rgb_synthetic_albedo_has_three_bands():
    rgb = terraingen.evaluate_albedo(
        terraingen.Albedo(terraingen.SyntheticAlbedo(rgb=True, hue=30.0, saturation=0.2)),
        terraingen.RasterGrid.centred(129, 129, 8.0),
    )
    assert len(rgb) == 3
    on_a_plane = terraingen.SyntheticAlbedo(rgb=True, hue=30.0, saturation=0.3, surface=terraingen.Surface())
    assert on_a_plane.bands() == 3


def albedo_map():
    values = 0.12 + 0.01 * np.random.default_rng(1).random((33, 33))
    return terraingen.Raster.from_array(
        values.astype(np.float32), terraingen.RasterGrid.centred(33, 33, 8.0).transform
    )


def raster_albedo():
    detail = terraingen.RasterAlbedoOptions(
        method=terraingen.ResampleMethod.CUBIC, detail=terraingen.AlbedoDetail(octaves=3, seed=2)
    )
    return terraingen.RasterAlbedo([albedo_map()], detail)


def test_raster_albedo_measures_its_roughness():
    source = raster_albedo()
    assert 0.0 <= source.hurst <= 1.0 and source.cell_roughness > 0


def test_raster_albedo_keeps_the_map_mean_on_a_finer_grid():
    band = terraingen.evaluate_albedo(
        terraingen.Albedo(raster_albedo()), terraingen.RasterGrid.centred(132, 132, 2.0)
    )[0].values
    assert np.nanmean(band) == pytest.approx(0.125, abs=0.01)


def test_crater_albedo_options_keep_nested_settings():
    rays = terraingen.CraterRays(oblique=terraingen.ObliqueImpacts(heading=90.0), excess=0.06)
    options = terraingen.CraterAlbedoOptions(
        seed=3, rays=rays, retention=terraingen.RayRetention(scale=2.0), interior_excess=0.2
    )
    assert options.rays.oblique.heading == 90.0 and options.retention.myr(500.0) == pytest.approx(10.0)


def test_ray_median_length_exceeds_the_diameter():
    rays = terraingen.CraterRays(oblique=terraingen.ObliqueImpacts(heading=90.0), excess=0.06)
    assert rays.median_length(2000.0) > 2000.0


def test_crater_albedo_options_select_craters():
    assert terraingen.CraterAlbedoOptions(min_diameter=100.0).selection.min_diameter == 100.0


@pytest.mark.parametrize(
    ("validate", "bad"),
    [
        (terraingen.validate_crater_rays, terraingen.CraterRays(excess=-1.0)),
        (terraingen.validate_albedo_detail, terraingen.AlbedoDetail(octaves=0)),
    ],
    ids=["crater_rays", "albedo_detail"],
)
def test_albedo_validators_reject_bad_options(validate, bad):
    with pytest.raises(ValueError):
        validate(bad)


def test_python_albedo_source_and_layer_evaluate():
    albedo = terraingen.Albedo(Stripes(), [Halve()])
    band = terraingen.evaluate_albedo(albedo, terraingen.RasterGrid.centred(8, 8, 5.0))[0].values
    assert set(np.unique(band)) == {np.float32(0.05), np.float32(0.1)}


def test_python_albedo_layer_answers_point_queries():
    assert Halve().apply_at([0.2], 1, 0.0, 0.0, terraingen.AlbedoContext()) == pytest.approx([0.1])


def test_crater_albedo_leaves_values_far_from_craters(custom_craters):
    crater_albedo = terraingen.CraterAlbedo(custom_craters())
    assert crater_albedo.apply_at([0.1], 1, 500.0, 500.0, terraingen.AlbedoContext()) == pytest.approx([0.1])


def test_crater_albedo_texture_options():
    texture = terraingen.CraterAlbedoTexture(streaks=0.8, continuous_radii=0.7, lobes=0.3, floor_share=0.5)
    options = terraingen.CraterAlbedoOptions(texture=texture)
    assert (options.texture.streaks, options.texture.lobe_count) == (0.8, 8)
    with pytest.raises(ValueError):
        terraingen.validate_crater_albedo_texture(terraingen.CraterAlbedoTexture(lobes=0.95))


def test_crater_albedo_reflectance_zones(lunar_models):
    zones = terraingen.CraterReflectanceZones(enabled=True, max_diameter=300.0)
    assert zones.distal_low.share == pytest.approx(0.84) and zones.max_reach(20.0) > 2000.0
    layer = terraingen.CraterAlbedo(
        manual_crater_layer(lunar_models), terraingen.CraterAlbedoOptions(zones=zones)
    )
    assert layer.reflectance_change(young_crater(), 0.0, 0.0, 1.0) > 0.0
    plain = terraingen.CraterAlbedo(manual_crater_layer(lunar_models))
    assert plain.reflectance_change(young_crater(), 0.0, 0.0, 1.0) == 0.0
    with pytest.raises(ValueError):
        terraingen.validate_reflectance_zone(terraingen.ReflectanceZone(feather=0.0))
    with pytest.raises(ValueError):
        terraingen.validate_crater_reflectance_zones(terraingen.CraterReflectanceZones(max_diameter=0.0))


def test_splotches_darken_most_of_their_ground():
    options = terraingen.SplotchOptions(min_diameter=2.0, seed=3)
    assert options.coefficient() == pytest.approx(3.25, rel=0.02)
    layer = terraingen.SplotchAlbedo(options)
    assert layer.count((0.0, 0.0, 100.0, 100.0), 0.5) > 50 and layer.options.dark_share == pytest.approx(
        0.907
    )
    albedo = terraingen.Albedo(terraingen.SyntheticAlbedo(value=0.11), [layer])
    band = terraingen.evaluate_albedo(albedo, terraingen.RasterGrid.centred(64, 64, 1.0))[0].values
    assert (band < 0.1099).sum() > (band > 0.1101).sum() > 0
    with pytest.raises(ValueError):
        terraingen.validate_splotches(terraingen.SplotchOptions(softness=0.0))


def test_albedo_layers_read_the_terrain_heights(lunar_models):
    shadows = terraingen.ZoneShadows(enabled=True, jet_angle_degrees=5.0)
    zones = terraingen.CraterReflectanceZones(enabled=True, max_diameter=300.0, shadows=shadows)
    layer = terraingen.CraterAlbedo(
        manual_crater_layer(lunar_models), terraingen.CraterAlbedoOptions(zones=zones)
    )
    flat = terraingen.Terrain(terraingen.FlatSource(5, 5, 2.0), [])
    alone = layer.reflectance_change(young_crater(), 0.0, 0.0, 1.0)
    assert layer.reflectance_change(young_crater(), 0.0, 0.0, 1.0, terrain=flat) == pytest.approx(alone)
    splotches = terraingen.SplotchAlbedo(terraingen.SplotchOptions(min_diameter=2.0, steep_bright_share=1.0))
    albedo = terraingen.Albedo(terraingen.SyntheticAlbedo(value=0.11), [layer, splotches])
    grid = terraingen.RasterGrid.centred(16, 16, 2.0)
    with_terrain = terraingen.evaluate_albedo(albedo, grid, terrain=flat)[0].values
    assert np.array_equal(with_terrain, terraingen.evaluate_albedo(albedo, grid)[0].values)
    assert terraingen.evaluate_albedo_at(albedo, 1.0, 1.0, 2.0, terrain=flat) == pytest.approx(
        terraingen.evaluate_albedo_at(albedo, 1.0, 1.0, 2.0)
    )
