"""meshes: triangle meshes, rigid transforms, mesh and scene writers, and boulder shapes."""

import numpy as np
import pytest

import terraingen


@pytest.fixture
def cube():
    return terraingen.base_solid_mesh(terraingen.BaseSolid.CUBE)


def egg_style():
    return terraingen.BoulderStyle(
        name="egg",
        rounding=terraingen.ValueRange(1.0, 1.0),
        noise_amplitude=0.0,
        intermediate=terraingen.ValueRange(0.8, 0.8),
    )


def shifted_turn():
    turn = terraingen.RigidTransform.rotation_z(90.0)
    return terraingen.RigidTransform.translation_by((5.0, 0.0, 0.0)).after(turn)


def test_cube_volume_area_and_bounds(cube):
    assert cube.volume == pytest.approx(8.0) and cube.surface_area == pytest.approx(24.0)
    assert cube.bounds == ((-1.0, -1.0, -1.0), (1.0, 1.0, 1.0))


def test_mesh_rebuilds_from_its_arrays(cube):
    positions, triangles = cube.arrays
    rebuilt = terraingen.TriangleMesh(positions, triangles.astype(np.uint32))
    assert rebuilt.volume == pytest.approx(8.0)


def test_icosphere_vertices_lie_on_the_unit_sphere():
    sphere = terraingen.icosphere(3)
    assert np.allclose(np.linalg.norm(sphere.arrays[0], axis=1), 1.0)


def test_vertex_normals_have_one_row_per_vertex():
    sphere = terraingen.icosphere(3)
    assert sphere.vertex_normals().shape == (sphere.vertex_count, 3)


def test_creases_split_vertices_at_sharp_edges(cube):
    creased, normals = cube.with_creases(30.0)
    assert creased.vertex_count == 24 and normals.shape == (24, 3)


def test_rotation_turns_x_into_y():
    turn = terraingen.RigidTransform.rotation_z(90.0)
    assert turn.apply((1.0, 0.0, 0.0)) == pytest.approx((0.0, 1.0, 0.0), abs=1e-12)


def test_rigid_transforms_compose_right_to_left():
    assert shifted_turn().apply((1.0, 0.0, 0.0)) == pytest.approx((5.0, 1.0, 0.0))


def test_transformed_mesh_moves_its_bounds(cube):
    assert cube.transformed(shifted_turn()).bounds[0][0] == pytest.approx(4.0)


def test_scaled_mesh_scales_its_volume(cube):
    assert cube.scaled((2.0, 1.0, 1.0)).volume == pytest.approx(16.0)


def test_subdivision_quadruples_the_triangles(cube):
    assert cube.subdivided(1).triangle_count == 4 * cube.triangle_count


def test_meshes_write_ply_and_glb_scenes(tmp_path, cube):
    cube.write(tmp_path / "cube.stl")
    assert terraingen.read_mesh(tmp_path / "cube.stl").mesh.vertex_count == cube.vertex_count
    terraingen.write_scene(
        tmp_path / "scene.glb",
        [terraingen.SceneMesh(cube), terraingen.SceneMesh(terraingen.icosphere(3))],
        [terraingen.SceneNode(0), terraingen.SceneNode(1, scale=(2.0, 2.0, 2.0), pose=shifted_turn())],
    )
    assert (tmp_path / "scene.glb").read_bytes()[:4] == b"glTF"


@pytest.mark.parametrize("name", ["rock.obj", "rock.gltf", "rock.glb"])
def test_meshes_round_trip_with_their_material(tmp_path, cube, name):
    basalt = terraingen.MeshMaterial("basalt", (0.2, 0.25, 0.3))
    cube.write(tmp_path / name, material=basalt)
    read = terraingen.read_mesh(tmp_path / name)
    np.testing.assert_allclose(read.mesh.arrays[0], cube.arrays[0], atol=1e-6)
    assert read.material.name == "basalt"
    assert read.material.colour == pytest.approx((0.2, 0.25, 0.3))
    assert terraingen.is_mesh_path(tmp_path / name) and not terraingen.is_mesh_path("rock.ply")


def test_boulder_shapes_draw_from_a_library_of_rocks(cube):
    rock = terraingen.SceneMesh(cube.scaled((3.0, 1.0, 2.0)), terraingen.MeshMaterial("granite"))
    options = terraingen.BoulderShapeOptions(subdivisions=1)
    options.library = [rock]
    shape = terraingen.generate_boulder_shape(5, options)
    assert shape.material.name == "granite"
    assert shape.intermediate == pytest.approx(1.0 / 3.0)
    assert shape.short_axis == pytest.approx(2.0 / 3.0)
    assert len(options.library) == 1
    normalized = terraingen.normalized_boulder_shape(rock)
    assert np.ptp(normalized.mesh.arrays[0][:, 0]) == pytest.approx(1.0)


def test_default_boulder_styles():
    styles = terraingen.default_boulder_styles()
    assert [s.name for s in styles] == [
        "angular",
        "platy",
        "subrounded",
        "rounded",
        "block",
        "wedge",
        "pyramid",
        "shard",
        "sphere",
    ]
    assert {s.solid for s in styles} == set(terraingen.BaseSolid.__members__.values())
    assert all(s.edge_softness.max > 0.0 and s.face_roughness > 0.0 for s in styles)


def test_boulder_shapes_are_deterministic():
    options = terraingen.BoulderShapeOptions(subdivisions=2, seed=3)
    shape = terraingen.generate_boulder_shape(7, options)
    again = terraingen.generate_boulder_shape(7, options)
    np.testing.assert_array_equal(shape.mesh.arrays[0], again.mesh.arrays[0])


def test_boulder_shapes_are_unit_long_with_ordered_axes():
    shape = terraingen.generate_boulder_shape(7, terraingen.BoulderShapeOptions(subdivisions=2, seed=3))
    positions, _ = shape.mesh.arrays
    assert np.ptp(positions[:, 0]) == pytest.approx(1.0, abs=0.05)
    assert 0 < shape.short_axis <= shape.intermediate <= 1.0


def test_custom_boulder_style_sets_the_intermediate_axis(tmp_path):
    smooth = terraingen.generate_boulder_shape(
        1, terraingen.BoulderShapeOptions(styles=[egg_style()], subdivisions=3)
    )
    assert smooth.style == 0 and smooth.intermediate == pytest.approx(0.8)
    smooth.mesh.write(tmp_path / "egg.glb")


def test_boulder_shape_seed_wraps_to_the_prototype_count():
    assert terraingen.boulder_shape_seed(12345) == 12345
    assert terraingen.boulder_shape_seed(12345, 16) < 16


def test_boulder_shapes_need_a_style():
    with pytest.raises(ValueError):
        terraingen.generate_boulder_shape(1, terraingen.BoulderShapeOptions(styles=[]))


def test_validate_boulder_shape_options_accepts_the_defaults():
    terraingen.validate_boulder_shape_options(terraingen.BoulderShapeOptions())


def test_icq_models_round_trip_and_triangulate(tmp_path):
    q = 4
    model = terraingen.IcqModel()
    model.q = q
    model.vertices = [
        tuple(1000.0 * c for c in terraingen.icq_direction(q, i, j, f))
        for f in range(1, 7)
        for j in range(q + 1)
        for i in range(q + 1)
    ]
    assert model.index(q, q, 6) == len(model.vertices) - 1
    path = tmp_path / "body.icq"
    terraingen.write_icq(path, model)
    back = terraingen.read_icq(path)
    assert back.q == q and len(back.vertices) == 6 * (q + 1) ** 2
    mesh = back.to_mesh()
    assert mesh.vertex_count == 6 * q * q + 2 and mesh.triangle_count == 12 * q * q
    assert mesh.volume > 0.0
    assert terraingen.read_mesh(path).mesh.triangle_count == 12 * q * q


def test_body_icq_follows_an_ellipsoid():
    body = terraingen.Surface.ellipsoid(270.0, 147.0, 104.0)
    terrain = terraingen.Terrain(terraingen.FlatSource(4, 4, 1.0, 0.0), surface=body)
    model = terraingen.body_icq(terrain, 4)
    assert model.q == 4
    x = model.vertices[model.index(2, 2, 5)][0]
    assert x == pytest.approx(270.0)


def test_shape_models_come_from_meshes_and_icq_models(tmp_path):
    mesh = terraingen.icosphere(4).scaled((300.0, 200.0, 100.0))
    shape = terraingen.shape_model_from_mesh(mesh, 8)
    assert shape.q == 8
    assert shape.at((1.0, 0.0, 0.0)) == pytest.approx(300.0, rel=1e-3)
    assert shape.at((0.0, 0.0, 1.0)) == pytest.approx(100.0, rel=1e-3)
    model = terraingen.to_icq(shape)
    assert model.q == 8 and model.to_radial_shape().radii == pytest.approx(shape.radii)
    path = tmp_path / "shape.icq"
    terraingen.write_icq(path, model)
    assert terraingen.read_icq(path).to_radial_shape().radii == pytest.approx(shape.radii, abs=1e-5)
    with pytest.raises(ValueError):
        terraingen.shape_model_from_mesh(mesh, 0)
