import gc

import numpy as np
import pytest

import terraingen as tg


def test_constructs_filled_with_shape_height_by_width():
    raster = tg.Raster(4, 3, 2.5)
    assert (raster.width, raster.height, raster.shape) == (4, 3, (3, 4))
    assert raster.values.dtype == np.float32
    assert np.all(raster.values == 2.5)
    assert repr(raster) == "Raster(width=4, height=3)"


def test_values_is_a_writeable_zero_copy_view():
    raster = tg.Raster(4, 3)
    raster.values[1, 2] = 7.0
    assert raster.values[1, 2] == 7.0
    assert np.shares_memory(raster.values, raster.values)


def test_values_view_keeps_the_raster_alive():
    view = tg.Raster(8, 8, 3.0).values
    gc.collect()
    assert np.all(view == 3.0)


def test_asarray_shares_memory_and_honours_dtype_and_copy():
    raster = tg.Raster(3, 2, 1.0)
    assert np.shares_memory(np.asarray(raster), raster.values)
    assert np.asarray(raster, dtype=np.float64).dtype == np.float64
    copied = np.array(raster, copy=True)
    copied[0, 0] = 5.0
    assert raster.values[0, 0] == 1.0


def test_from_dlpack_is_zero_copy():
    raster = tg.Raster(5, 4)
    array = np.from_dlpack(raster)
    array[3, 4] = 9.0
    assert raster.values[3, 4] == 9.0
    assert np.shares_memory(array, raster.values)
    assert raster.__dlpack_device__() == (1, 0)


def test_from_array_copies_rows_and_columns():
    source = np.arange(12, dtype=np.float32).reshape(3, 4)
    raster = tg.Raster.from_array(source, tg.GeoTransform.north_up(0.0, 3.0, 1.0))
    assert raster.shape == (3, 4)
    np.testing.assert_array_equal(raster.values, source)
    source[0, 0] = -1.0
    assert raster.values[0, 0] == 0.0
    assert raster.transform == tg.GeoTransform.north_up(0.0, 3.0, 1.0)


def test_from_array_accepts_non_contiguous_and_float64_input():
    source = np.arange(12, dtype=np.float64).reshape(3, 4).T
    raster = tg.Raster.from_array(source)
    np.testing.assert_array_equal(raster.values, source.astype(np.float32))


def test_from_array_accepts_dlpack_producers():
    source = np.arange(6, dtype=np.float32).reshape(2, 3)
    raster = tg.Raster.from_array(tg.Raster.from_array(source))
    np.testing.assert_array_equal(raster.values, source)


def test_from_array_rejects_wrong_dimensions():
    with pytest.raises(TypeError):
        tg.Raster.from_array(np.zeros(5, dtype=np.float32))


def test_georeference_and_nodata_round_trip():
    raster = tg.Raster(10, 10)
    raster.transform = tg.GeoTransform.north_up(100.0, 200.0, 2.0)
    raster.crs = 'LOCAL_CS["test"]'
    raster.nodata = -9999.0
    assert raster.cell_center(1, 2) == (103.0, 195.0)
    assert raster.crs == 'LOCAL_CS["test"]'
    assert raster.nodata == -9999.0
    raster.nodata = None
    assert raster.nodata is None


def test_empty_raster_has_empty_views():
    raster = tg.Raster(0, 0)
    assert raster.values.shape == (0, 0)
    assert np.from_dlpack(raster).shape == (0, 0)


def test_torch_interop_if_available():
    torch = pytest.importorskip("torch")
    raster = tg.Raster(3, 2)
    tensor = torch.from_dlpack(raster)
    tensor[1, 2] = 4.0
    assert raster.values[1, 2] == 4.0
