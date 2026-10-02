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


def test_basis_noise_matches_cpp_golden_values():
    assert np.float32(tg.perlin_noise2(0.25, 0.75, 1234)) == f32("0x1.93a3e6p-4")
    assert np.float32(tg.simplex_noise2(0.25, 0.75, 1234)) == f32("0x1.bac986p-4")
    assert np.float32(tg.value_noise2(0.25, 0.75, 1234)) == f32("-0x1.0ed85ap-1")
    f1, f2 = tg.worley2(0.25, 0.75, 1234)
    assert (np.float32(f1), np.float32(f2)) == (f32("0x1.0f7b3p-1"), f32("0x1.2e7808p-1"))


@pytest.mark.parametrize("noise", [tg.perlin_noise2, tg.simplex_noise2, tg.value_noise2])
def test_vectorized_noise_matches_scalar_and_keeps_shape(noise):
    rng = np.random.default_rng(0)
    x = rng.uniform(-50, 50, size=(6, 7)).astype(np.float32)
    y = rng.uniform(-50, 50, size=(6, 7)).astype(np.float32)
    values = noise(x, y, 7)
    assert values.shape == (6, 7)
    assert values.dtype == np.float32
    expected = [noise(float(a), float(b), 7) for a, b in zip(x.flat, y.flat)]
    np.testing.assert_array_equal(values.ravel(), np.float32(expected))
    assert np.all(np.abs(values) <= 1.0)


def test_vectorized_worley_returns_both_distances():
    x = np.linspace(-3, 3, 12, dtype=np.float32).reshape(3, 4)
    f1, f2 = tg.worley2(x, x.T.copy().reshape(3, 4), 5)
    assert f1.shape == f2.shape == (3, 4)
    assert np.all(f1 <= f2)
    scalar = tg.worley2(float(x[1, 2]), float(x.T.copy().reshape(3, 4)[1, 2]), 5)
    assert (np.float32(scalar[0]), np.float32(scalar[1])) == (f1[1, 2], f2[1, 2])


def test_vectorized_noise_converts_float64_input():
    x = np.linspace(0.1, 5.1, 11)
    np.testing.assert_array_equal(
        tg.simplex_noise2(x, x, 3), tg.simplex_noise2(x.astype(np.float32), x.astype(np.float32), 3)
    )


def test_vectorized_noise_rejects_mismatched_shapes():
    with pytest.raises(ValueError):
        tg.perlin_noise2(np.zeros(3, np.float32), np.zeros(4, np.float32), 0)
    with pytest.raises(ValueError):
        tg.worley2(np.zeros((2, 2), np.float32), np.zeros(4, np.float32), 0)


def test_basis_noise2_dispatches_by_enum():
    assert tg.basis_noise2(tg.NoiseBasis.SIMPLEX, 1.5, 2.5, 9) == tg.simplex_noise2(1.5, 2.5, 9)
    assert tg.basis_noise2(tg.NoiseBasis.VALUE, 1.5, 2.5, 9) == tg.value_noise2(1.5, 2.5, 9)
