"""Tiling: tile schemes and keys, tiled evaluation and the tile cache."""

import numpy as np
import pytest

import terraingen

GRID = terraingen.RasterGrid.centred(128, 128, 2.0)


@pytest.fixture
def scheme():
    return terraingen.TileScheme.covering(GRID, 64)


@pytest.fixture
def tiled_terrain(lunar_models, crater_layer, noise_relief):
    craters = crater_layer(lunar_models(3.8), 6.0, 60.0)
    return terraingen.Terrain(terraingen.FlatSource(128, 128, 2.0), [noise_relief(2, 100.0, 5.0), craters])


def test_scheme_level_matches_the_grid_spacing(scheme):
    level = scheme.resolving_level(2.0)
    assert scheme.spacing(level) == pytest.approx(2.0) and scheme.tile_size(level) == pytest.approx(128.0)


def test_tiles_covering_the_grid_start_at_its_corner(scheme):
    level = scheme.resolving_level(2.0)
    keys = scheme.tiles_covering(GRID.bounds, level)
    assert len(keys) == 4 and keys[0] == scheme.tile_at(-127.0, 127.0, level)


def test_tiles_match_the_whole_grid(scheme, tiled_terrain):
    whole = terraingen.evaluate(tiled_terrain, grid=GRID).values
    keys = scheme.tiles_covering(GRID.bounds, scheme.resolving_level(2.0))
    tiles = terraingen.evaluate_tiles(tiled_terrain, scheme, keys)
    for key, tile in zip(keys, tiles, strict=True):
        tile_grid = scheme.grid(key)
        col = int(round((tile_grid.transform.origin_x - GRID.transform.origin_x) / 2.0))
        row = int(round((GRID.transform.origin_y - tile_grid.transform.origin_y) / 2.0))
        np.testing.assert_array_equal(tile.values, whole[row : row + 64, col : col + 64])


def test_evaluate_tile_matches_evaluate_tiles(scheme, tiled_terrain):
    keys = scheme.tiles_covering(GRID.bounds, scheme.resolving_level(2.0))
    tiles = terraingen.evaluate_tiles(tiled_terrain, scheme, keys)
    single = terraingen.evaluate_tile(tiled_terrain, scheme, keys[0])
    np.testing.assert_array_equal(single.values, tiles[0].values)


def test_tile_keys_know_their_parent_and_level(scheme):
    level = scheme.resolving_level(2.0)
    key = scheme.tiles_covering(GRID.bounds, level)[0]
    assert key.children[0].parent == key and key.level == level


def test_whole_body_schemes_start_from_eight_root_tiles():
    body = terraingen.TileScheme.whole_body(16)
    assert len(terraingen.TileScheme.body_tiles(1)) == 8 and body.root_size == 180.0


def test_body_tiles_wrap_across_the_pole():
    wrapped = terraingen.TileScheme.wrap_body_tile(terraingen.TileKey(0, -1, 0))
    assert wrapped == terraingen.TileKey(0, 1, 0)


def test_custom_scheme_spacing_halves_per_level():
    custom = terraingen.TileScheme((0.0, 0.0), 100.0, cells=10)
    assert custom.spacing(1) == pytest.approx(5.0) and custom.origin == (0.0, 0.0)


def test_scheme_rejects_a_negative_root_size():
    with pytest.raises(ValueError):
        terraingen.TileScheme((0.0, 0.0), -1.0)


def cached_terrain(noise_relief):
    terrain = terraingen.Terrain(terraingen.FlatSource(256, 256, 0.25), [noise_relief(1, 30.0, 3.0)])
    scheme = terraingen.TileScheme.covering(terraingen.RasterGrid.centred(256, 256, 0.25), 16)
    keys = scheme.tiles_covering(
        terraingen.RasterGrid.centred(256, 256, 0.25).bounds, scheme.resolving_level(0.25)
    )
    return terrain, scheme, keys


def test_tile_cache_evicts_beyond_its_budget(noise_relief):
    terrain, scheme, keys = cached_terrain(noise_relief)
    cache = terraingen.TileCache(terrain, scheme, max_bytes=16 * 16 * 4 * 16)
    for key in keys:
        cache.get(key)
    assert cache.evictions > 0 and cache.max_bytes == 16 * 16 * 4 * 16


def test_tile_cache_regenerates_evicted_tiles_identically(noise_relief):
    terrain, scheme, keys = cached_terrain(noise_relief)
    cache = terraingen.TileCache(terrain, scheme, max_bytes=16 * 16 * 4 * 16)
    first = cache.get(keys[0]).values.copy()
    for key in keys:
        cache.get(key)
    np.testing.assert_array_equal(cache.get(keys[0]).values, first)


def test_tile_cache_matches_evaluate_tile(noise_relief):
    terrain, scheme, keys = cached_terrain(noise_relief)
    cache = terraingen.TileCache(terrain, scheme, max_bytes=16 * 16 * 4 * 16)
    first = cache.get(keys[0]).values.copy()
    np.testing.assert_array_equal(first, terraingen.evaluate_tile(terrain, scheme, keys[0]).values)
