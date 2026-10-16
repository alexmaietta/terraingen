"""Crater models: age models, degradation, profiles, and models written in Python."""

import numpy as np
import pytest

import terraingen


@pytest.fixture
def steady():
    return terraingen.SteadyStateAges(
        terraingen.extended_neukum_production_function(),
        terraingen.trask_equilibrium(),
        3.5,
        terraingen.neukum_lunar_chronology,
    )


@pytest.fixture
def crater():
    return terraingen.Crater(0.0, 0.0, 100.0, age_gyr=1.0)


@pytest.fixture
def fresh_state():
    fresh = terraingen.lunar_fresh_crater_morphometry(100.0)
    return terraingen.CraterState(fresh, fresh, 0.0)


def test_steady_state_ages_report_the_surface_age(steady):
    assert steady.surface_age_gyr == 3.5


def test_steady_state_lifetime_grows_with_diameter_up_to_the_surface_age(steady):
    assert steady.lifetime_gyr(10.0) < steady.lifetime_gyr(1000.0) <= 3.5


def test_steady_state_equilibrium_lifetime_matches_the_lifetime(steady):
    assert steady.equilibrium_lifetime_gyr(10.0) == pytest.approx(steady.lifetime_gyr(10.0))


def test_uniform_ages_remaining_lifetime():
    assert terraingen.UniformAges(2.0).remaining_lifetime(10.0, 1.0) == pytest.approx(0.5)


def test_no_degradation_keeps_the_fresh_depth_ratio(crater):
    fresh = terraingen.lunar_fresh_crater_morphometry(100.0)
    assert terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry).state(
        crater
    ).current.depth_ratio == pytest.approx(fresh.depth_ratio)


def test_no_degradation_can_fix_the_depth_ratio(crater):
    assert terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry, depth_ratio=0.1).state(
        crater
    ).current.depth_ratio == pytest.approx(0.1)


def test_no_degradation_takes_a_fresh_morphometry_law(crater):
    mars = terraingen.NoDegradation(terraingen.mars_fresh_crater_morphometry)
    assert mars.state(crater).fresh.depth_ratio == pytest.approx(
        terraingen.mars_fresh_crater_morphometry(100.0).depth_ratio
    )


def test_freshness_degradation_shallows_old_craters(crater):
    fresh = terraingen.lunar_fresh_crater_morphometry(100.0)
    freshness = terraingen.FreshnessDegradation(
        terraingen.UniformAges(2.0), terraingen.lunar_fresh_crater_morphometry
    )
    assert freshness.state(crater).current.depth_ratio < fresh.depth_ratio


def test_power_law_diffusivity_keeps_its_coefficient():
    assert terraingen.PowerLawDiffusivity(coefficient=5.5, exponent=0.0).coefficient == 5.5


@pytest.mark.parametrize(
    "diffusivity",
    [terraingen.fassett_2022_hartmann_diffusivity(), lambda diameter, age: 50.0 * age],
    ids=["fassett_2022_hartmann", "python"],
)
def test_diffusion_degradation_shallows_craters(diffusivity, crater):
    diffused = terraingen.DiffusedProfile(terraingen.FassettThomsonProfile())
    state = terraingen.DiffusionDegradation(
        terraingen.lunar_fresh_crater_morphometry, diffusivity, diffused
    ).state(crater)
    assert state.diffusion > 0 and state.current.depth_ratio < state.fresh.depth_ratio


def test_crater_state_freshness_is_the_fraction_of_fresh_depth():
    state = terraingen.CraterState(
        terraingen.CraterMorphometry(0.2, 0.04), terraingen.CraterMorphometry(0.1, 0.02)
    )
    assert state.freshness == pytest.approx(0.5)


def test_fassett_thomson_plateau_profile_has_a_bowl_and_a_rim(fresh_state):
    simple = terraingen.FassettThomsonProfile(terraingen.FassettThomsonRim.PLATEAU)
    assert simple.height(0.0, 100.0, fresh_state) < 0 < simple.height(50.0, 100.0, fresh_state)


def test_fassett_thomson_profile_is_simple_and_reaches_one_and_a_half_radii():
    simple = terraingen.FassettThomsonProfile(terraingen.FassettThomsonRim.PLATEAU)
    assert simple.reach_radii() == pytest.approx(1.5) and not simple.is_complex(1e6)


def test_martin_parkes_dunstan_profile_has_a_bowl_and_reaches_two_radii(fresh_state):
    martin = terraingen.MartinParkesDunstanProfile()
    assert martin.height(0.0, 100.0, fresh_state) < 0 and martin.reach_radii() == pytest.approx(2.0)


def test_complex_profile_switches_at_the_transition_diameter():
    shape = terraingen.lunar_complex_crater_shape()
    simple = terraingen.FassettThomsonProfile(terraingen.FassettThomsonRim.PLATEAU)
    complex_profile = terraingen.ComplexCraterProfile(simple, shape)
    assert complex_profile.is_complex(30000.0) and not complex_profile.is_complex(1000.0)
    assert complex_profile.shape.transition_diameter == shape.transition_diameter


def test_complex_crater_shape_takes_custom_laws():
    custom_shape = terraingen.ComplexCraterShape(
        transition_diameter=5000.0, peak_height=terraingen.KilometrePowerLaw(0.1, 1.0)
    )
    assert custom_shape.peak_height.coefficient == 0.1


def test_crater_models_keep_their_profile(lunar_models):
    assert lunar_models().profile.reach_radii() == pytest.approx(1.5)


def test_python_age_model_lifetime(linear_ages):
    assert linear_ages.lifetime_gyr(10.0) == pytest.approx(0.1)


def test_python_age_model_samples_ages_below_the_lifetime(linear_ages):
    assert 0.0 <= linear_ages.sample_age_gyr(terraingen.RandomStream(1, 2), 10.0) < 0.1


def test_python_age_model_can_override_sampling(oldest_ages):
    assert oldest_ages.sample_age_gyr(terraingen.RandomStream(1, 2), 10.0) == pytest.approx(0.1)


def test_python_models_drive_generated_craters(custom_craters):
    table = custom_craters().craters((-100.0, -100.0, 100.0, 100.0))
    assert len(table["diameter"]) > 10
    assert (table["diameter"] >= 4.0).all() and (table["age_gyr"] < 0.01 * table["diameter"]).all()
    np.testing.assert_allclose(table["depth_ratio"], 0.15, rtol=1e-6)


def test_python_age_model_sets_generated_ages(custom_craters, oldest_ages):
    old = custom_craters(oldest_ages).craters((-100.0, -100.0, 100.0, 100.0))
    np.testing.assert_allclose(old["age_gyr"], 0.01 * old["diameter"])


def test_python_profile_carves_bowls_without_rims(custom_craters):
    terrain = terraingen.Terrain(terraingen.FlatSource(65, 65, 2.0), [custom_craters()])
    heights = terraingen.evaluate(terrain, threads=4).heights.values
    assert heights.min() < -1.0 and heights.max() == 0.0


def test_python_models_evaluate_the_same_on_any_thread_count(custom_craters):
    terrain = terraingen.Terrain(terraingen.FlatSource(65, 65, 2.0), [custom_craters()])
    heights = terraingen.evaluate(terrain, threads=4).heights.values
    np.testing.assert_array_equal(terraingen.evaluate(terrain, threads=1).heights.values, heights)


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


def test_steady_state_ages_take_any_production_and_chronology():
    mars = terraingen.SteadyStateAges(
        terraingen.ivanov_mars_production_function(),
        terraingen.hartmann_equilibrium(),
        3.0,
        terraingen.ivanov_mars_chronology,
    )
    assert mars.surface_age_gyr == 3.0 and 0 < mars.lifetime_gyr(100.0) <= 3.0


@pytest.mark.parametrize(
    "build",
    [
        lambda: terraingen.NoDegradation(),
        lambda: terraingen.FreshnessDegradation(terraingen.UniformAges(1.0)),
        lambda: terraingen.DiffusionDegradation(
            terraingen.fassett_2022_hartmann_diffusivity(), terraingen.FassettThomsonProfile()
        ),
    ],
)
def test_degradation_models_need_a_fresh_morphometry(build):
    with pytest.raises(TypeError):
        build()


def test_freshness_degradation_takes_its_cutoff_after_the_morphometry(crater):
    deep = terraingen.FreshnessDegradation(
        terraingen.UniformAges(2.0), terraingen.mars_fresh_crater_morphometry, cutoff_depth_ratio=0.05
    )
    assert deep.state(crater).fresh.depth_ratio == pytest.approx(
        terraingen.mars_fresh_crater_morphometry(100.0).depth_ratio
    )
