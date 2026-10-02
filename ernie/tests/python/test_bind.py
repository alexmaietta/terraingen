import numpy as np
import pytest

import ernie
from ernie import tokens
from test_cuda import instancer_scene


@pytest.fixture
def cuda_renderer(renderer):
    if not renderer.cuda_available:
        pytest.skip("No CUDA interop on this machine")
    return renderer


def green_columns(image):
    pixels = np.asarray(image).astype(np.int32)
    green = (pixels[..., 1] > 200) & (2 * (pixels[..., 0] + pixels[..., 2]) < pixels[..., 1])
    return np.nonzero(green.any(axis=0))[0]


def test_bound_positions_are_written_in_place(cuda_renderer):
    scene = instancer_scene(np.array([[-10.0, 0.0, 0.0]], np.float32))
    binding = cuda_renderer.bind(scene, "/World/Rocks", tokens.POSITIONS)
    assert binding.path == ernie.Path("/World/Rocks")
    assert binding.attribute == tokens.POSITIONS

    array = binding.map(stream=1)
    assert array.__dlpack_device__() == (2, 0)
    assert binding.mapped
    np.testing.assert_array_equal(np.array(cuda_renderer.cuda_download(array)), [[-10, 0, 0]])
    moved = cuda_renderer.cuda_upload(np.array([[10.0, 0.0, 0.0]], np.float32))
    cuda_renderer.cuda_copy(moved, array, stream=1)
    binding.unmap(stream=1)
    assert not binding.mapped

    columns = green_columns(cuda_renderer.render(scene))
    assert columns.size and columns.min() > 24, "the instance moved right"
    assert cuda_renderer.take_validation_messages() == []


def test_bind_rejects_other_attributes(cuda_renderer):
    scene = instancer_scene(np.zeros((1, 3), np.float32))
    with pytest.raises(ValueError, match="can be bound"):
        cuda_renderer.bind(scene, "/World/Rocks", tokens.POINTS)


def test_steps_alternate_between_bound_arrays(cuda_renderer):
    scene = instancer_scene(np.array([[-10.0, 0.0, 0.0]], np.float32))
    binding = cuda_renderer.bind(scene, "/World/Rocks", tokens.POSITIONS)
    for x in (10.0, -10.0, 10.0, -10.0):
        array = binding.map(stream=1)
        value = cuda_renderer.cuda_upload(np.array([[x, 0.0, 0.0]], np.float32))
        cuda_renderer.cuda_copy(value, array, stream=1)
        binding.unmap(stream=1)
        columns = green_columns(cuda_renderer.render(scene))
        assert columns.size
        assert (columns.min() > 24) == (x > 0), f"step drew the array written for x={x}"
    assert cuda_renderer.take_validation_messages() == []
