"""bodies: radii, chronologies, production and equilibrium distributions, and per-body crater laws."""

import math

import pytest

import terraingen


def test_mean_radii_match_the_fact_sheets():
    moon = terraingen.body_sphere(terraingen.Body.MOON)
    assert moon.is_sphere and moon.radius == pytest.approx(1737400.0)
    assert terraingen.mean_radius(terraingen.Body.MARS) == pytest.approx(3389500.0)


def test_body_named_finds_known_bodies_only():
    assert terraingen.body_named("mars") == terraingen.Body.MARS and terraingen.body_named("pluto") is None


def test_trask_equilibrium_matches_its_power_law():
    assert terraingen.trask_equilibrium().cumulative(10.0) == pytest.approx(10**-1.1 * 10.0**-2, rel=1e-6)


def test_hartmann_equilibrium_is_positive():
    assert terraingen.hartmann_equilibrium().cumulative(100.0) > 0


@pytest.mark.parametrize("terrain", list(terraingen.MooreTerrain))
def test_moore_distributions_cover_every_terrain(terrain):
    assert terraingen.moore_distribution(terrain).cumulative(10.0) > 0


def test_viper_specification_is_positive():
    assert terraingen.viper_specification().cumulative(10.0) > 0


def test_neukum_production_function_is_a_rate():
    assert terraingen.neukum_production_function().kind == terraingen.SfdKind.PRODUCTION_RATE


def test_extended_neukum_production_function_reaches_smaller_craters():
    production = terraingen.neukum_production_function()
    assert terraingen.extended_neukum_production_function().cumulative(5.0) > production.cumulative(20.0)


def test_lunar_chronology_grows_with_age():
    assert terraingen.neukum_lunar_chronology(3.8) > terraingen.neukum_lunar_chronology(1.0) > 0


def test_lunar_chronology_rate_grows_with_age():
    assert terraingen.neukum_lunar_chronology_rate(3.8) > terraingen.neukum_lunar_chronology_rate(1.0) > 0


def test_mars_chronology_and_its_rate_are_positive():
    assert terraingen.ivanov_mars_chronology(3.0) > 0
    assert terraingen.ivanov_mars_chronology_rate(3.0) > 0


def test_mars_production_function_is_positive():
    assert terraingen.ivanov_mars_production_function().cumulative(100.0) > 0


def test_lunar_surface_density_caps_production_at_trask_equilibrium():
    production = terraingen.ProductionSurface(terraingen.extended_neukum_production_function(), 3.5)
    capped = terraingen.MinimumSfd([production, terraingen.trask_equilibrium()])
    lunar = terraingen.lunar_surface(3.5)
    assert lunar.density.cumulative(5.0) == pytest.approx(capped.cumulative(5.0), rel=1e-6)


def test_lunar_surface_density_falls_with_diameter():
    surface = terraingen.lunar_surface(3.5)
    assert surface.density.cumulative(10.0) > surface.density.cumulative(100.0) > 0
    assert surface.density.expected_count(1e6, 10.0, 100.0) > 0


def test_mars_surface_density_is_positive():
    assert terraingen.mars_surface(3.0).density.cumulative(100.0) > 0


def test_lunar_fresh_craters_are_simple_bowls_with_rims():
    fresh = terraingen.lunar_fresh_crater_morphometry(100.0)
    assert 0.1 < fresh.depth_ratio < 0.25 and fresh.rim_height_ratio > 0


def test_lunar_fresh_depth_ratio_matches_the_morphometry():
    assert terraingen.lunar_fresh_crater_depth_ratio(100.0) == pytest.approx(
        terraingen.lunar_fresh_crater_morphometry(100.0).depth_ratio
    )


def test_mars_fresh_crater_depth_ratio_is_positive():
    assert terraingen.mars_fresh_crater_morphometry(100.0).depth_ratio > 0


def test_complex_crater_transition_diameters():
    assert terraingen.lunar_complex_crater_shape().transition_diameter == pytest.approx(15000.0)
    assert terraingen.mars_complex_crater_shape().transition_diameter == pytest.approx(7000.0)


def test_fassett_2022_hartmann_diffusivity_grows_with_age():
    law = terraingen.fassett_2022_hartmann_diffusivity()
    assert law(100.0, 1.0) > law(100.0, 0.5) > 0 and law.diffusivity(100.0) > 0


def test_fassett_thomson_2014_diffusivity_coefficient():
    assert terraingen.fassett_thomson_2014_diffusivity().coefficient == pytest.approx(5.5)


def test_fassett_2022_trask_diffusivity_exponent_is_positive():
    assert terraingen.fassett_2022_trask_diffusivity().exponent > 0


def test_fassett_thomson_2014_diffusion_age_is_positive():
    assert terraingen.fassett_thomson_2014_diffusion_age(3.0) > 0


def boulder_specifications():
    blocks = [terraingen.sp8023_intercrater_blocks(c) for c in terraingen.SpecificationCurve]
    blocks += [terraingen.sp8023_crater_blocks(c) for c in terraingen.SpecificationCurve]
    blocks += [terraingen.synthterrain_boulder_distribution(kind) for kind in terraingen.SynthterrainBoulders]
    return blocks


@pytest.mark.parametrize("sfd", boulder_specifications())
def test_boulder_specifications_are_positive_in_their_range(sfd):
    low, high = sfd.valid_range
    low = max(low, 0.1)
    assert sfd.cumulative(math.sqrt(low * min(high, 100.0 * low))) > 0


def test_mars_transverse_aeolian_ridges_are_longer_than_large_ripples():
    ripples = terraingen.mars_large_ripples()
    assert terraingen.mars_transverse_aeolian_ridges().wavelength > ripples.wavelength
