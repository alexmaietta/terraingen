from pathlib import Path

import pytest

import terraingen

REPO_ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="session")
def repo_root():
    return REPO_ROOT


def build_lunar_models(age=3.5, degradation=None):
    """Crater models of a lunar surface `age` Gyr old, with freshness degradation by default."""
    surface = terraingen.lunar_surface(age)
    return terraingen.CraterModels(
        surface.density,
        surface.ages,
        degradation or terraingen.FreshnessDegradation(surface.ages),
        terraingen.FassettThomsonProfile(),
    )


@pytest.fixture(scope="session")
def lunar_models():
    """The `build_lunar_models` factory, for tests that build lunar crater models."""
    return build_lunar_models
