import math

import numpy as np
import pytest

import terraingen as tg


def f32(hex_value):
    return np.float32(float.fromhex(hex_value))


def test_fractal_params_defaults_and_keywords():
    params = tg.FractalParams()
    assert (params.seed, params.octaves, params.wavelength) == (0, 8, 1000.0)
    assert params.basis == tg.NoiseBasis.PERLIN
    assert params.type == tg.FractalType.FBM
    ridged = tg.FractalParams(seed=3, basis=tg.NoiseBasis.SIMPLEX, type=tg.FractalType.RIDGED, offset=0.8)
    assert (ridged.seed, ridged.basis, ridged.type) == (3, tg.NoiseBasis.SIMPLEX, tg.FractalType.RIDGED)
    assert ridged.offset == pytest.approx(0.8)
    assert ridged != params
    params.gain = tg.gain_from_hurst(1.0)
    assert params.gain == 0.5


def test_fractals_match_cpp_golden_values():
    x, y = float.fromhex("0x1.004p+9"), float.fromhex("-0x1.001p+10")
    expected = {
        tg.FractalType.FBM: "-0x1.278372p+3",
        tg.FractalType.RIDGED: "0x1.c929e4p+3",
        tg.FractalType.BILLOW: "-0x1.bc10ep+5",
        tg.FractalType.HYBRID_MULTIFRACTAL: "0x1.1ace18p+5",
    }
    for fractal_type, value in expected.items():
        assert np.float32(tg.fractal2(x, y, tg.FractalParams(seed=42, type=fractal_type))) == f32(value)


def test_every_basis_and_fractal_stays_within_amplitude():
    x, y = np.meshgrid(np.linspace(-300, 300, 40), np.linspace(-200, 250, 40))
    x, y = x.astype(np.float32), y.astype(np.float32)
    for basis in tg.NoiseBasis:
        for fractal_type in tg.FractalType:
            params = tg.FractalParams(amplitude=10.0, wavelength=60.0, octaves=5, basis=basis, type=fractal_type)
            heights = tg.fractal2(x, y, params)
            assert np.abs(heights).max() <= 10.0 * (1 + 1e-5), (basis, fractal_type)


def test_invalid_parameters_raise_value_error():
    with pytest.raises(ValueError):
        tg.validate_fractal_params(tg.FractalParams(octaves=0))
    with pytest.raises(ValueError):
        tg.fractal2(0.0, 0.0, tg.FractalParams(wavelength=0.0))
    with pytest.raises(ValueError):
        tg.validate_noise_layer(tg.NoiseLayer(warp_amplitude=-1.0))
    with pytest.raises(ValueError):
        tg.validate_synthetic_terrain(tg.SyntheticTerrainParams(layers=[]))
    with pytest.raises(ValueError):
        tg.generate_synthetic_terrain(0, 4, 1.0, tg.SyntheticTerrainParams())


def test_layers_match_cpp_golden_value():
    base = tg.NoiseLayer(tg.FractalParams(seed=1))
    detail = tg.NoiseLayer(
        tg.FractalParams(seed=2, basis=tg.NoiseBasis.WORLEY_F2_MINUS_F1, amplitude=10.0),
        warp_amplitude=50.0,
    )
    params = tg.SyntheticTerrainParams([base, detail])
    x = np.float32([float.fromhex("0x1.004p+9")])
    y = np.float32([float.fromhex("-0x1.001p+10")])
    assert tg.evaluate_noise_layers(x, y, params)[0] == f32("-0x1.3b5e94p+4")


def test_noise_layer_and_terrain_params_fields():
    layer = tg.NoiseLayer()
    assert layer.enabled and layer.blend == tg.LayerBlend.ADD
    layer.blend = tg.LayerBlend.MASK
    layer.warp_wavelength = 250.0
    assert layer != tg.NoiseLayer()
    params = tg.SyntheticTerrainParams()
    assert len(params.layers) == 1
    params.layers = [layer, tg.NoiseLayer()]
    assert params.layers[0].blend == tg.LayerBlend.MASK
    assert tg.MAX_NOISE_LAYERS == 8


def test_generate_synthetic_terrain_is_centred_and_matches_fill():
    params = tg.SyntheticTerrainParams(
        [tg.NoiseLayer(tg.FractalParams(seed=3, wavelength=50.0)),
         tg.NoiseLayer(tg.FractalParams(basis=tg.NoiseBasis.SIMPLEX, type=tg.FractalType.RIDGED,
                                        wavelength=10.0, amplitude=2.0))]
    )
    raster = tg.generate_synthetic_terrain(17, 9, 1.5, params)
    assert raster.shape == (9, 17)
    assert raster.transform == tg.GeoTransform.north_up(-12.75, 6.75, 1.5)
    filled = tg.Raster(17, 9)
    filled.transform = raster.transform
    tg.fill_synthetic_terrain(filled, params)
    np.testing.assert_array_equal(raster.values, filled.values)

    cols, rows = np.meshgrid(np.arange(17), np.arange(9))
    x = (-12.75 + (cols + 0.5) * 1.5).astype(np.float32)
    y = (6.75 - (rows + 0.5) * 1.5).astype(np.float32)
    np.testing.assert_array_equal(raster.values, tg.evaluate_noise_layers(x, y, params))


def test_compute_stats_skips_nodata_and_nan():
    raster = tg.Raster.from_array(np.array([[1.0, 2.0], [-9999.0, np.nan]], dtype=np.float32))
    raster.nodata = -9999.0
    stats = tg.compute_stats(raster)
    assert (stats.min, stats.max, stats.mean, stats.valid_count) == (1.0, 2.0, 1.5, 2)
    assert "valid_count=2" in repr(stats)
    empty = tg.compute_stats(tg.Raster(0, 0))
    assert empty.valid_count == 0 and math.isnan(empty.mean)
