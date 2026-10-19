"""distribution: size-frequency distribution shapes, combinations and Python subclasses."""

import math

import pytest

import terraingen


@pytest.fixture
def power():
    return terraingen.PowerLawSfd(0.05, 2.0, min_diameter=1.0, max_diameter=100.0)


def test_power_law_reports_its_parameters(power):
    assert (power.coefficient, power.exponent, power.valid_range) == (0.05, 2.0, (1.0, 100.0))


def test_power_law_rejects_diameters_outside_its_range_unless_extrapolating(power):
    with pytest.raises(ValueError):
        power.cumulative(1000.0)
    power.allows_extrapolation = True
    assert power.cumulative(1000.0) == pytest.approx(0.05e-6)


def test_power_law_coefficient_from_kilometres():
    assert terraingen.power_law_coefficient_from_kilometres(1.0, 2.0) == pytest.approx(1e-6 * 1e6)


def test_power_law_samples_diameters_in_the_requested_range(power):
    stream = terraingen.RandomStream(3, 4)
    draws = [power.sample_diameter(stream, 2.0, 20.0) for _ in range(200)]
    assert min(draws) >= 2.0 and max(draws) < 20.0


def test_piecewise_power_law_uses_the_segment_at_the_diameter():
    piecewise = terraingen.PiecewisePowerLawSfd([(1.0, 0.1, 2.0), (10.0, 0.1, 2.0)], min_diameter=1.0)
    assert piecewise.cumulative(20.0) == pytest.approx(0.1 / 400)


def test_piecewise_power_law_lists_its_segments():
    piecewise = terraingen.PiecewisePowerLawSfd([(0.0, 0.1, 2.0), (10.0, 1.0, 3.0)])
    assert [s.start for s in piecewise.segments] == [0.0, 10.0]


def test_tabulated_sfd_interpolates_its_table():
    table = terraingen.TabulatedSfd([1.0, 10.0, 100.0], [1e-1, 1e-3, 1e-5])
    assert table.cumulative(10.0) == pytest.approx(1e-3)


def test_tabulated_sfd_exposes_its_table():
    table = terraingen.TabulatedSfd([1.0, 10.0], [1e-1, 1e-3])
    assert table.diameters == [1.0, 10.0] and table.cumulative_values == pytest.approx([1e-1, 1e-3])


def test_piecewise_uniform_sfd_falls_linearly_across_its_bands():
    bands = terraingen.PiecewiseUniformSfd([(1.0, 2.0, 0.1), (3.0, 5.0, 0.01)])
    assert bands.cumulative(1.5) == pytest.approx(0.06)
    assert bands.cumulative(2.5) == pytest.approx(0.01)
    assert bands.cumulative(5.0) == 0.0


def test_piecewise_uniform_sfd_shares_a_total_by_weight():
    bands = terraingen.PiecewiseUniformSfd([(1.0, 2.0, 1.0), (2.0, 3.0, 3.0)], total=0.4)
    assert [b.density for b in bands.bands] == pytest.approx([0.1, 0.3])
    assert (bands.bands[0].min, bands.bands[0].max) == (1.0, 2.0)


def test_summed_sfd_weights_its_terms(power):
    summed = terraingen.SummedSfd([(1.0, power), (2.0, power)])
    assert summed.cumulative(10.0) == pytest.approx(3 * power.cumulative(10.0))


def test_summed_sfd_exposes_its_terms():
    assert terraingen.SummedSfd([(2.0, terraingen.PowerLawSfd(0.05, 2.0))]).terms[0][0] == 2.0


def test_spliced_sfd_uses_the_piece_covering_the_diameter(power):
    spliced = terraingen.SplicedSfd([(1.0, power), (10.0, terraingen.PowerLawSfd(0.05, 2.0))], 1.0, 100.0)
    assert spliced.cumulative(50.0) == pytest.approx(power.cumulative(50.0))


def test_spliced_sfd_exposes_its_pieces():
    assert len(terraingen.SplicedSfd([(1.0, terraingen.PowerLawSfd(0.05, 2.0))], 1.0, 100.0).pieces) == 1


def test_log_polynomial_sfd_evaluates_its_polynomial():
    assert terraingen.LogPolynomialSfd([-2.0, -2.0], 1.0, 100.0).cumulative(10.0) == pytest.approx(1e-4)


def test_log_polynomial_sfd_exposes_its_coefficients():
    assert terraingen.LogPolynomialSfd([-2.0, -2.0], 1.0, 100.0).coefficients == [-2.0, -2.0]


def test_minimum_sfd_takes_the_smaller_cumulative():
    surface = terraingen.ProductionSurface(
        terraingen.extended_neukum_production_function(), 3.5, terraingen.neukum_lunar_chronology
    )
    trask = terraingen.trask_equilibrium()
    capped = terraingen.MinimumSfd([surface, trask])
    assert capped.cumulative(5.0) == pytest.approx(min(surface.cumulative(5.0), trask.cumulative(5.0)))


def test_minimum_sfd_exposes_its_distributions():
    power = terraingen.PowerLawSfd(0.05, 2.0)
    assert len(terraingen.MinimumSfd([power, terraingen.trask_equilibrium()]).distributions) == 2


def test_production_surface_is_a_density_of_its_age():
    surface = terraingen.ProductionSurface(
        terraingen.extended_neukum_production_function(), 3.5, terraingen.neukum_lunar_chronology
    )
    assert surface.age_gyr == 3.5 and surface.kind == terraingen.SfdKind.DENSITY


def test_production_surface_scales_with_a_custom_chronology():
    production = terraingen.extended_neukum_production_function()
    surface = terraingen.ProductionSurface(production, 3.5, terraingen.neukum_lunar_chronology)
    doubled = terraingen.ProductionSurface(
        production, 3.5, lambda age: 2 * terraingen.neukum_lunar_chronology(age)
    )
    assert doubled.cumulative(100.0) == pytest.approx(2 * surface.cumulative(100.0), rel=1e-9)


def test_production_surface_keeps_its_production_function():
    surface = terraingen.ProductionSurface(
        terraingen.extended_neukum_production_function(), 3.0, terraingen.neukum_lunar_chronology
    )
    assert surface.production.kind == terraingen.SfdKind.PRODUCTION_RATE


def test_golombek_rapp_coverage_sets_the_exponent():
    boulders = terraingen.GolombekRappSfd(0.05)
    assert boulders.coverage == 0.05 and boulders.q == pytest.approx(1.79 + 0.152 / 0.05)
    assert boulders.fractional_area(0.0) == pytest.approx(0.05)


def test_python_distribution_evaluates_cumulative_at(inverse_square):
    assert inverse_square.cumulative(10.0) == pytest.approx(5e-4)


def test_python_distribution_derives_incremental_and_expected_count(inverse_square):
    assert inverse_square.incremental(10.0) == pytest.approx(1e-4, rel=1e-3)
    assert inverse_square.expected_count(1e4, 2.0, 50.0) == pytest.approx(1e4 * 0.05 * (2.0**-2 - 50.0**-2))


def test_python_distribution_reports_its_kind_and_range(inverse_square):
    assert inverse_square.valid_range == (1.0, 1000.0) and inverse_square.kind == terraingen.SfdKind.DENSITY


def test_python_distribution_without_cumulative_at_raises():
    class Incomplete(terraingen.SizeFrequencyDistribution):
        pass

    with pytest.raises(TypeError, match="Incomplete must define cumulative_at"):
        Incomplete().cumulative(10.0)


def test_python_profile_without_height_raises_type_error():
    class Flat(terraingen.CraterProfile):
        def reach_radii(self):
            return 1.0

    with pytest.raises(TypeError, match="Flat must define height"):
        Flat().height(1.0, 10.0, terraingen.CraterState())


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


def test_mars_production_surface_follows_the_mars_chronology_at_one_kilometre():
    surface = terraingen.ProductionSurface(
        terraingen.ivanov_mars_production_function(), 3.0, terraingen.ivanov_mars_chronology
    )
    assert surface.cumulative(1000.0) == pytest.approx(terraingen.ivanov_mars_chronology(3.0), rel=1e-9)
    assert surface.cumulative(100.0) > surface.cumulative(1000.0)


def test_production_surface_needs_a_chronology():
    with pytest.raises(TypeError):
        terraingen.ProductionSurface(terraingen.extended_neukum_production_function(), 3.5)


def test_lunar_crater_density_falls_with_diameter(lunar_craters):
    density, _ = lunar_craters(3.5)
    assert density.cumulative(10.0) > density.cumulative(100.0) > 0
    assert density.expected_count(1e6, 10.0, 100.0) > 0


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
