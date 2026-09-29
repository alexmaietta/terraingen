import pytest

import terraingen as tg


def test_default_matches_gdal_default():
    assert tg.GeoTransform().to_gdal() == (0.0, 1.0, 0.0, 0.0, 0.0, 1.0)


def test_gdal_coefficients_round_trip():
    coefficients = (500.0, 2.0, 0.25, -300.0, -0.5, -2.0)
    assert tg.GeoTransform.from_gdal(coefficients).to_gdal() == coefficients


def test_north_up_maps_corners_and_centres():
    t = tg.GeoTransform.north_up(1000.0, 2000.0, 5.0)
    assert t.pixel_to_world(0.0, 0.0) == (1000.0, 2000.0)
    assert t.pixel_to_world(0.5, 0.5) == (1002.5, 1997.5)
    assert (t.pixel_width, t.pixel_height) == (5.0, -5.0)


def test_world_to_pixel_inverts_pixel_to_world():
    t = tg.GeoTransform.from_gdal((500.0, 2.0, 0.25, -300.0, -0.5, -2.0))
    col, row = t.world_to_pixel(*t.pixel_to_world(3.5, 7.25))
    assert col == pytest.approx(3.5)
    assert row == pytest.approx(7.25)


def test_world_to_pixel_raises_when_not_invertible():
    with pytest.raises(ValueError):
        tg.GeoTransform.from_gdal((0.0, 1.0, 1.0, 0.0, 1.0, 1.0)).world_to_pixel(1.0, 1.0)


def test_fields_are_writable_and_compare_by_value():
    t = tg.GeoTransform()
    t.origin_x = 10.0
    t.origin_y = 20.0
    t.pixel_width = 2.0
    t.pixel_height = -2.0
    t.row_rotation = 0.0
    t.col_rotation = 0.0
    assert t == tg.GeoTransform.north_up(10.0, 20.0, 2.0)
    assert t != tg.GeoTransform()
    assert repr(t).startswith("GeoTransform(10, 2, 0, 20, 0, -2")
