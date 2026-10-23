"""gpu: compute backends and the default backend."""

import numpy as np
import pytest

import terraingen


@pytest.fixture
def restore_backend():
    yield
    terraingen.set_default_backend(terraingen.Backend.CPU)


def test_backends_list_cpu_then_each_gpu_backend_and_cpu_is_available():
    statuses = terraingen.backends()
    assert [s.backend for s in statuses] == [
        terraingen.Backend.CPU,
        terraingen.Backend.VULKAN,
        terraingen.Backend.CUDA,
        terraingen.Backend.METAL,
    ]
    assert statuses[0].available


def test_backend_named_finds_known_backends_only():
    assert (
        terraingen.backend_named("Vulkan") == terraingen.Backend.VULKAN
        and terraingen.backend_named("GPU") == terraingen.Backend.GPU
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
    config = terraingen.TerrainGenConfig.from_dict(small_config)
    terrain = terraingen.build_generator(config)
    grid = config.output_grids()[0]
    terraingen.set_default_backend(terraingen.Backend.AUTO)
    by_default = terraingen.evaluate(terrain, grid=grid).values
    chosen = terraingen.evaluate(terrain, grid=grid, backend=terraingen.Backend.AUTO).values
    np.testing.assert_array_equal(by_default, chosen)
    terraingen.set_default_backend(terraingen.Backend.CPU)
    cpu = terraingen.evaluate(terrain, grid=grid, backend=terraingen.Backend.CPU).values
    np.testing.assert_array_equal(terraingen.evaluate(terrain, grid=grid).values, cpu)


def test_resolving_gpu_gives_the_best_gpu_backend_or_says_why_not():
    available = {s.backend for s in terraingen.backends() if s.available}
    order = [terraingen.Backend.METAL, terraingen.Backend.CUDA, terraingen.Backend.VULKAN]
    best = next((b for b in order if b in available), None)
    if best is not None:
        assert terraingen.resolve_backend(terraingen.Backend.GPU) == best
    else:
        with pytest.raises(RuntimeError, match="No GPU backend is available"):
            terraingen.resolve_backend(terraingen.Backend.GPU)
