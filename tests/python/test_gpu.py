"""gpu: compute backends and the default backend."""

import pytest

import terraingen


@pytest.fixture
def restore_backend():
    yield
    terraingen.set_default_backend(terraingen.Backend.AUTO)


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
