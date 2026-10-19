"""util: version and build info, logging, surfaces, cell grids and special functions."""

import logging
import re

import numpy as np
import pytest

import terraingen


@pytest.fixture
def restore_logging():
    yield
    terraingen.use_python_logging()
    terraingen.set_log_level("warning")


@pytest.fixture
def moon():
    return terraingen.Surface.sphere(1737400.0)


def test_version_matches_cmake(repo_root):
    cmake = (repo_root / "CMakeLists.txt").read_text()
    expected = re.search(r'set\(TERRAINGEN_FULL_VERSION "([^"]+)"\)', cmake).group(1)
    assert terraingen.__version__ == expected
    assert (
        ".".join(str(part) for part in terraingen.version()) == re.match(r"\d+\.\d+\.\d+", expected).group()
    )


def test_api_version_is_positive():
    assert terraingen.api_version() >= 1


def test_build_info_has_expected_fields():
    info = terraingen.build_info()
    assert set(info) == {"git_sha", "git_dirty", "build_date"}
    assert info["git_sha"]
    assert isinstance(info["git_dirty"], bool)


@pytest.mark.usefixtures("restore_logging")
def test_log_level_round_trips_and_rejects_unknown_names():
    assert terraingen.get_log_level() == "warning"
    terraingen.set_log_level("DEBUG")
    assert terraingen.get_log_level() == "debug"
    with pytest.raises(ValueError):
        terraingen.set_log_level("loud")


@pytest.mark.usefixtures("restore_logging")
def test_log_records_reach_the_logging_module(caplog):
    terraingen.set_log_level("info")
    with caplog.at_level(logging.INFO, logger="terraingen"):
        terraingen.generate_synthetic_terrain(9, 7, 1.0, terraingen.SyntheticTerrainParams())
    records = [r for r in caplog.records if r.name == "terraingen.sources"]
    assert len(records) == 1
    assert records[0].levelno == logging.INFO
    assert records[0].getMessage().startswith("Filled 9x7 synthetic terrain (1 layer) in ")


@pytest.mark.usefixtures("restore_logging")
def test_nothing_is_logged_below_the_threshold(caplog):
    with caplog.at_level(logging.DEBUG, logger="terraingen"):
        terraingen.generate_synthetic_terrain(4, 4, 1.0, terraingen.SyntheticTerrainParams())
    assert not [r for r in caplog.records if r.name.startswith("terraingen")]


@pytest.mark.usefixtures("restore_logging")
def test_custom_log_sinks_receive_level_category_and_message():
    received = []
    terraingen.set_log_sink(lambda level, category, message: received.append((level, category, message)))
    terraingen.set_log_level("info")
    terraingen.generate_synthetic_terrain(3, 3, 1.0, terraingen.SyntheticTerrainParams())
    assert len(received) == 1
    level, category, message = received[0]
    assert (level, category) == ("info", "sources")
    assert message.startswith("Filled 3x3")


def test_sphere_unit_length_is_one_degree_of_arc(moon):
    assert moon.unit_length == pytest.approx(moon.radius * np.pi / 180.0)
    assert moon.distance((0.0, 0.0), (1.0, 0.0)) == pytest.approx(moon.unit_length)


def test_sphere_position_puts_the_north_pole_on_z(moon):
    x, _, z = moon.position((0.0, 90.0))
    assert z == pytest.approx(moon.radius) and abs(x) < 1e-6


def test_sphere_offset_inverts_locate(moon):
    east, north = moon.offset((10.0, 0.0), moon.locate((10.0, 0.0), (500.0, -200.0)))
    assert (east, north) == pytest.approx((500.0, -200.0), abs=1e-6)


def test_sphere_frame_points_up_and_north(moon):
    _, north, up = moon.frame((0.0, 0.0))
    assert up == pytest.approx((1.0, 0.0, 0.0)) and north == pytest.approx((0.0, 0.0, 1.0))


def test_sphere_coordinates_invert_a_direction(moon):
    assert moon.coordinates((0.0, 1.0, 0.0)) == pytest.approx((90.0, 0.0))


def test_sphere_expand_grows_a_region(moon):
    assert moon.expand((0.0, 0.0, 1.0, 1.0), 30000.0)[0] < 0.0


def test_sphere_contains_regions_across_the_antimeridian(moon):
    assert moon.contains((170.0, -5.0, 190.0, 5.0), (-175.0, 0.0))


def test_surfaces_compare_by_value(moon):
    assert moon == terraingen.Surface.sphere(1737400.0) and moon != terraingen.Surface()


def test_ellipsoid_of_equal_radii_is_the_sphere(moon):
    assert terraingen.Surface.ellipsoid(1737400.0, 1737400.0, 1737400.0) == moon
    assert moon.radii == (1737400.0, 1737400.0, 1737400.0)


def test_ellipsoid_lies_on_its_radii_and_offsets_invert_locate():
    body = terraingen.Surface.ellipsoid(270.0, 147.0, 104.0)
    assert body.is_body and body.radii == (270.0, 147.0, 104.0)
    assert body.radius_at((0.0, 0.0)) == pytest.approx(270.0)
    assert body.radius_at((0.0, 90.0)) == pytest.approx(104.0)
    assert body.area_per_steradian((90.0, 0.0)) == pytest.approx(147.0**2)
    point = body.locate((30.0, 10.0), (4.0, -3.0))
    assert body.offset((30.0, 10.0), point) == pytest.approx((4.0, -3.0))
    assert body.distance((30.0, 10.0), point) == pytest.approx(5.0, rel=1e-3)
    with pytest.raises(ValueError):
        terraingen.Surface.ellipsoid(1.0, 0.0, 1.0)


def test_plane_surface_has_unit_length_and_euclidean_distance():
    plane = terraingen.Surface()
    assert not plane.is_body and plane.unit_length == 1.0
    assert terraingen.Surface.plane().distance((0.0, 0.0), (3.0, 4.0)) == pytest.approx(5.0)


def test_planar_cells_cover_a_region_with_square_cells():
    planar = terraingen.PlanarCells()
    cells = planar.cells_covering((0.0, 0.0, 25.0, 15.0), 10.0)
    assert len(cells) == 6 and planar.cell_area(cells[0], 10.0) == pytest.approx(100.0)


def test_planar_cells_sample_points_inside_their_cell():
    px, py = terraingen.PlanarCells().sample_point(0.5, 0.5, terraingen.CellIndex(1, 2), 10.0)
    assert (px, py) == pytest.approx((15.0, 25.0))


def test_cube_sphere_cells_neighbour_themselves():
    sphere = terraingen.CubeSphereCells(1737400.0)
    cell = sphere.cell_at(23.0, 1.0, 50000.0)
    assert cell in sphere.neighbours(cell, 50000.0, 1) and sphere.cells_per_edge(50000.0) > 10


def test_cube_sphere_cell_area_is_near_the_cell_size_squared():
    sphere = terraingen.CubeSphereCells(1737400.0)
    cell = sphere.cell_at(23.0, 1.0, 50000.0)
    assert sphere.cell_area(cell, 50000.0) == pytest.approx(50000.0**2, rel=0.5)


def test_cube_sphere_direction_is_a_unit_vector():
    assert np.linalg.norm(terraingen.CubeSphereCells.direction(0, 0.0, 0.0)) == pytest.approx(1.0)


def test_exponential_integral_matches_a_reference_value():
    assert terraingen.exponential_integral(1, 1.0) == pytest.approx(0.2193839, rel=1e-5)


def test_ellipsoid_cells_thin_by_area_and_spheres_do_not(moon):
    body = terraingen.Surface.ellipsoid(270.0, 147.0, 104.0)
    cells = terraingen.CubeSphereCells(body)
    assert cells.radius == 104.0
    area, bound = cells.cell_sampling(terraingen.CellIndex(0, 0), 50.0)
    assert bound > 0.0 and area >= cells.cell_area(terraingen.CellIndex(0, 0), 50.0)
    point = cells.sample_point(0.5, 0.5, terraingen.CellIndex(0, 0), 50.0)
    assert cells.weight(point) == pytest.approx(body.area_per_steradian(point))
    assert terraingen.CubeSphereCells(moon).cell_sampling(terraingen.CellIndex(0, 0), 50000.0)[1] == 0.0


def test_radial_shapes_interpolate_their_labels_and_make_shape_model_surfaces():
    shape = terraingen.RadialShape.sample(8, lambda u: 300.0 + 40.0 * u[0])
    assert shape.q == 8 and len(shape.radii) == 6 * 9 * 9
    assert shape.at((1.0, 0.0, 0.0)) == pytest.approx(340.0)
    assert shape.min_radius == pytest.approx(260.0) and shape.max_radius == pytest.approx(340.0)
    direction = terraingen.RadialShape.label_direction(8, 2, 3, 4)
    assert shape.radii[shape.label_index(2, 3, 4)] == pytest.approx(shape.at(direction))
    assert terraingen.RadialShape(8, shape.radii) == shape
    with pytest.raises(ValueError):
        terraingen.RadialShape(2, [1.0] * 10)

    body = terraingen.Surface.shape_model(shape)
    assert body.is_body and not body.is_sphere and body.shape == shape
    assert body.min_radius == pytest.approx(260.0) and body.max_radius == pytest.approx(340.0)
    assert body.radius_at((0.0, 0.0)) == pytest.approx(340.0)
    assert body.area_per_steradian((0.0, 0.0)) <= shape.max_area_per_steradian
    point = body.locate((30.0, 10.0), (4.0, -3.0))
    assert body.offset((30.0, 10.0), point) == pytest.approx((4.0, -3.0))
    assert terraingen.Surface.sphere(10.0).is_sphere and terraingen.Surface.sphere(10.0).shape is None
    cells = terraingen.CubeSphereCells(body)
    assert cells.radius == body.min_radius
    assert cells.cell_sampling(terraingen.CellIndex(0, 0), 50.0)[1] == shape.max_area_per_steradian
