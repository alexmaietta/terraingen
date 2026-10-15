import math

import numpy as np
import pytest

import terraingen


def test_crater_distributions_and_chronologies():
    trask = terraingen.trask_equilibrium()
    assert trask.cumulative(10.0) == pytest.approx(10**-1.1 * 10.0**-2, rel=1e-6)
    hartmann = terraingen.hartmann_equilibrium()
    assert hartmann.cumulative(100.0) > 0
    for terrain in terraingen.MooreTerrain:
        assert terraingen.moore_distribution(terrain).cumulative(10.0) > 0
    assert terraingen.viper_specification().cumulative(10.0) > 0
    production = terraingen.neukum_production_function()
    assert production.kind == terraingen.SfdKind.PRODUCTION_RATE
    assert terraingen.extended_neukum_production_function().cumulative(5.0) > production.cumulative(20.0)
    assert terraingen.neukum_lunar_chronology(3.8) > terraingen.neukum_lunar_chronology(1.0) > 0
    assert terraingen.ivanov_mars_chronology(3.0) > 0
    assert terraingen.ivanov_mars_production_function().cumulative(100.0) > 0

    surface = terraingen.ProductionSurface(terraingen.extended_neukum_production_function(), 3.5)
    assert surface.age_gyr == 3.5 and surface.kind == terraingen.SfdKind.DENSITY
    doubled = terraingen.ProductionSurface(
        terraingen.extended_neukum_production_function(), 3.5, lambda age: 2 * terraingen.neukum_lunar_chronology(age)
    )
    assert doubled.cumulative(100.0) == pytest.approx(2 * surface.cumulative(100.0), rel=1e-9)
    capped = terraingen.MinimumSfd([surface, trask])
    assert capped.cumulative(5.0) == pytest.approx(min(surface.cumulative(5.0), trask.cumulative(5.0)))
    lunar = terraingen.lunar_surface(3.5)
    assert lunar.density.cumulative(5.0) == pytest.approx(capped.cumulative(5.0), rel=1e-6)
    assert terraingen.mars_surface(3.0).density.cumulative(100.0) > 0


def test_custom_shapes_of_distributions():
    power = terraingen.PowerLawSfd(0.05, 2.0, min_diameter=1.0, max_diameter=100.0)
    assert (power.coefficient, power.exponent, power.valid_range) == (0.05, 2.0, (1.0, 100.0))
    with pytest.raises(ValueError):
        power.cumulative(1000.0)
    power.allows_extrapolation = True
    assert power.cumulative(1000.0) == pytest.approx(0.05e-6)
    assert terraingen.power_law_coefficient_from_kilometres(1.0, 2.0) == pytest.approx(1e-6 * 1e6)
    piecewise = terraingen.PiecewisePowerLawSfd([(1.0, 0.1, 2.0), (10.0, 0.1, 2.0)], min_diameter=1.0)
    assert piecewise.cumulative(20.0) == pytest.approx(0.1 / 400)
    table = terraingen.TabulatedSfd([1.0, 10.0, 100.0], [1e-1, 1e-3, 1e-5])
    assert table.cumulative(10.0) == pytest.approx(1e-3)
    summed = terraingen.SummedSfd([(1.0, power), (2.0, power)])
    assert summed.cumulative(10.0) == pytest.approx(3 * power.cumulative(10.0))
    spliced = terraingen.SplicedSfd([(1.0, power), (10.0, terraingen.PowerLawSfd(0.05, 2.0))], 1.0, 100.0)
    assert spliced.cumulative(50.0) == pytest.approx(power.cumulative(50.0))
    poly = terraingen.LogPolynomialSfd([-2.0, -2.0], 1.0, 100.0)
    assert poly.cumulative(10.0) == pytest.approx(1e-4)
    boulders = terraingen.GolombekRappSfd(0.05)
    assert boulders.coverage == 0.05 and boulders.q == pytest.approx(1.79 + 0.152 / 0.05)
    assert boulders.fractional_area(0.0) == pytest.approx(0.05)
    blocks = [terraingen.sp8023_intercrater_blocks(c) for c in terraingen.SpecificationCurve]
    blocks += [terraingen.sp8023_crater_blocks(c) for c in terraingen.SpecificationCurve]
    blocks += [terraingen.synthterrain_boulder_distribution(kind) for kind in terraingen.SynthterrainBoulders]
    for sfd in blocks:
        low, high = sfd.valid_range
        low = max(low, 0.1)
        assert sfd.cumulative(math.sqrt(low * min(high, 100.0 * low))) > 0
    stream = terraingen.RandomStream(3, 4)
    draws = [power.sample_diameter(stream, 2.0, 20.0) for _ in range(200)]
    assert min(draws) >= 2.0 and max(draws) < 20.0


def test_ages_degradation_and_profiles():
    steady = terraingen.SteadyStateAges(
        terraingen.extended_neukum_production_function(), terraingen.trask_equilibrium(), 3.5
    )
    assert steady.surface_age_gyr == 3.5
    assert steady.lifetime_gyr(10.0) < steady.lifetime_gyr(1000.0) <= 3.5
    assert steady.equilibrium_lifetime_gyr(10.0) == pytest.approx(steady.lifetime_gyr(10.0))
    assert terraingen.UniformAges(2.0).remaining_lifetime(10.0, 1.0) == pytest.approx(0.5)

    fresh = terraingen.lunar_fresh_crater_morphometry(100.0)
    assert 0.1 < fresh.depth_ratio < 0.25 and fresh.rim_height_ratio > 0
    assert terraingen.mars_fresh_crater_morphometry(100.0).depth_ratio > 0
    crater = terraingen.Crater(0.0, 0.0, 100.0, age_gyr=1.0)
    assert terraingen.NoDegradation().state(crater).current.depth_ratio == pytest.approx(fresh.depth_ratio)
    assert terraingen.NoDegradation(depth_ratio=0.1).state(crater).current.depth_ratio == pytest.approx(0.1)
    mars = terraingen.NoDegradation(fresh=terraingen.mars_fresh_crater_morphometry)
    assert mars.state(crater).fresh.depth_ratio == pytest.approx(terraingen.mars_fresh_crater_morphometry(100.0).depth_ratio)
    freshness = terraingen.FreshnessDegradation(terraingen.UniformAges(2.0))
    assert freshness.state(crater).current.depth_ratio < fresh.depth_ratio

    law = terraingen.fassett_2022_hartmann_diffusivity()
    assert law(100.0, 1.0) > law(100.0, 0.5) > 0 and law.diffusivity(100.0) > 0
    assert terraingen.fassett_thomson_2014_diffusivity().coefficient == pytest.approx(5.5)
    assert terraingen.fassett_2022_trask_diffusivity().exponent > 0
    custom = terraingen.PowerLawDiffusivity(coefficient=5.5, exponent=0.0)
    assert custom.coefficient == 5.5
    diffused = terraingen.DiffusedProfile(terraingen.FassettThomsonProfile())
    for diffusivity in (law, lambda diameter, age: 50.0 * age):
        diffusion = terraingen.DiffusionDegradation(diffusivity, diffused)
        state = diffusion.state(crater)
        assert state.diffusion > 0 and state.current.depth_ratio < state.fresh.depth_ratio

    simple = terraingen.FassettThomsonProfile(terraingen.FassettThomsonRim.PLATEAU)
    state = terraingen.CraterState(fresh, fresh, 0.0)
    assert simple.height(0.0, 100.0, state) < 0 < simple.height(50.0, 100.0, state)
    assert simple.reach_radii() == pytest.approx(1.5) and not simple.is_complex(1e6)
    martin = terraingen.MartinParkesDunstanProfile()
    assert martin.height(0.0, 100.0, state) < 0 and martin.reach_radii() == pytest.approx(2.0)
    shape = terraingen.lunar_complex_crater_shape()
    assert shape.transition_diameter == pytest.approx(15000.0)
    assert terraingen.mars_complex_crater_shape().transition_diameter == pytest.approx(7000.0)
    complex_profile = terraingen.ComplexCraterProfile(simple, shape)
    assert complex_profile.is_complex(30000.0) and not complex_profile.is_complex(1000.0)
    assert complex_profile.shape.transition_diameter == shape.transition_diameter
    custom_shape = terraingen.ComplexCraterShape(transition_diameter=5000.0, peak_height=terraingen.KilometrePowerLaw(0.1, 1.0))
    assert custom_shape.peak_height.coefficient == 0.1


def test_crater_options_secondaries_and_positions():
    surface = terraingen.lunar_surface(3.5)
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )
    assert models.profile.reach_radii() == pytest.approx(1.5)
    region = (-200.0, -200.0, 200.0, 200.0)

    def craters(**generation):
        options = terraingen.CraterLayerOptions(
            seed=2, generation=terraingen.CraterGenerationOptions(min_diameter=10.0, max_diameter=100.0, **generation)
        )
        return terraingen.CraterLayer(models, options).craters(region)

    loose = craters(spacing=terraingen.CraterSpacing(separation=0.0))
    spaced = craters(spacing=terraingen.CraterSpacing(diameter_ratio=2.0, separation=1.0))
    capped = craters(max_craters=10)
    assert len(spaced["x"]) < len(loose["x"]) and len(capped["x"]) == 10

    apply = terraingen.CraterApplyOptions(
        order=terraingen.CraterOrder.LARGEST_FIRST,
        datum=terraingen.CraterDatum.MEAN_HEIGHT,
        interior_replacement=0.5,
        blend=terraingen.CraterBlend(start_radii=0.7, end_radii=1.3, shape=terraingen.BlendShape.LINEAR),
        fade=terraingen.ResolutionFade(start_samples=1.0, end_samples=2.0),
        irregularity=terraingen.CraterIrregularity(
            enabled=True,
            roughness=terraingen.ValueDistribution(terraingen.ValueDistributionKind.UNIFORM, 0.02, 0.05),
            elongated_share=0.5,
        ),
    )
    assert apply.irregularity.roughness.b == 0.05 and apply.blend.shape == terraingen.BlendShape.LINEAR
    layer = terraingen.CraterLayer(
        models, terraingen.CraterLayerOptions(generation=terraingen.CraterGenerationOptions(min_diameter=10.0, max_diameter=100.0), apply=apply)
    )
    noise = terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=200.0, amplitude=5.0))])
    heights = terraingen.evaluate(terraingen.Terrain(terraingen.FlatSource(129, 129, 2.0), [noise, layer])).heights.values
    assert np.isfinite(heights).all()
    with pytest.raises(AttributeError):
        terraingen.CraterApplyOptions(bogus=1)

    primary = terraingen.CraterLayer(
        models,
        terraingen.CraterLayerOptions(
            generate=False, generation=terraingen.CraterGenerationOptions(manual=[terraingen.Crater(0.0, 0.0, 2000.0, age_gyr=0.01)])
        ),
    )
    options = terraingen.SecondaryCraterOptions(seed=4, onset_count=20.0, min_diameter=20.0)
    secondaries = terraingen.SecondaryCraterLayer(primary, terraingen.FassettThomsonProfile(), terraingen.NoDegradation(depth_ratio=0.11), options)
    outer = secondaries.outer_range(2000.0)
    table = secondaries.secondaries((-outer, -outer, outer, outer))
    ranges = np.hypot(table["x"], table["y"])
    assert len(ranges) > 50 and ranges.min() >= terraingen.secondary_onset_range(2000.0)
    assert terraingen.singer_max_secondary_diameter(2000.0, 5000.0) > 20.0


def test_other_layers_and_boulder_options():
    flat = terraingen.FlatSource(65, 65, 1.0)
    bump = terraingen.ScaleLayer(1.0, 5.0)
    region = terraingen.RegionLayer(bump, terraingen.RegionShape.circle(0.0, 0.0, 10.0), feather=5.0)
    heights = terraingen.evaluate(terraingen.Terrain(flat, [region])).heights.values
    assert heights[32, 32] == pytest.approx(5.0) and heights[0, 0] == 0.0
    assert region.weight(0.0, 0.0) == 1.0 and region.weight(30.0, 0.0) == 0.0
    assert terraingen.RegionShape.rectangle(0, 0, 10, 10).distance_outside(15.0, 5.0) == pytest.approx(5.0)
    clamped = terraingen.evaluate(terraingen.Terrain(flat, [bump, terraingen.ClampLayer(max=2.0)])).heights.values
    assert clamped.max() == 2.0
    rough = terraingen.RoughnessLayer(terraingen.RoughnessOptions(rms_slope=0.2, seed=3))
    assert rough.rms_slope(0.25) == pytest.approx(0.2, rel=0.05)
    assert np.ptp(terraingen.evaluate(terraingen.Terrain(terraingen.FlatSource(65, 65, 0.05), [rough])).heights.values) > 0
    ripples = terraingen.mars_large_ripples()
    ripples.cover = 0.9
    bedforms = terraingen.BedformLayer(ripples)
    assert bedforms.cover_at(0.0, 0.0) >= 0.0
    assert terraingen.mars_transverse_aeolian_ridges().wavelength > ripples.wavelength

    options = terraingen.BoulderLayerOptions(
        coverage=0.08,
        min_diameter=0.2,
        max_diameter=2.0,
        distribution=terraingen.synthterrain_boulder_distribution(terraingen.SynthterrainBoulders.HAWORTH),
        patches=terraingen.BoulderPatches(spread=1.0),
        min_burial=0.0,
        max_burial=0.0,
        relief=terraingen.BoulderRelief.DOME,
        models=terraingen.BoulderModelThreshold(min_diameter=1.0),
        exposure=terraingen.ExposureParams(reach=6.0),
    )
    assert options.patches.spread == 1.0 and options.exposure.reach == 6.0
    options.exposure = None
    layer = terraingen.BoulderLayer(options)
    boulders = layer.boulders((-10.0, -10.0, 10.0, 10.0))
    assert len(boulders["x"]) > 0 and (boulders["burial"] == 0.0).all()
    assert layer.abundance_at(0.0, 0.0) == (1.0, 0.0)
    ejecta = terraingen.BoulderEjecta(min_crater_diameter=5.0, coverage=0.3, profile=terraingen.EjectaProfile.EXPONENTIAL)
    assert ejecta.profile == terraingen.EjectaProfile.EXPONENTIAL and ejecta.aging == terraingen.EjectaAging.FRESHNESS
