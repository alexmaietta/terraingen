"""sources: flat, GeoTIFF, chained, synthetic and Python height sources."""

import numpy as np
import pytest

import terraingen


def synthetic_params():
    return terraingen.SyntheticTerrainParams(
        [
            terraingen.NoiseLayer(terraingen.FractalParams(seed=3, wavelength=50.0)),
            terraingen.NoiseLayer(
                terraingen.FractalParams(
                    basis=terraingen.NoiseBasis.SIMPLEX,
                    type=terraingen.FractalType.RIDGED,
                    wavelength=10.0,
                    amplitude=2.0,
                )
            ),
        ]
    )


@pytest.fixture
def coarse_dem(tmp_path, noise_relief):
    """A 33 x 33 noise DEM at 8 m written to dem.tif; returns its heights."""
    terrain = terraingen.Terrain(terraingen.FlatSource(33, 33, 8.0), [noise_relief(3, 200.0, 20.0)])
    coarse = terraingen.evaluate(terrain).heights
    terraingen.write_geotiff(tmp_path / "dem.tif", coarse)
    return coarse


def resampled_source(tmp_path):
    return terraingen.GeoTiffSource(
        tmp_path / "dem.tif", pixel_size=2.0, resampling=terraingen.ResampleMethod.BILINEAR, scale=2.0
    )


def test_flat_source_describes_its_grid():
    source = terraingen.FlatSource(65, 33, 2.0, elevation=5.0, centre=(100.0, -50.0))
    assert (source.width, source.height) == (65, 33)
    assert source.transform.pixel_width == 2.0
    assert source.height_at(100.0, -50.0) == 5.0


def test_flat_source_rejects_an_empty_grid():
    with pytest.raises(ValueError):
        terraingen.FlatSource(0, 10, 1.0)


def test_geotiff_source_resamples_to_its_pixel_size(tmp_path, coarse_dem):
    assert resampled_source(tmp_path).width == 132


def test_geotiff_source_scales_heights(tmp_path, coarse_dem):
    heights = terraingen.evaluate(terraingen.Terrain(resampled_source(tmp_path))).heights.values
    assert np.nanmax(heights) == pytest.approx(2 * coarse_dem.values.max(), rel=0.05)


def test_source_chain_takes_the_grid_of_its_first_source(tmp_path, coarse_dem):
    raster = resampled_source(tmp_path)
    chain = terraingen.SourceChain([raster, terraingen.FlatSource(200, 200, 2.0, elevation=-5.0)])
    assert chain.width == raster.width


def test_python_source_describes_its_grid(paraboloid):
    assert paraboloid.width == 65 and paraboloid.reference_point() == (0.0, 0.0)


def test_python_source_fills_the_heights(paraboloid):
    plain = terraingen.evaluate(terraingen.Terrain(paraboloid)).heights.values
    assert plain[32, 32] == pytest.approx(0.0, abs=1e-6) and plain[0, 0] > 4.0


def test_synthetic_terrain_is_centred_on_the_origin():
    raster = terraingen.generate_synthetic_terrain(17, 9, 1.5, synthetic_params())
    assert raster.shape == (9, 17)
    assert raster.transform == terraingen.GeoTransform.north_up(-12.75, 6.75, 1.5)


def test_synthetic_terrain_matches_fill():
    params = synthetic_params()
    raster = terraingen.generate_synthetic_terrain(17, 9, 1.5, params)
    filled = terraingen.Raster(17, 9)
    filled.transform = raster.transform
    terraingen.fill_synthetic_terrain(filled, params)
    np.testing.assert_array_equal(raster.values, filled.values)


def test_synthetic_terrain_samples_the_noise_layers_at_cell_centres():
    params = synthetic_params()
    raster = terraingen.generate_synthetic_terrain(17, 9, 1.5, params)
    cols, rows = np.meshgrid(np.arange(17), np.arange(9))
    x = (-12.75 + (cols + 0.5) * 1.5).astype(np.float32)
    y = (6.75 - (rows + 0.5) * 1.5).astype(np.float32)
    np.testing.assert_array_equal(raster.values, terraingen.evaluate_noise_layers(x, y, params))


def test_synthetic_terrain_rejects_an_empty_grid():
    with pytest.raises(ValueError):
        terraingen.generate_synthetic_terrain(0, 4, 1.0, terraingen.SyntheticTerrainParams())
