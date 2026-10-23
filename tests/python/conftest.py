"""Shared fixtures and builders for the Python tests."""

import copy
from pathlib import Path

import pytest

import terraingen

REPO_ROOT = Path(__file__).resolve().parents[2]

LUNAR_DENSITY = {
    "type": "minimum",
    "distributions": [
        {
            "type": "production_surface",
            "production": "extended_neukum",
            "chronology": "neukum",
            "age_gyr": 3.5,
        },
        {"type": "trask"},
    ],
}

LUNAR_AGES = {
    "type": "steady_state",
    "age_gyr": 3.5,
    "production": "extended_neukum",
    "equilibrium": "trask",
    "chronology": "neukum",
}

SMALL_AREA = {"pixel_size_m": 0.5, "extent_m": {"center": [0, 0], "size": [40.5, 32.5]}}
"""81 x 65 cells of 0.5 m centered on the origin."""

SMALL_CONFIG = {
    "schema_version": 1,
    "layers": [
        {"type": "noise", "stack": [{"wavelength": 20, "amplitude": 0.5, "octaves": 3}]},
        {
            "type": "craters",
            "min_diameter_m": 3,
            "max_diameter_m": 15,
            "density": copy.deepcopy(LUNAR_DENSITY),
            "ages": copy.deepcopy(LUNAR_AGES),
        },
        {
            "type": "boulders",
            "coverage": 0.1,
            "sizes": {"type": "golombek_rapp"},
            "min_diameter_m": 0.5,
            "max_diameter_m": 2,
        },
    ],
    "outputs": [
        {"type": "heightmap", "path": "heights.tif", **SMALL_AREA},
        {"type": "crater_list", "path": "craters.csv", **SMALL_AREA},
    ],
}


@pytest.fixture(scope="session")
def repo_root():
    return REPO_ROOT


@pytest.fixture
def moon():
    """The Moon as a sphere of its mean radius."""
    return terraingen.Surface.sphere(1737400.0)


@pytest.fixture
def small_config():
    """A fresh copy of a small config dict: noise, craters and boulders, with two outputs."""
    return copy.deepcopy(SMALL_CONFIG)


def build_lunar_craters(age=3.5):
    """The crater density and ages of a lunar surface `age` Gyr old.

    The density is the least of the extended Neukum production surface and Trask equilibrium; the
    ages are steady state from the same two distributions.
    """
    production = terraingen.extended_neukum_production_function()
    equilibrium = terraingen.trask_equilibrium()
    surface = terraingen.ProductionSurface(production, age, terraingen.neukum_lunar_chronology)
    density = terraingen.MinimumSfd([surface, equilibrium])
    ages = terraingen.SteadyStateAges(production, equilibrium, age, terraingen.neukum_lunar_chronology)
    return density, ages


@pytest.fixture(scope="session")
def lunar_craters():
    """The `build_lunar_craters` factory."""
    return build_lunar_craters


def build_lunar_models(age=3.5, degradation=None):
    """Crater models of a lunar surface `age` Gyr old, with freshness degradation by default."""
    density, ages = build_lunar_craters(age)
    return terraingen.CraterModels(
        density,
        ages,
        degradation or terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
        terraingen.FassettThomsonProfile(),
    )


@pytest.fixture(scope="session")
def lunar_models():
    """The `build_lunar_models` factory, for tests that build lunar crater models."""
    return build_lunar_models


def build_crater_layer(models, min_diameter, max_diameter, **layer_options):
    """A crater layer generating craters of `models` between the two diameters (m)."""
    generation = terraingen.CraterGenerationOptions(d_min=min_diameter, d_max=max_diameter)
    return terraingen.CraterLayer(
        models, terraingen.CraterLayerOptions(generation=generation, **layer_options)
    )


@pytest.fixture(scope="session")
def crater_layer():
    """The `build_crater_layer` factory."""
    return build_crater_layer


def build_noise_relief(seed, wavelength, amplitude, surface=None, **params):
    """Single-layer fractal noise relief; extra keywords go to `FractalParams`."""
    layer = terraingen.NoiseLayer(
        terraingen.FractalParams(seed=seed, wavelength=wavelength, amplitude=amplitude, **params)
    )
    if surface is None:
        return terraingen.NoiseRelief([layer])
    return terraingen.NoiseRelief([layer], surface=surface)


@pytest.fixture(scope="session")
def noise_relief():
    """The `build_noise_relief` factory."""
    return build_noise_relief


class InverseSquare(terraingen.SizeFrequencyDistribution):
    """A density distribution N(D) = 0.05 D^-2 written in Python."""

    def __init__(self):
        super().__init__(terraingen.SfdKind.DENSITY, 1.0, 1000.0)

    def cumulative_at(self, diameter):
        return 0.05 * diameter**-2.0


class LinearAges(terraingen.CraterAgeModel):
    """Crater lifetimes of 0.01 Gyr per meter of diameter."""

    def lifetime_at(self, diameter):
        return 0.01 * diameter


class OldestAges(LinearAges):
    """Linear lifetimes, every crater as old as its lifetime."""

    def sample_age_at(self, random, diameter, lifetime_gyr):
        return lifetime_gyr


class Bowl(terraingen.CraterProfile):
    """A parabolic bowl with no rim."""

    def height(self, distance, diameter, state):
        x = distance / (diameter / 2)
        return -state.current.depth_ratio * diameter * (1 - x * x) if x < 1 else 0.0

    def reach_radii(self):
        return 1.0


class FixedDepth(terraingen.DegradationModel):
    """Every crater at depth ratio 0.15 with no rim."""

    def state(self, crater):
        shape = terraingen.CraterMorphometry(0.15, 0.0)
        return terraingen.CraterState(shape, shape, 0.0)


class Paraboloid(terraingen.Source):
    """A 65 x 65 bowl at 2 m spacing, 0.001 r^2 high."""

    def grid(self):
        return terraingen.RasterGrid.centered(65, 65, 2.0)

    def height_at(self, x, y, spacing):
        return 0.001 * (x * x + y * y)


@pytest.fixture
def inverse_square():
    return InverseSquare()


@pytest.fixture
def linear_ages():
    return LinearAges()


@pytest.fixture
def oldest_ages():
    return OldestAges()


@pytest.fixture
def paraboloid():
    return Paraboloid()


def build_custom_craters(ages=None):
    """A crater layer whose distribution, ages, degradation and profile are all written in Python."""
    models = terraingen.CraterModels(InverseSquare(), ages or LinearAges(), FixedDepth(), Bowl())
    return build_crater_layer(models, 4.0, 40.0)


@pytest.fixture(scope="session")
def custom_craters():
    """The `build_custom_craters` factory."""
    return build_custom_craters
