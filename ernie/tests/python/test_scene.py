import gc

import numpy as np
import pytest

import ernie
from ernie import Dirty, tokens


def test_paths_parse_navigate_and_compare():
    path = ernie.Path("/World/Sun")
    assert str(path) == "/World/Sun"
    assert path.name == "Sun"
    assert path.parent == ernie.Path("/World")
    assert ernie.Path.root().child("World") == path.parent
    assert path.has_prefix("/World")
    assert not ernie.Path("/Worlds").has_prefix("/World")
    assert ernie.Path.root().is_root
    assert ernie.Path().is_empty
    assert ernie.Path.is_valid_name("_a1") and not ernie.Path.is_valid_name("1a")
    assert len({ernie.Path("/a"), ernie.Path("/a")}) == 1
    assert ernie.Path("/a") < ernie.Path("/a/b") and ernie.Path("/a") != ernie.Path("/b")
    assert repr(path) == "Path('/World/Sun')"
    with pytest.raises(ValueError):
        ernie.Path("World")


def test_define_builds_typeless_ancestors_and_lists_prims():
    scene = ernie.Scene()
    sun = scene.define("/World/Lights/Sun", tokens.DISTANT_LIGHT)
    assert sun and sun.valid and sun.type == "DistantLight"
    assert sun.path == ernie.Path("/World/Lights/Sun")
    assert repr(sun) == "Prim('/World/Lights/Sun')"
    assert scene.type_of("/World") == ""
    assert "/World/Lights" in scene and scene.has("/World")
    assert scene.prims() == [ernie.Path(p) for p in ("/World", "/World/Lights", "/World/Lights/Sun")]
    assert scene.prims(tokens.DISTANT_LIGHT) == [ernie.Path("/World/Lights/Sun")]
    assert scene.children("/World") == [ernie.Path("/World/Lights")]
    assert scene.get_prim("/Missing") is None
    assert scene.get_prim("/World").path == ernie.Path("/World")
    assert scene.remove("/World/Lights") and not scene.remove("/World/Lights")
    assert not sun.valid
    with pytest.raises(ValueError):
        scene.type_of("/World/Lights")


def test_values_round_trip_by_usd_name():
    scene = ernie.Scene()
    prim = scene.define("/P", tokens.XFORM)
    prim.set("b", True).set("i", 3).set("f", 0.5).set("s", "lambert")
    prim.set("rel", ernie.Path("/Looks/Rock")).set("res", (640, 480)).set("uv", (0.5, 1))
    prim.set("rgb", [0.1, 0.2, 0.3]).set("rgba", (1, 2, 3, 4)).set("curve", [1, 2, 3, 4, 5])
    prim.set("np", np.float32(2.5)).set("vec", np.array([1.0, 2.0, 3.0]))
    assert prim.get("b") is True
    assert prim.get("i") == 3
    assert prim.get("f") == 0.5
    assert prim.get("s") == "lambert"
    assert prim.get("rel") == ernie.Path("/Looks/Rock")
    assert prim.get("res") == (640, 480)
    assert prim.get("uv") == (0.5, 1.0)
    assert prim.get("rgb") == pytest.approx((0.1, 0.2, 0.3))
    assert prim.get("rgba") == (1.0, 2.0, 3.0, 4.0)
    assert prim.get("curve") == [1.0, 2.0, 3.0, 4.0, 5.0]
    assert prim.get("np") == 2.5
    assert prim.get("vec") == (1.0, 2.0, 3.0)
    assert prim.get("missing") is None and prim.get("missing", 7) == 7
    assert scene.get("/P", "i") == 3 and scene.get("/Missing", "i", "x") == "x"
    assert set(scene.attributes("/P")) >= {"b", "i", "rel", "rgba"}
    assert scene.clear("/P", "b") and prim.get("b") is None
    prim.set("none", None)
    assert prim.get("none") is None
    for bad in ({}, [True, False], ["a", "b"], [[1, 2], [3, 4]], 2**40):
        with pytest.raises((TypeError, ValueError)):
            prim.set("bad", bad)
    with pytest.raises(ValueError):
        scene.set("/Missing", "x", 1)


def test_matrices_use_column_vectors_and_compose_down_the_tree():
    scene = ernie.Scene()
    move = np.eye(4)
    move[:3, 3] = (10.0, 0.0, 0.0)
    scene.define("/World", tokens.XFORM).set(tokens.TRANSFORM, move)
    scene.define("/World/Rover", tokens.XFORM).set(tokens.TRANSFORM, np.diag([2.0, 2.0, 2.0, 1.0]))
    np.testing.assert_array_equal(scene.get("/World", tokens.TRANSFORM), move)
    np.testing.assert_array_equal(scene.local_transform("/World"), move)
    world = scene.world_transform("/World/Rover")
    np.testing.assert_allclose(world @ np.array([1.0, 0.0, 0.0, 1.0]), [12.0, 0.0, 0.0, 1.0])
    scene.set("/World", tokens.VISIBILITY, tokens.INVISIBLE)
    assert not scene.is_visible("/World/Rover")


def test_look_at_and_light_transforms():
    camera = ernie.look_at_transform((0.0, -10.0, 5.0), (0.0, 0.0, 0.0))
    np.testing.assert_allclose(camera[:3, 3], [0.0, -10.0, 5.0])
    np.testing.assert_allclose(-camera[:3, 2], np.array([0.0, 10.0, -5.0]) / np.sqrt(125.0))
    light = ernie.distant_light_transform((0.0, 0.0, 2.0))
    np.testing.assert_allclose(light[:3, 2], [0.0, 0.0, 1.0])
    with pytest.raises(ValueError):
        ernie.look_at_transform((1, 2, 3), (1, 2, 3))
    with pytest.raises(TypeError):
        ernie.distant_light_transform((1.0, 2.0))


def test_writes_share_memory_and_reads_are_read_only_views():
    scene = ernie.Scene()
    terrain = scene.define("/T", tokens.TERRAIN)
    heights = np.arange(12, dtype=np.float32).reshape(3, 4)
    terrain.write(tokens.HEIGHTS, heights)
    view = scene.read("/T", tokens.HEIGHTS)
    assert np.shares_memory(view, heights)
    assert not view.flags.writeable
    np.testing.assert_array_equal(terrain.read(tokens.HEIGHTS), heights)
    assert scene.get("/T", tokens.HEIGHTS) is None and terrain.read("missing") is None

    del heights, view
    gc.collect()
    np.testing.assert_array_equal(scene.read("/T", tokens.HEIGHTS)[2], [8, 9, 10, 11])


def test_strided_and_typed_arrays_keep_their_layout():
    scene = ernie.Scene()
    scene.define("/T", tokens.TERRAIN)
    base = np.arange(12, dtype=np.float32).reshape(3, 4)
    scene.write("/T", "transposed", base.T)
    np.testing.assert_array_equal(scene.read("/T", "transposed"), base.T)
    for dtype in (np.uint8, np.uint16, np.uint32, np.int32, np.float16, np.float64):
        array = np.ones((2, 2), dtype=dtype)
        scene.write("/T", "typed", array)
        assert scene.read("/T", "typed").dtype == dtype
    for dtype in (np.int64, np.complex64, np.bool_):
        with pytest.raises((TypeError, ValueError)):
            scene.write("/T", "bad", np.ones(2, dtype=dtype))
    assert ernie._ernie._roundtrip(base).base is not None


def test_bulk_writes_spread_rows_over_prims():
    scene = ernie.Scene()
    paths = [f"/Particles/p{i}" for i in range(3)]
    for path in paths:
        scene.define(path, tokens.XFORM)
    positions = np.arange(9, dtype=np.float32).reshape(3, 3)
    scene.write(paths, tokens.POSITIONS, positions)
    row = scene.read("/Particles/p2", tokens.POSITIONS)
    assert np.shares_memory(row, positions)
    np.testing.assert_array_equal(row, [6, 7, 8])
    with pytest.raises(ValueError):
        scene.write(paths[:2], tokens.POSITIONS, positions)


def test_changes_report_dirty_flags_per_cursor():
    scene = ernie.Scene()
    cursor = ernie.ChangeCursor()
    scene.define("/World/Sun", tokens.DISTANT_LIGHT)
    assert scene.changes(cursor) == [
        (ernie.Path("/World"), Dirty.ADDED),
        (ernie.Path("/World/Sun"), Dirty.ADDED),
    ]
    assert cursor.version == scene.version
    assert scene.changes(cursor) == []
    scene.set("/World/Sun", tokens.INTENSITY, 2.0)
    scene.set("/World/Sun", tokens.MATERIAL_BINDING, ernie.Path("/Looks/Rock"))
    [(path, bits)] = scene.changes(cursor)
    assert bits == Dirty.PARAMS | Dirty.MATERIAL
    assert Dirty.MATERIAL in bits and Dirty.POINTS not in bits
    assert ernie.dirty_of(tokens.HEIGHTS) == Dirty.POINTS
    before = scene.attribute_version("/World/Sun", tokens.INTENSITY)
    scene.set("/World/Sun", tokens.INTENSITY, 3.0)
    assert scene.attribute_version("/World/Sun", tokens.INTENSITY) > before


def test_tokens_name_usd_attributes():
    assert tokens.TRANSFORM == "xformOp:transform"
    assert tokens.INTENSITY == "inputs:intensity"
    assert tokens.MATERIAL_BINDING == "material:binding"
    assert tokens.LDR_COLOR == "LdrColor"


def test_scenes_release_arrays_they_no_longer_hold():
    import sys

    heights = np.zeros((4, 4), dtype=np.float32)
    baseline = sys.getrefcount(heights)
    scene = ernie.Scene()
    scene.define("/T", tokens.TERRAIN).write(tokens.HEIGHTS, heights)
    assert sys.getrefcount(heights) > baseline
    scene.clear("/T", tokens.HEIGHTS)
    gc.collect()
    assert sys.getrefcount(heights) == baseline
    scene.write("/T", tokens.HEIGHTS, heights)
    del scene
    gc.collect()
    assert sys.getrefcount(heights) == baseline
