import json

import numpy as np
import pytest

import terraingen as tg

CONFIG = {
    "schema_version": 1,
    "source": {"type": "flat", "width": 81, "height": 65, "pixel_size": 0.5},
    "layers": [
        {"type": "noise", "stack": [{"wavelength": 20, "amplitude": 0.5, "octaves": 3}]},
        {
            "type": "craters",
            "min_diameter": 3,
            "max_diameter": 15,
            "density": {"type": "lunar_surface", "age_gyr": 3.5},
        },
        {"type": "boulders", "coverage": 0.1, "min_diameter": 0.5, "max_diameter": 2},
    ],
    "outputs": [
        {"type": "heightmap", "path": "heights.tif"},
        {"type": "crater_list", "path": "craters.csv"},
    ],
}


def test_config_round_trips_through_json_dicts_and_files(tmp_path):
    config = tg.Config.from_dict(CONFIG)
    assert repr(config) == "Config(3 layers, 0 filters, 2 outputs)"
    assert tg.Config.from_json(config.to_json()) == config
    assert tg.Config.from_dict(config.to_dict()) == config
    assert json.loads(config.to_json(indent=4)) == config.to_dict()
    path = tmp_path / "terrain.json"
    path.write_text(json.dumps(CONFIG))
    assert tg.Config.load(path) == config
    assert tg.Config() == tg.Config.from_dict({"schema_version": 1})


def test_config_errors_name_the_offending_value():
    with pytest.raises(tg.ConfigError) as unknown:
        tg.Config.from_dict({"schema_version": 1, "bogus": 1})
    assert unknown.value.path == "bogus"
    assert isinstance(unknown.value, ValueError)
    with pytest.raises(tg.ConfigError) as empty:
        tg.Config.from_dict({"schema_version": 1, "layers": [{"type": "noise", "stack": []}]}).validate()
    assert empty.value.path == "layers[0].stack"
    with pytest.raises(tg.ConfigError) as missing:
        tg.Config.from_dict({"schema_version": 1, "source": {"type": "raster", "path": "none.tif"}}).validate(
            "/nonexistent"
        )
    assert missing.value.path.startswith("source")
    with pytest.raises(tg.ConfigError):
        tg.Config.load("/nonexistent/terrain.json")


def test_schema_describes_fields_with_units_and_choices():
    schema = tg.config_schema()
    assert schema["schema_version"] == 1
    root = {f["name"]: f for f in schema["root"]["fields"]}
    assert list(root)[:3] == ["schema_version", "surface", "source"]
    assert root["layers"]["description"].endswith(".")
    source = schema["definitions"][root["source"]["schema"]["ref"]]
    flat = next(a for a in source["alternatives"] if a["type"] == "flat")
    pixel_size = next(f for f in flat["fields"] if f["name"] == "pixel_size")
    assert pixel_size["unit"] == "m" and pixel_size["default"] == 2.0


def test_catalog_lists_every_type_and_each_layer_parses():
    catalog = tg.config_catalog()
    assert catalog["schema_version"] == 1
    assert [s["type"] for s in catalog["surface"]] == ["plane", "sphere"]
    assert "craters" in [layer["type"] for layer in catalog["layers"]]
    assert "density" in catalog["models"]
    for layer in catalog["layers"]:
        tg.Config.from_dict({"schema_version": 1, "layers": [layer]})


def test_generate_terrain_returns_heights_craters_and_boulders():
    config = tg.Config.from_dict(CONFIG)
    terrain = tg.generate_terrain(config, threads=2)
    assert terrain.heights.shape == (65, 81)
    heights = terrain.heights.values
    assert np.isfinite(heights).all() and np.ptp(heights) > 0
    craters = terrain.craters
    assert set(craters) == {"x", "y", "diameter", "age_gyr", "id", "manual", "floor_depth", "rim_height", "depth_ratio", "freshness", "complex"}
    assert (craters["depth_ratio"] > 0).all() and (craters["freshness"] <= 1).all()
    assert len(craters["x"]) > 0
    assert craters["diameter"].min() >= 3 and craters["diameter"].max() < 15
    assert craters["id"].dtype == np.uint64 and (craters["manual"] == 0).all()
    boulders = terrain.boulders
    assert len(boulders["x"]) > 0 and boulders["diameter"].min() >= 0.5
    assert terrain.boulder_model_count == 0 and terrain.albedo == []
    again = tg.generate_terrain(config, threads=1)
    np.testing.assert_array_equal(again.heights.values, heights)


def test_generate_terrain_with_an_albedo_and_write_outputs(tmp_path):
    data = dict(CONFIG)
    data["albedo"] = tg.config_catalog()["albedo"]
    data["outputs"] = CONFIG["outputs"] + [{"type": "albedo", "path": "albedo.tif"}]
    config = tg.Config.from_dict(data)
    terrain = tg.generate_terrain(config, tmp_path)
    assert len(terrain.albedo) >= 1 and terrain.albedo[0].shape == (65, 81)
    written = tg.write_outputs(config, terrain, tmp_path)
    assert sorted(p.split("/")[-1] for p in written) == ["albedo.tif", "craters.csv", "heights.tif"]
    assert (tmp_path / "craters.csv").read_text().startswith("x,y,diameter")
