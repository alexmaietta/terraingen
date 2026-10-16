"""noise: hashes, basis noise in 2D and 3D, fractals, noise layers and their validation."""

import numpy as np
import pytest

import terraingen

X3 = np.array([0.3, 1.7e6, -2.5e5], dtype=np.float64)
Y3 = np.array([0.2, -3.1e5, 7.0], dtype=np.float64)
Z3 = np.array([0.1, 2.2e5, 1.0], dtype=np.float64)


def f32(hex_value):
    return np.float32(float.fromhex(hex_value))


def fractal3_params():
    return terraingen.FractalParams(seed=2, wavelength=1000.0, amplitude=10.0, octaves=4)


def test_hashes_match_cpp_golden_values():
    assert terraingen.pcg_hash(0) == 129708002
    assert terraingen.pcg_hash(0xDEADBEEF) == 1730779506
    assert terraingen.pcg3d_hash(1, 2, 3) == (4204755366, 1223881804, 1500469937)
    assert terraingen.hash_to_unit_float(0x80000000) == 0.5


def test_basis_noise_matches_cpp_golden_values():
    assert np.float32(terraingen.perlin_noise2(0.25, 0.75, 1234)) == f32("0x1.93a3e6p-4")
    assert np.float32(terraingen.simplex_noise2(0.25, 0.75, 1234)) == f32("0x1.bac986p-4")
    assert np.float32(terraingen.value_noise2(0.25, 0.75, 1234)) == f32("-0x1.0ed85ap-1")
    f1, f2 = terraingen.worley2(0.25, 0.75, 1234)
    assert (np.float32(f1), np.float32(f2)) == (f32("0x1.0f7b3p-1"), f32("0x1.2e7808p-1"))


@pytest.mark.parametrize(
    "noise", [terraingen.perlin_noise2, terraingen.simplex_noise2, terraingen.value_noise2]
)
def test_vectorized_noise_matches_scalar_and_keeps_shape(noise):
    rng = np.random.default_rng(0)
    x = rng.uniform(-50, 50, size=(6, 7)).astype(np.float32)
    y = rng.uniform(-50, 50, size=(6, 7)).astype(np.float32)
    values = noise(x, y, 7)
    assert values.shape == (6, 7)
    assert values.dtype == np.float32
    expected = [noise(float(a), float(b), 7) for a, b in zip(x.flat, y.flat, strict=True)]
    np.testing.assert_array_equal(values.ravel(), np.float32(expected))
    assert np.all(np.abs(values) <= 1.0)


def test_vectorized_worley_returns_both_distances():
    x = np.linspace(-3, 3, 12, dtype=np.float32).reshape(3, 4)
    f1, f2 = terraingen.worley2(x, x.T.copy().reshape(3, 4), 5)
    assert f1.shape == f2.shape == (3, 4)
    assert np.all(f1 <= f2)
    scalar = terraingen.worley2(float(x[1, 2]), float(x.T.copy().reshape(3, 4)[1, 2]), 5)
    assert (np.float32(scalar[0]), np.float32(scalar[1])) == (f1[1, 2], f2[1, 2])


def test_vectorized_noise_converts_float64_input():
    x = np.linspace(0.1, 5.1, 11)
    np.testing.assert_array_equal(
        terraingen.simplex_noise2(x, x, 3),
        terraingen.simplex_noise2(x.astype(np.float32), x.astype(np.float32), 3),
    )


def test_vectorized_noise_rejects_mismatched_shapes():
    with pytest.raises(ValueError):
        terraingen.perlin_noise2(np.zeros(3, np.float32), np.zeros(4, np.float32), 0)
    with pytest.raises(ValueError):
        terraingen.worley2(np.zeros((2, 2), np.float32), np.zeros(4, np.float32), 0)


def test_basis_noise2_dispatches_by_enum():
    assert terraingen.basis_noise2(terraingen.NoiseBasis.SIMPLEX, 1.5, 2.5, 9) == terraingen.simplex_noise2(
        1.5, 2.5, 9
    )
    assert terraingen.basis_noise2(terraingen.NoiseBasis.VALUE, 1.5, 2.5, 9) == terraingen.value_noise2(
        1.5, 2.5, 9
    )


def test_perlin_noise3_matches_scalar_and_stays_in_range():
    values = terraingen.perlin_noise3(X3, Y3, Z3, 4)
    assert values.shape == (3,) and np.all(np.abs(values) <= 1.0)
    assert values[0] == pytest.approx(terraingen.perlin_noise3(0.3, 0.2, 0.1, 4))


def test_perlin_noise3_is_zero_on_lattice_points():
    assert terraingen.perlin_noise3(1.0, 2.0, 3.0, 4) == 0.0
    assert terraingen.basis_noise3(terraingen.NoiseBasis.PERLIN, 1, 2, 3, 0.0, 0.0, 0.0, 4) == 0.0


def test_value_noise3_stays_in_range():
    assert -1.0 <= terraingen.value_noise3(0.5, 0.25, 0.75, 4) <= 1.0


def test_has_basis_noise3_lists_the_3d_bases():
    assert terraingen.has_basis_noise3(terraingen.NoiseBasis.VALUE)
    assert not terraingen.has_basis_noise3(terraingen.NoiseBasis.SIMPLEX)


def test_noise3_rejects_mismatched_shapes():
    with pytest.raises(ValueError):
        terraingen.perlin_noise3(X3, Y3[:2], Z3, 1)


def test_fractal3_matches_scalar_and_stays_within_amplitude():
    heights = terraingen.fractal3(X3, Y3, Z3, fractal3_params())
    assert np.all(np.abs(heights) <= 10.0)
    assert heights[1] == pytest.approx(terraingen.fractal3(1.7e6, -3.1e5, 2.2e5, fractal3_params()))


def test_noise_layers3_of_a_single_layer_match_fractal3():
    params = fractal3_params()
    stack = terraingen.SyntheticTerrainParams([terraingen.NoiseLayer(params)])
    np.testing.assert_allclose(
        terraingen.evaluate_noise_layers3(X3, Y3, Z3, stack), terraingen.fractal3(X3, Y3, Z3, params)
    )


def test_domain_warp_stays_within_the_warp_amplitude():
    layer = terraingen.NoiseLayer(fractal3_params(), warp_amplitude=20.0, warp_wavelength=300.0)
    dx, dy = terraingen.domain_warp2(10.0, 20.0, layer)
    assert abs(dx) <= 20.0 and abs(dy) <= 20.0


def test_evaluate_noise_layer_stays_in_range():
    layer = terraingen.NoiseLayer(fractal3_params(), warp_amplitude=20.0, warp_wavelength=300.0)
    assert -1.0 <= terraingen.evaluate_noise_layer(10.0, 20.0, layer) <= 1.0


def test_fractal_params_defaults():
    params = terraingen.FractalParams()
    assert (params.seed, params.octaves, params.wavelength) == (0, 8, 1000.0)
    assert params.basis == terraingen.NoiseBasis.PERLIN
    assert params.type == terraingen.FractalType.FBM


def test_fractal_params_keywords_and_comparison():
    ridged = terraingen.FractalParams(
        seed=3, basis=terraingen.NoiseBasis.SIMPLEX, type=terraingen.FractalType.RIDGED, offset=0.8
    )
    assert (ridged.seed, ridged.basis, ridged.type) == (
        3,
        terraingen.NoiseBasis.SIMPLEX,
        terraingen.FractalType.RIDGED,
    )
    assert ridged.offset == pytest.approx(0.8)
    assert ridged != terraingen.FractalParams()


def test_gain_from_hurst_one_halves_each_octave():
    params = terraingen.FractalParams()
    params.gain = terraingen.gain_from_hurst(1.0)
    assert params.gain == 0.5


def test_fractals_match_cpp_golden_values():
    x, y = float.fromhex("0x1.004p+9"), float.fromhex("-0x1.001p+10")
    expected = {
        terraingen.FractalType.FBM: "-0x1.a18ed6p+0",
        terraingen.FractalType.RIDGED: "0x1.8fd9ap+5",
        terraingen.FractalType.BILLOW: "-0x1.26132cp+6",
        terraingen.FractalType.HYBRID_MULTIFRACTAL: "0x1.58502ap+5",
    }
    for fractal_type, value in expected.items():
        params = terraingen.FractalParams(seed=42, type=fractal_type)
        assert np.float32(terraingen.fractal2(x, y, params)) == f32(value)


def test_every_basis_and_fractal_stays_within_amplitude():
    x, y = np.meshgrid(np.linspace(-300, 300, 40), np.linspace(-200, 250, 40))
    x, y = x.astype(np.float32), y.astype(np.float32)
    for basis in terraingen.NoiseBasis:
        for fractal_type in terraingen.FractalType:
            params = terraingen.FractalParams(
                amplitude=10.0, wavelength=60.0, octaves=5, basis=basis, type=fractal_type
            )
            heights = terraingen.fractal2(x, y, params)
            assert np.abs(heights).max() <= 10.0 * (1 + 1e-5), (basis, fractal_type)


def test_noise_layers_match_cpp_golden_value():
    base = terraingen.NoiseLayer(terraingen.FractalParams(seed=1))
    detail = terraingen.NoiseLayer(
        terraingen.FractalParams(seed=2, basis=terraingen.NoiseBasis.WORLEY_F2_MINUS_F1, amplitude=10.0),
        warp_amplitude=50.0,
    )
    params = terraingen.SyntheticTerrainParams([base, detail])
    x = np.float32([float.fromhex("0x1.004p+9")])
    y = np.float32([float.fromhex("-0x1.001p+10")])
    assert terraingen.evaluate_noise_layers(x, y, params)[0] == f32("-0x1.38f3cp+4")


def test_noise_layer_fields_are_writable():
    layer = terraingen.NoiseLayer()
    assert layer.enabled and layer.blend == terraingen.LayerBlend.ADD
    layer.blend = terraingen.LayerBlend.MASK
    layer.warp_wavelength = 250.0
    assert layer != terraingen.NoiseLayer()


def test_synthetic_terrain_params_hold_up_to_eight_layers():
    params = terraingen.SyntheticTerrainParams()
    assert len(params.layers) == 1
    layer = terraingen.NoiseLayer()
    layer.blend = terraingen.LayerBlend.MASK
    params.layers = [layer, terraingen.NoiseLayer()]
    assert params.layers[0].blend == terraingen.LayerBlend.MASK
    assert terraingen.MAX_NOISE_LAYERS == 8


def test_validate_fractal_params_rejects_zero_octaves():
    with pytest.raises(ValueError):
        terraingen.validate_fractal_params(terraingen.FractalParams(octaves=0))


def test_fractal2_rejects_a_zero_wavelength():
    with pytest.raises(ValueError):
        terraingen.fractal2(0.0, 0.0, terraingen.FractalParams(wavelength=0.0))


def test_validate_noise_layer_rejects_a_negative_warp():
    with pytest.raises(ValueError):
        terraingen.validate_noise_layer(terraingen.NoiseLayer(warp_amplitude=-1.0))


def test_validate_synthetic_terrain_needs_a_layer():
    with pytest.raises(ValueError):
        terraingen.validate_synthetic_terrain(terraingen.SyntheticTerrainParams(layers=[]))
