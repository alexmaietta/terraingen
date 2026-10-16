"""filters: crater rim smoothing and filters written in Python."""

import numpy as np
import pytest

import terraingen


class Negate(terraingen.Filter):
    """Flips the heights upside down."""

    def apply(self, heights, spacing):
        heights.values[:] *= -1


def undegraded_craters(crater_layer, lunar_craters):
    density, ages = lunar_craters(3.5)
    models = terraingen.CraterModels(
        density,
        ages,
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
        terraingen.FassettThomsonProfile(),
    )
    return crater_layer(models, 10.0, 60.0)


def smoothed_heights(craters, smoothing):
    source = terraingen.FlatSource(129, 129, 1.0)
    filters = [terraingen.RimSmoothingFilter(smoothing)]
    return terraingen.evaluate(terraingen.Terrain(source, [craters], filters)).heights.values


def test_rim_smoothing_lowers_the_highest_rim(crater_layer, lunar_craters):
    craters = undegraded_craters(crater_layer, lunar_craters)
    plain = terraingen.evaluate(
        terraingen.Terrain(terraingen.FlatSource(129, 129, 1.0), [craters])
    ).heights.values
    soft = terraingen.RimSmoothing(
        sigma_radii=0.15, inner_radii=0.7, outer_radii=1.3, shape=terraingen.BlendShape.SMOOTHSTEP
    )
    assert smoothed_heights(craters, soft).max() < plain.max()


def test_rim_smoothing_at_zero_strength_changes_nothing(crater_layer, lunar_craters):
    craters = undegraded_craters(crater_layer, lunar_craters)
    plain = terraingen.evaluate(
        terraingen.Terrain(terraingen.FlatSource(129, 129, 1.0), [craters])
    ).heights.values
    np.testing.assert_array_equal(smoothed_heights(craters, terraingen.RimSmoothing(strength=0.0)), plain)


def test_rim_smoothing_keeps_its_options():
    soft = terraingen.RimSmoothing(
        sigma_radii=0.15, inner_radii=0.7, outer_radii=1.3, shape=terraingen.BlendShape.SMOOTHSTEP
    )
    assert soft.shape == terraingen.BlendShape.SMOOTHSTEP and soft.outer_radii == 1.3


def test_validate_rim_smoothing_rejects_strength_above_one():
    with pytest.raises(ValueError):
        terraingen.validate_rim_smoothing(terraingen.RimSmoothing(strength=2.0))


def lunar_crater_terrains(lunar_models, crater_layer):
    craters = crater_layer(lunar_models(), 3.0, 20.0)
    source = terraingen.FlatSource(301, 301, 0.1)
    smoothing = terraingen.RimSmoothingFilter(terraingen.RimSmoothing(sigma_radii=0.1))
    return terraingen.Terrain(source, [craters], [smoothing]), terraingen.Terrain(source, [craters])


def test_rim_smoothing_filter_changes_the_heights(lunar_models, crater_layer):
    smoothed, plain = lunar_crater_terrains(lunar_models, crater_layer)
    assert not np.array_equal(
        terraingen.evaluate(smoothed).heights.values, terraingen.evaluate(plain).heights.values
    )


def test_point_queries_reject_terrains_with_filters(lunar_models, crater_layer):
    smoothed, _ = lunar_crater_terrains(lunar_models, crater_layer)
    assert len(smoothed.filters) == 1
    with pytest.raises(ValueError):
        terraingen.evaluate_at(smoothed, 0.0, 0.0, 0.1)


def test_removing_filters_restores_point_queries(lunar_models, crater_layer):
    smoothed, plain = lunar_crater_terrains(lunar_models, crater_layer)
    smoothed.filters = []
    assert terraingen.evaluate_at(smoothed, 0.0, 0.0, 0.1) == terraingen.evaluate_at(plain, 0.0, 0.0, 0.1)


def test_python_filter_edits_the_heights(paraboloid):
    plain = terraingen.evaluate(terraingen.Terrain(paraboloid)).heights.values
    negated = terraingen.evaluate(terraingen.Terrain(paraboloid, [], [Negate()])).heights.values
    np.testing.assert_array_equal(negated, -plain)


def test_python_filter_needs_no_margin_by_default():
    assert Negate().margin(10.0, 1.0) == 0.0


def test_gaussian_blur_filter_smooths_a_terrain_on_a_plane(paraboloid):
    plain = terraingen.evaluate(terraingen.Terrain(paraboloid)).heights.values
    blur = terraingen.GaussianBlurFilter(3.0)
    blurred = terraingen.evaluate(terraingen.Terrain(paraboloid, [], [blur])).heights.values
    assert blur.sigma == 3.0
    assert blur.margin(10.0, 1.0) > 9.0
    assert plain[32, 32] == 0.0
    assert blurred[32, 32] == pytest.approx(0.001 * 2 * 3.0**2, abs=0.002), (
        "a bowl blurred rises by 2 a sigma^2"
    )
    with pytest.raises(ValueError):
        terraingen.GaussianBlurFilter(1.0, terraingen.Surface.sphere(1000.0))
