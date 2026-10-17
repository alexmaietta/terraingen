"""gpu: compute backends and the default backend."""

import numpy as np
import pytest

import terraingen


@pytest.fixture
def restore_backend():
    yield
    terraingen.set_default_backend(terraingen.Backend.CPU)


def test_backends_list_cpu_then_vulkan_and_cpu_is_available():
    statuses = terraingen.backends()
    assert [s.backend for s in statuses] == [terraingen.Backend.CPU, terraingen.Backend.VULKAN]
    assert statuses[0].available


def test_backend_named_finds_known_backends_only():
    assert (
        terraingen.backend_named("Vulkan") == terraingen.Backend.VULKAN
        and terraingen.backend_named("x") is None
    )


@pytest.mark.usefixtures("restore_backend")
def test_default_backend_round_trips():
    terraingen.set_default_backend(terraingen.Backend.CPU)
    assert terraingen.default_backend() == terraingen.Backend.CPU


def test_resolving_cpu_gives_cpu():
    assert terraingen.resolve_backend(terraingen.Backend.CPU) == terraingen.Backend.CPU


@pytest.mark.usefixtures("restore_backend")
def test_evaluation_without_a_backend_follows_the_default(small_config):
    config = terraingen.Config.from_dict(small_config)
    terrain = terraingen.build_terrain(config)
    grid = config.output_grids()[0]
    terraingen.set_default_backend(terraingen.Backend.AUTO)
    by_default = terraingen.evaluate(terrain, grid=grid).values
    chosen = terraingen.evaluate(terrain, grid=grid, backend=terraingen.Backend.AUTO).values
    np.testing.assert_array_equal(by_default, chosen)
    terraingen.set_default_backend(terraingen.Backend.CPU)
    cpu = terraingen.evaluate(terrain, grid=grid, backend=terraingen.Backend.CPU).values
    np.testing.assert_array_equal(terraingen.evaluate(terrain, grid=grid).values, cpu)
