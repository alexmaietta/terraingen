import numpy as np
import pytest

import terraingen as tg


def f32(hex_value):
    return np.float32(float.fromhex(hex_value))


def test_hashes_match_cpp_golden_values():
    assert tg.pcg_hash(0) == 129708002
    assert tg.pcg_hash(0xDEADBEEF) == 1730779506
    assert tg.pcg3d_hash(1, 2, 3) == (4204755366, 1223881804, 1500469937)
    assert tg.hash_to_unit_float(0x80000000) == 0.5


def test_perlin_matches_cpp_golden_values():
    assert np.float32(tg.perlin_noise2(0.25, 0.75, 1234)) == f32("0x1.93a3e6p-4")
    x = np.array([0.25, -12.5], dtype=np.float32)
    y = np.array([0.75, 3.125], dtype=np.float32)
    np.testing.assert_array_equal(
        tg.perlin_noise2(x, y, 1234), [f32("0x1.93a3e6p-4"), f32("-0x1.459948p-1")]
    )


def test_fbm_matches_cpp_golden_values():
    params = tg.FbmParams(seed=42)
    assert np.float32(tg.fbm2(0.0, 0.0, params)) == f32("0x1.822c5cp+3")


def test_vectorized_noise_matches_scalar_and_keeps_shape():
    rng = np.random.default_rng(0)
    x = rng.uniform(-50, 50, size=(6, 7)).astype(np.float32)
    y = rng.uniform(-50, 50, size=(6, 7)).astype(np.float32)
    values = tg.perlin_noise2(x, y, 7)
    assert values.shape == (6, 7)
    assert values.dtype == np.float32
    expected = [tg.perlin_noise2(float(a), float(b), 7) for a, b in zip(x.flat, y.flat)]
    np.testing.assert_array_equal(values.ravel(), np.float32(expected))


def test_vectorized_noise_converts_float64_input():
    x = np.linspace(0.1, 5.1, 11)
    values = tg.perlin_noise2(x, x, 3)
    np.testing.assert_array_equal(values, tg.perlin_noise2(x.astype(np.float32), x.astype(np.float32), 3))


def test_vectorized_noise_rejects_mismatched_shapes():
    with pytest.raises(ValueError):
        tg.perlin_noise2(np.zeros(3, np.float32), np.zeros(4, np.float32), 0)
    with pytest.raises(ValueError):
        tg.perlin_noise2(np.zeros((2, 2), np.float32), np.zeros(4, np.float32), 0)


def test_fbm_params_defaults_and_fields():
    params = tg.FbmParams()
    assert (params.seed, params.octaves) == (0, 8)
    assert params.wavelength == 1000.0
    params.gain = tg.gain_from_hurst(1.0)
    assert params.gain == 0.5


def test_invalid_fbm_params_raise_value_error():
    with pytest.raises(ValueError):
        tg.validate_fbm_params(tg.FbmParams(octaves=0))
    with pytest.raises(ValueError):
        tg.fbm2(0.0, 0.0, tg.FbmParams(wavelength=0.0))
    with pytest.raises(ValueError):
        tg.fbm2(np.zeros(2, np.float32), np.zeros(2, np.float32), tg.FbmParams(gain=0.0))
    with pytest.raises(ValueError):
        tg.fill_fbm(tg.Raster(2, 2), tg.FbmParams(lacunarity=1.0))


def test_fill_fbm_matches_fbm_at_cell_centres():
    raster = tg.Raster(16, 8)
    raster.transform = tg.GeoTransform.north_up(-100.0, 50.0, 2.0)
    params = tg.FbmParams(seed=5, wavelength=64.0, amplitude=20.0)
    tg.fill_fbm(raster, params)
    cols, rows = np.meshgrid(np.arange(16), np.arange(8))
    x = (-100.0 + (cols + 0.5) * 2.0).astype(np.float32)
    y = (50.0 - (rows + 0.5) * 2.0).astype(np.float32)
    np.testing.assert_array_equal(raster.values, tg.fbm2(x, y, params))
    assert np.abs(raster.values).max() <= params.amplitude
