import gc
import sys

import numpy as np
import pytest

import ernie
from ernie import tokens
from test_render import quad


@pytest.fixture
def cuda_renderer(renderer):
    if not renderer.cuda_available:
        pytest.skip("No CUDA interop on this machine")
    return renderer


def instancer_scene(positions):
    scene = ernie.Scene()
    scene.define("/World/Camera", tokens.CAMERA).set(
        tokens.TRANSFORM, ernie.look_at_transform((0, 0, 60), (0, 0, 0), up=(0, 1, 0))
    ).set(tokens.FOCAL_LENGTH, 18.0)
    scene.define("/Render/Settings", tokens.RENDER_SETTINGS).set(tokens.RESOLUTION, (48, 48))
    scene.define("/World/Rocks", tokens.POINT_INSTANCER).write(tokens.POSITIONS, positions).set(
        tokens.PROTOTYPES, [ernie.Path("/World/Rocks/Rock")]
    ).set(tokens.EXTENT, [-15.0, -15.0, -1.0, 15.0, 15.0, 1.0])
    quad(scene, "/World/Rocks/Rock", 1.5).set(tokens.DISPLAY_COLOR, (0.0, 1.0, 0.0))
    return scene


def test_cuda_instances_render_like_host_ones(cuda_renderer):
    positions = np.array([[-10.0, 0.0, 0.0], [10.0, 0.0, 0.0], [0.0, 10.0, 0.0]], np.float32)
    expected = np.array(cuda_renderer.render(instancer_scene(positions)))
    on_gpu = cuda_renderer.cuda_upload(positions)
    image = cuda_renderer.render(instancer_scene(on_gpu))
    np.testing.assert_array_equal(image, expected)


def test_renders_into_cuda_memory(cuda_renderer):
    positions = np.array([[-10.0, 0.0, 0.0], [10.0, 0.0, 0.0]], np.float32)
    scene = instancer_scene(positions)
    expected = np.array(cuda_renderer.render(scene))
    result = cuda_renderer.render(scene, device="cuda")
    assert isinstance(result, ernie.CudaArray)
    assert result.shape == (48, 48, 4)
    assert result.__dlpack_device__() == (2, 0)
    assert type(result.__dlpack__(stream=-1)).__name__ == "PyCapsule"
    assert type(result.__dlpack__(stream=None)).__name__ == "PyCapsule"
    np.testing.assert_array_equal(np.array(cuda_renderer.cuda_download(result)), expected)
    assert cuda_renderer.take_validation_messages() == []


def test_cuda_results_keep_the_renderer_alive(cuda_renderer):
    scene = instancer_scene(np.zeros((1, 3), np.float32))
    gc.collect()
    before = sys.getrefcount(cuda_renderer)
    result = cuda_renderer.render(scene, device="cuda:0")
    assert sys.getrefcount(cuda_renderer) > before
    del result
    gc.collect()
    assert sys.getrefcount(cuda_renderer) == before


def test_rejects_bad_devices(renderer):
    scene = instancer_scene(np.zeros((1, 3), np.float32))
    with pytest.raises(ValueError, match="device must be"):
        renderer.render(scene, device="tpu")


def test_back_to_back_cuda_renders_stay_correct(cuda_renderer):
    scene = instancer_scene(np.array([[-10.0, 0.0, 0.0]], np.float32))
    expected = np.array(cuda_renderer.render(scene))
    results = [cuda_renderer.render(scene, device="cuda") for _ in range(4)]
    for result in results:
        np.testing.assert_array_equal(np.array(cuda_renderer.cuda_download(result)), expected)
