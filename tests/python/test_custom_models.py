import math

import numpy as np
import pytest

import terraingen


class InverseSquare(terraingen.SizeFrequencyDistribution):
    def __init__(self):
        super().__init__(terraingen.SfdKind.DENSITY, 1.0, 1000.0)

    def cumulative_at(self, diameter):
        return 0.05 * diameter**-2.0


class Linear(terraingen.CraterAgeModel):
    def lifetime_at(self, diameter):
        return 0.01 * diameter


class Oldest(Linear):
    def sample_age_at(self, random, diameter, lifetime_gyr):
        return lifetime_gyr


class Bowl(terraingen.CraterProfile):
    def height(self, distance, diameter, state):
        x = distance / (diameter / 2)
        return -state.current.depth_ratio * diameter * (1 - x * x) if x < 1 else 0.0

    def reach_radii(self):
        return 1.0


class Fixed(terraingen.DegradationModel):
    def state(self, crater):
        shape = terraingen.CraterMorphometry(0.15, 0.0)
        return terraingen.CraterState(shape, shape, 0.0)


class Bump(terraingen.Layer):
    def apply_at(self, height, x, y, context):
        return height + math.exp(-(x * x + y * y) / 2000.0)


class Tilt(terraingen.Layer):
    def apply(self, heights, context):
        heights.values[:] += np.linspace(0, 1, heights.width, dtype=np.float32)

    def max_feature_diameter(self):
        return 5.0


class Paraboloid(terraingen.Source):
    def grid(self):
        return terraingen.RasterGrid.centred(65, 65, 2.0)

    def height_at(self, x, y):
        return 0.001 * (x * x + y * y)


class Negate(terraingen.Filter):
    def apply(self, heights, spacing):
        heights.values[:] *= -1


class Stripes(terraingen.AlbedoSource):
    def bands(self):
        return 1

    def value_at(self, x, y, context):
        return 0.1 if int(x // 10) % 2 else 0.2


class Halve(terraingen.AlbedoLayer):
    def apply_at(self, value, bands, x, y, context):
        return [v * 0.5 for v in value]


def custom_craters(ages=None):
    models = terraingen.CraterModels(InverseSquare(), ages or Linear(), Fixed(), Bowl())
    generation = terraingen.CraterGenerationOptions(min_diameter=4.0, max_diameter=40.0)
    return terraingen.CraterLayer(models, terraingen.CraterLayerOptions(generation=generation))


def test_python_distribution_age_model_profile_and_degradation_drive_craters():
    sfd = InverseSquare()
    assert sfd.cumulative(10.0) == pytest.approx(5e-4)
    assert sfd.incremental(10.0) == pytest.approx(1e-4, rel=1e-3)
    assert sfd.expected_count(1e4, 2.0, 50.0) == pytest.approx(1e4 * 0.05 * (2.0**-2 - 50.0**-2))
    assert sfd.valid_range == (1.0, 1000.0) and sfd.kind == terraingen.SfdKind.DENSITY
    assert Linear().lifetime_gyr(10.0) == pytest.approx(0.1)
    assert 0.0 <= Linear().sample_age_gyr(terraingen.RandomStream(1, 2), 10.0) < 0.1
    assert Oldest().sample_age_gyr(terraingen.RandomStream(1, 2), 10.0) == pytest.approx(0.1)

    craters = custom_craters()
    table = craters.craters((-100.0, -100.0, 100.0, 100.0))
    assert len(table["diameter"]) > 10
    assert (table["diameter"] >= 4.0).all() and (table["age_gyr"] < 0.01 * table["diameter"]).all()
    np.testing.assert_allclose(table["depth_ratio"], 0.15, rtol=1e-6)
    old = custom_craters(Oldest()).craters((-100.0, -100.0, 100.0, 100.0))
    np.testing.assert_allclose(old["age_gyr"], 0.01 * old["diameter"])

    terrain = terraingen.Terrain(terraingen.FlatSource(65, 65, 2.0), [craters])
    heights = terraingen.evaluate(terrain, threads=4).heights.values
    assert heights.min() < -1.0 and heights.max() == 0.0
    np.testing.assert_array_equal(terraingen.evaluate(terrain, threads=1).heights.values, heights)


def test_python_sources_layers_and_filters():
    source = Paraboloid()
    assert source.width == 65 and source.reference_point() == (0.0, 0.0)
    plain = terraingen.evaluate(terraingen.Terrain(source)).heights.values
    assert plain[32, 32] == pytest.approx(0.0, abs=1e-6) and plain[0, 0] > 4.0

    bumped = terraingen.evaluate(terraingen.Terrain(source, [Bump()])).heights.values
    assert bumped[32, 32] - plain[32, 32] == pytest.approx(1.0, abs=1e-3)
    assert terraingen.evaluate_at(terraingen.Terrain(source, [Bump()]), 0.0, 0.0, 2.0) == pytest.approx(1.0, abs=1e-3)

    tilted = terraingen.evaluate(terraingen.Terrain(source, [Tilt()])).heights.values
    np.testing.assert_allclose(tilted - plain, np.broadcast_to(np.linspace(0, 1, 65), (65, 65)), atol=1e-5)
    assert Tilt().max_feature_diameter() == 5.0 and Bump().max_feature_diameter() == 0.0

    negated = terraingen.evaluate(terraingen.Terrain(source, [], [Negate()])).heights.values
    np.testing.assert_array_equal(negated, -plain)
    assert Negate().margin(10.0, 1.0) == 0.0

    with pytest.raises(RuntimeError):
        terraingen.evaluate(terraingen.Terrain(source, [terraingen.Layer()]))

    class Incomplete(terraingen.SizeFrequencyDistribution):
        pass

    with pytest.raises(RuntimeError):
        Incomplete().cumulative(10.0)


def test_python_albedo_source_and_layer():
    albedo = terraingen.Albedo(Stripes(), [Halve()])
    band = terraingen.evaluate_albedo(albedo, terraingen.RasterGrid.centred(8, 8, 5.0))[0].values
    assert set(np.unique(band)) == {np.float32(0.05), np.float32(0.1)}
    assert Halve().apply_at([0.2], 1, 0.0, 0.0, terraingen.AlbedoContext()) == pytest.approx([0.1])
    crater_albedo = terraingen.CraterAlbedo(custom_craters())
    assert crater_albedo.apply_at([0.1], 1, 500.0, 500.0, terraingen.AlbedoContext()) == pytest.approx([0.1])
