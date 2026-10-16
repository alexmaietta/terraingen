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
    return terraingen.body_sphere(terraingen.Body.MOON)


def test_version_matches_cmake(repo_root):
    cmake = (repo_root / "CMakeLists.txt").read_text()
    expected = re.search(r'set\(TERRAINGEN_FULL_VERSION "([^"]+)"\)', cmake).group(1)
    assert terraingen.__version__ == expected
    assert ".".join(str(part) for part in terraingen.version()) == expected


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


def test_sphere_point_of_inverts_a_direction(moon):
    assert moon.point_of((0.0, 1.0, 0.0)) == pytest.approx((90.0, 0.0))


def test_sphere_expand_grows_a_region(moon):
    assert moon.expand((0.0, 0.0, 1.0, 1.0), 30000.0)[0] < 0.0


def test_sphere_contains_regions_across_the_antimeridian(moon):
    assert moon.contains((170.0, -5.0, 190.0, 5.0), (-175.0, 0.0))


def test_surfaces_compare_by_value(moon):
    assert moon == terraingen.body_sphere(terraingen.Body.MOON) and moon != terraingen.Surface()


def test_plane_surface_has_unit_length_and_euclidean_distance():
    plane = terraingen.Surface()
    assert not plane.is_sphere and plane.unit_length == 1.0
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
    cell = sphere.cell_of(23.0, 1.0, 50000.0)
    assert cell in sphere.neighbours(cell, 50000.0, 1) and sphere.cells_per_edge(50000.0) > 10


def test_cube_sphere_cell_area_is_near_the_cell_size_squared():
    sphere = terraingen.CubeSphereCells(1737400.0)
    cell = sphere.cell_of(23.0, 1.0, 50000.0)
    assert sphere.cell_area(cell, 50000.0) == pytest.approx(50000.0**2, rel=0.5)


def test_cube_sphere_direction_is_a_unit_vector():
    assert np.linalg.norm(terraingen.CubeSphereCells.direction(0, 0.0, 0.0)) == pytest.approx(1.0)


def test_exponential_integral_matches_a_reference_value():
    assert terraingen.exponential_integral(1, 1.0) == pytest.approx(0.2193839, rel=1e-5)
