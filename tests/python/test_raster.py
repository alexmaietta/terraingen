# SPDX-License-Identifier: BSD-3-Clause

"""raster: rasters and their array views, geotransforms, grids, statistics and GeoTIFF I/O."""

import gc
import math

import numpy as np
import pytest

import terraingen


def ramp_raster():
    """An 8 x 8 ramp 0..63 on a centered 2 m grid."""
    return terraingen.Raster.from_array(
        np.arange(64, dtype=np.float32).reshape(8, 8),
        terraingen.RasterGrid.centered((0, 0), (8 * 2.0, 8 * 2.0), 2.0).transform,
    )


def test_constructs_filled_with_shape_height_by_width():
    raster = terraingen.Raster(4, 3, 2.5)
    assert (raster.width, raster.height, raster.shape) == (4, 3, (3, 4))
    assert raster.values.dtype == np.float32
    assert np.all(raster.values == 2.5)
    assert repr(raster) == "Raster(width=4, height=3)"


def test_render_bounds_span_cell_centers_and_valid_heights():
    from terraingen import render

    raster = terraingen.Raster(4, 3, 2.0)
    raster.transform = terraingen.GeoTransform.north_up(100.0, 50.0, 2.0)
    raster.values[0, 0] = math.nan
    raster.values[1, 1] = -1.0
    assert render.terrain_bounds(raster, exaggeration=3.0) == ((101.0, 45.0, -3.0), (107.0, 49.0, 6.0))


def test_values_is_a_writeable_zero_copy_view():
    raster = terraingen.Raster(4, 3)
    raster.values[1, 2] = 7.0
    assert raster.values[1, 2] == 7.0
    assert np.shares_memory(raster.values, raster.values)


def test_values_view_keeps_the_raster_alive():
    view = terraingen.Raster(8, 8, 3.0).values
    gc.collect()
    assert np.all(view == 3.0)


def test_asarray_shares_memory_and_honors_dtype_and_copy():
    raster = terraingen.Raster(3, 2, 1.0)
    assert np.shares_memory(np.asarray(raster), raster.values)
    assert np.asarray(raster, dtype=np.float64).dtype == np.float64
    copied = np.array(raster, copy=True)
    copied[0, 0] = 5.0
    assert raster.values[0, 0] == 1.0


def test_from_dlpack_is_zero_copy():
    raster = terraingen.Raster(5, 4)
    array = np.from_dlpack(raster)
    array[3, 4] = 9.0
    assert raster.values[3, 4] == 9.0
    assert np.shares_memory(array, raster.values)
    assert raster.__dlpack_device__() == (1, 0)


def test_from_array_copies_rows_and_columns():
    source = np.arange(12, dtype=np.float32).reshape(3, 4)
    raster = terraingen.Raster.from_array(source, terraingen.GeoTransform.north_up(0.0, 3.0, 1.0))
    assert raster.shape == (3, 4)
    np.testing.assert_array_equal(raster.values, source)
    source[0, 0] = -1.0
    assert raster.values[0, 0] == 0.0
    assert raster.transform == terraingen.GeoTransform.north_up(0.0, 3.0, 1.0)


def test_from_array_accepts_non_contiguous_and_float64_input():
    source = np.arange(12, dtype=np.float64).reshape(3, 4).T
    raster = terraingen.Raster.from_array(source)
    np.testing.assert_array_equal(raster.values, source.astype(np.float32))


def test_from_array_accepts_dlpack_producers():
    source = np.arange(6, dtype=np.float32).reshape(2, 3)
    raster = terraingen.Raster.from_array(terraingen.Raster.from_array(source))
    np.testing.assert_array_equal(raster.values, source)


def test_from_array_rejects_wrong_dimensions():
    with pytest.raises(TypeError):
        terraingen.Raster.from_array(np.zeros(5, dtype=np.float32))


def test_georeference_and_nodata_round_trip():
    raster = terraingen.Raster(10, 10)
    raster.transform = terraingen.GeoTransform.north_up(100.0, 200.0, 2.0)
    raster.no_data = -9999.0
    assert raster.cell_center(1, 2) == (103.0, 195.0)
    assert not hasattr(raster, "crs")
    assert raster.no_data == -9999.0
    raster.no_data = None
    assert raster.no_data is None


def test_empty_raster_has_empty_views():
    raster = terraingen.Raster(0, 0)
    assert raster.values.shape == (0, 0)
    assert np.from_dlpack(raster).shape == (0, 0)


def test_torch_interop_if_available():
    torch = pytest.importorskip("torch")
    raster = terraingen.Raster(3, 2)
    tensor = torch.from_dlpack(raster)
    tensor[1, 2] = 4.0
    assert raster.values[1, 2] == 4.0


def test_geotransform_default_matches_gdal_default():
    assert terraingen.GeoTransform().to_gdal() == (0.0, 1.0, 0.0, 0.0, 0.0, 1.0)


def test_geotransform_gdal_coefficients_round_trip():
    coefficients = (500.0, 2.0, 0.25, -300.0, -0.5, -2.0)
    assert terraingen.GeoTransform.from_gdal(coefficients).to_gdal() == coefficients


def test_north_up_geotransform_maps_corners_and_centers():
    t = terraingen.GeoTransform.north_up(1000.0, 2000.0, 5.0)
    assert t.pixel_to_world(0.0, 0.0) == (1000.0, 2000.0)
    assert t.pixel_to_world(0.5, 0.5) == (1002.5, 1997.5)
    assert (t.pixel_width, t.pixel_height) == (5.0, -5.0)


def test_world_to_pixel_inverts_pixel_to_world():
    t = terraingen.GeoTransform.from_gdal((500.0, 2.0, 0.25, -300.0, -0.5, -2.0))
    col, row = t.world_to_pixel(*t.pixel_to_world(3.5, 7.25))
    assert col == pytest.approx(3.5)
    assert row == pytest.approx(7.25)


def test_world_to_pixel_raises_when_not_invertible():
    with pytest.raises(ValueError):
        terraingen.GeoTransform.from_gdal((0.0, 1.0, 1.0, 0.0, 1.0, 1.0)).world_to_pixel(1.0, 1.0)


def test_geotransform_fields_are_writable_and_compare_by_value():
    t = terraingen.GeoTransform()
    t.origin_x = 10.0
    t.origin_y = 20.0
    t.pixel_width = 2.0
    t.pixel_height = -2.0
    t.row_rotation = 0.0
    t.col_rotation = 0.0
    assert t == terraingen.GeoTransform.north_up(10.0, 20.0, 2.0)
    assert t != terraingen.GeoTransform()
    assert repr(t).startswith("GeoTransform(10, 2, 0, 20, 0, -2")


def test_compute_stats_skips_nodata_and_nan():
    raster = terraingen.Raster.from_array(np.array([[1.0, 2.0], [-9999.0, np.nan]], dtype=np.float32))
    raster.no_data = -9999.0
    stats = terraingen.compute_stats(raster)
    assert (stats.min, stats.max, stats.mean, stats.valid_count) == (1.0, 2.0, 1.5, 2)
    assert "valid_count=2" in repr(stats)


def test_compute_stats_of_an_empty_raster_has_no_mean():
    empty = terraingen.compute_stats(terraingen.Raster(0, 0))
    assert empty.valid_count == 0 and math.isnan(empty.mean)


def test_grid_area_and_pixel_size():
    grid = terraingen.RasterGrid.centered((0, 0), (20.0, 40.0), 2.0)
    assert grid.area == pytest.approx(800.0) and grid.pixel_size == 2.0


def test_grid_on_a_plane_has_its_flat_area_and_spacing():
    grid = terraingen.RasterGrid.centered((0, 0), (20.0, 40.0), 2.0)
    assert grid.surface_area(terraingen.Surface()) == pytest.approx(800.0)
    assert grid.spacing(terraingen.Surface()) == 2.0


def test_grid_makes_filled_rasters():
    assert terraingen.RasterGrid.centered((0, 0), (20.0, 40.0), 2.0).make_raster(3.0).values.mean() == 3.0


def test_grid_covering_snaps_a_box_outward_from_its_min_corner():
    grid = terraingen.RasterGrid.covering((1.0, 2.0, 3.3, 3.0), 1.0)
    assert (grid.width, grid.height) == (3, 1)
    assert grid.bounds == (1.0, 2.0, 4.0, 3.0)
    with pytest.raises(ValueError):
        terraingen.RasterGrid.covering((1.0, 0.0, 0.0, 1.0), 1.0)


def test_grid_centered_covers_a_box_about_its_center():
    grid = terraingen.RasterGrid.centered((3.0, -2.0), (20.0, 10.0), 0.1)
    assert (grid.width, grid.height) == (200, 100)
    assert grid.bounds == terraingen.RasterGrid.covering((-7.0, -7.0, 13.0, 3.0), 0.1).bounds


def test_geographic_sphere_keys_are_geographic():
    keys = terraingen.geographic_sphere_keys(1737400.0)
    assert keys.is_geographic and not keys.empty and terraingen.GeoKeys().empty


def test_geotiff_info_reports_the_write_options(tmp_path):
    keys = terraingen.geographic_sphere_keys(1737400.0)
    options = terraingen.GeoTiffWriteOptions(compression=terraingen.TiffCompression.DEFLATE, tile_size=16)
    terraingen.write_geotiff(tmp_path / "a.tif", ramp_raster(), keys, options)
    info = terraingen.read_geotiff_info(tmp_path / "a.tif")
    assert (info.width, info.height, info.bands) == (8, 8, 1)
    assert info.compression == terraingen.TiffCompression.DEFLATE and info.tiled
    assert info.sample_format == terraingen.TiffSampleFormat.FLOAT and info.geo_keys == keys


def test_reading_a_missing_geotiff_raises_a_geotiff_error(tmp_path):
    with pytest.raises(terraingen.GeoTiffError):
        terraingen.read_geotiff_info(tmp_path / "missing.tif")
    assert issubclass(terraingen.GeoTiffError, RuntimeError)


def test_geotiff_heights_round_trip(tmp_path, lunar_models, crater_layer):
    craters = crater_layer(lunar_models(3.2), 60.0, 300.0)
    heights = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(129, 129, 8.0), [craters])
    )
    terraingen.write_geotiff(tmp_path / "heights.tif", heights)
    np.testing.assert_array_equal(terraingen.read_geotiff(tmp_path / "heights.tif").values, heights.values)


def test_geotiff_bands_round_trip_and_out_of_range_bands_raise(tmp_path):
    rgb = terraingen.evaluate_albedo(
        terraingen.Albedo(terraingen.SyntheticAlbedo(rgb=True, hue_deg=30.0, saturation=0.2)),
        terraingen.RasterGrid.centered((0, 0), (129 * 8.0, 129 * 8.0), 8.0),
    )
    terraingen.write_geotiff(tmp_path / "rgb.tif", rgb)
    np.testing.assert_array_equal(terraingen.read_geotiff(tmp_path / "rgb.tif", band=2).values, rgb[2].values)
    with pytest.raises(RuntimeError):
        terraingen.read_geotiff(tmp_path / "rgb.tif", band=3)


def test_sample_raster_nearest_reads_the_corner_cell():
    assert terraingen.sample_raster(ramp_raster(), -7.0, 7.0, terraingen.ResampleMethod.NEAREST) == 0.0
