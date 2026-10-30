# SPDX-License-Identifier: BSD-3-Clause

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
    assert steady.lifetime_at(10.0) < steady.lifetime_at(1000.0) <= 3.5


def test_steady_state_equilibrium_lifetime_matches_the_lifetime(steady):
    assert steady.equilibrium_lifetime_gyr(10.0) == pytest.approx(steady.lifetime_at(10.0))


def test_uniform_ages_remaining_lifetime():
    assert terraingen.UniformAges(2.0).remaining_lifetime(10.0, 1.0) == pytest.approx(0.5)


def test_freshness_ages_draw_freshness_from_a_distribution():
    normal = terraingen.ValueDistribution(terraingen.ValueDistribution.Kind.NORMAL, 0.35, 0.1)
    ages = terraingen.FreshnessAges(normal, lifetime_gyr=2.0)
    assert ages.freshness.min == 0.0 and ages.freshness.max == 1.0
    assert ages.lifetime_at(5.0) == ages.lifetime_at(500.0) == 2.0
    stream = terraingen.RandomStream(3, 4)
    freshness = np.array(
        [ages.remaining_lifetime(10.0, ages.sample_age_gyr(stream, 10.0)) for _ in range(20000)]
    )
    assert freshness.mean() == pytest.approx(0.35, abs=0.004)
    assert freshness.std() == pytest.approx(0.1, abs=0.004)
    assert 0.0 <= ages.sample_freshness(stream) <= 1.0


def test_freshness_ages_take_piecewise_shapes():
    shape = terraingen.ValueDistribution(
        terraingen.ValueDistribution.Kind.PIECEWISE, edges=[0.0, 0.5, 1.0], weights=[3.0, 1.0]
    )
    terraingen.validate_value_distribution(shape)
    ages = terraingen.FreshnessAges(shape, 1.0)
    stream = terraingen.RandomStream(5, 6)
    freshness = np.array([ages.sample_freshness(stream) for _ in range(20000)])
    assert (freshness < 0.5).mean() == pytest.approx(0.75, abs=0.01)
    with pytest.raises(ValueError):
        terraingen.FreshnessAges(
            terraingen.ValueDistribution(terraingen.ValueDistribution.Kind.PIECEWISE), 1.0
        )
    with pytest.raises(ValueError):
        terraingen.FreshnessAges(shape, 0.0)


def test_freshness_ages_drive_freshness_degradation():
    ages = terraingen.FreshnessAges(
        terraingen.ValueDistribution(terraingen.ValueDistribution.Kind.CONSTANT, 0.25), 2.0
    )
    degradation = terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry)
    young = degradation.state(terraingen.Crater(0.0, 0.0, 100.0, age_gyr=0.0)).current.depth_ratio
    drawn = ages.sample_age_gyr(terraingen.RandomStream(1, 1), 100.0)
    assert drawn == pytest.approx(1.5)
    worn = degradation.state(terraingen.Crater(0.0, 0.0, 100.0, age_gyr=drawn)).current.depth_ratio
    assert worn < young


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


def test_diffusion_degradation_shapes_craters_with_its_own_profile():
    diffused = terraingen.DiffusedProfile(terraingen.FassettThomsonProfile())
    degradation = terraingen.DiffusionDegradation(
        terraingen.lunar_fresh_crater_morphometry,
        terraingen.fassett_2022_hartmann_diffusivity(),
        diffused,
    )
    assert degradation.required_profile() is not None
    assert terraingen.matched_profile(None, degradation) is not None
    with pytest.raises(ValueError):
        terraingen.matched_profile(terraingen.FassettThomsonProfile(), degradation)
    models = terraingen.CraterModels(
        terraingen.PowerLawSfd(0.05, 2.0), terraingen.UniformAges(1.0), degradation
    )
    layer = terraingen.CraterLayer(models)
    assert layer.models.profile is not None
    with pytest.raises(ValueError):
        terraingen.CraterLayer(
            terraingen.CraterModels(
                terraingen.PowerLawSfd(0.05, 2.0),
                terraingen.UniformAges(1.0),
                degradation,
                terraingen.FassettThomsonProfile(),
            )
        )


def test_a_crater_layer_given_no_ages_draws_those_its_degradation_wears_by():
    ages = terraingen.UniformAges(2.0)
    degradation = terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry)
    assert degradation.age_model() is not None
    models = terraingen.CraterModels(
        terraingen.PowerLawSfd(0.05, 2.0), None, degradation, terraingen.FassettThomsonProfile()
    )
    assert terraingen.CraterLayer(models).models.ages is not None


def test_an_equilibrium_surface_gives_its_density_and_matched_diffusion():
    surface = terraingen.EquilibriumSurface(
        terraingen.extended_neukum_production_function(),
        terraingen.hartmann_equilibrium(),
        3.5,
        terraingen.neukum_lunar_chronology,
    )
    ages = surface.ages()
    assert ages.surface_age_gyr == 3.5 and surface.density().cumulative(100.0) > 0
    diffused = terraingen.DiffusedProfile(terraingen.FassettThomsonProfile())
    degradation = terraingen.matched_diffusion_degradation(
        diffused, terraingen.lunar_fresh_crater_morphometry, ages
    )
    lifetime = ages.equilibrium_lifetime_gyr(20.0)
    state = degradation.state(terraingen.Crater(0.0, 0.0, 20.0, age_gyr=lifetime))
    assert state.current.depth_ratio == pytest.approx(terraingen.VISIBILITY_DEPTH_RATIO, abs=1e-3)


def test_a_crater_layer_without_a_profile_shapes_with_martin_parkes_and_dunstans():
    ages = terraingen.UniformAges(2.0)
    degradation = terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry)
    layer = terraingen.CraterLayer(
        terraingen.CraterModels(terraingen.PowerLawSfd(0.05, 2.0), ages, degradation)
    )
    assert isinstance(layer.models.profile, terraingen.MartinParkesDunstanProfile)
    assert isinstance(terraingen.matched_profile(None, degradation), terraingen.MartinParkesDunstanProfile)


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
        transition_diameter=5000.0, peak_height=terraingen.KilometerPowerLaw(0.1, 1.0)
    )
    assert custom_shape.peak_height.coefficient == 0.1


def test_crater_models_keep_their_profile(lunar_models):
    assert lunar_models().profile.reach_radii() == pytest.approx(1.5)


def test_python_age_model_lifetime(linear_ages):
    assert linear_ages.lifetime_at(10.0) == pytest.approx(0.1)


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
    terrain = terraingen.Terrain(source=terraingen.FlatSource(65, 65, 2.0), layers=[custom_craters()])
    heights = terrain.evaluate(threads=4).values
    assert heights.min() < -1.0 and heights.max() == 0.0


def test_python_models_evaluate_the_same_on_any_thread_count(custom_craters):
    terrain = terraingen.Terrain(source=terraingen.FlatSource(65, 65, 2.0), layers=[custom_craters()])
    heights = terrain.evaluate(threads=4).values
    np.testing.assert_array_equal(terrain.evaluate(threads=1).values, heights)


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


def test_equilibrium_diffusion_ages_wear_craters_to_the_cutoff_at_their_lifetimes(steady):
    diffused = terraingen.DiffusedProfile(terraingen.FassettThomsonProfile())
    fresh = terraingen.lunar_fresh_crater_morphometry
    ages = terraingen.equilibrium_diffusion_ages(diffused, fresh, steady.equilibrium_lifetime_gyr)
    lifetime = steady.equilibrium_lifetime_gyr(20.0)
    degradation = terraingen.DiffusionDegradation(fresh, ages, diffused)
    state = degradation.state(terraingen.Crater(0.0, 0.0, 20.0, age_gyr=lifetime))
    assert state.current.depth_ratio == pytest.approx(terraingen.VISIBILITY_DEPTH_RATIO, abs=1e-8)
    tau = diffused.age_at_depth_ratio(fresh(20.0), 20.0, terraingen.VISIBILITY_DEPTH_RATIO)
    assert ages(20.0, lifetime) == pytest.approx(tau * 400.0)


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
    assert mars.surface_age_gyr == 3.0 and 0 < mars.lifetime_at(100.0) <= 3.0


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
