"""pipeline config: JSON round trips, errors, schema and catalog."""

import json

import pytest

import terraingen


def test_config_repr_counts_its_parts(small_config):
    assert repr(terraingen.Config.from_dict(small_config)) == "Config(3 layers, 2 outputs)"


def test_config_round_trips_through_json_and_dicts(small_config):
    config = terraingen.Config.from_dict(small_config)
    assert terraingen.Config.from_json(config.to_json()) == config
    assert terraingen.Config.from_dict(config.to_dict()) == config
    assert json.loads(config.to_json(indent=4)) == config.to_dict()


def test_config_loads_from_a_file(tmp_path, small_config):
    path = tmp_path / "terrain.json"
    path.write_text(json.dumps(small_config))
    assert terraingen.Config.load(path) == terraingen.Config.from_dict(small_config)


def test_default_config_has_only_a_schema_version():
    assert terraingen.Config() == terraingen.Config.from_dict({"schema_version": 1})


def test_config_on_a_sphere_describes_its_grid():
    config = terraingen.Config.from_dict(
        {
            "schema_version": 1,
            "surface": {"type": "sphere", "radius": 1737400},
            "source": {"type": "flat", "width": 17, "height": 17, "pixel_size": 100},
        }
    )
    assert config.surface.is_body and config.grid().width == 17


def test_unknown_keys_name_their_path():
    with pytest.raises(terraingen.ConfigError) as unknown:
        terraingen.Config.from_dict({"schema_version": 1, "bogus": 1})
    assert unknown.value.path == "bogus"
    assert isinstance(unknown.value, ValueError)


def test_validation_errors_name_their_path():
    with pytest.raises(terraingen.ConfigError) as empty:
        terraingen.Config.from_dict(
            {"schema_version": 1, "layers": [{"type": "noise", "stack": []}]}
        ).validate()
    assert empty.value.path == "layers[0].stack"


def test_missing_source_files_fail_validation():
    config = terraingen.Config.from_dict(
        {"schema_version": 1, "source": {"type": "raster", "path": "none.tif"}}
    )
    with pytest.raises(terraingen.ConfigError) as missing:
        config.validate("/nonexistent")
    assert missing.value.path.startswith("source")


def test_loading_a_missing_file_raises_a_config_error():
    with pytest.raises(terraingen.ConfigError):
        terraingen.Config.load("/nonexistent/terrain.json")


def test_schema_describes_fields_with_units_and_choices():
    schema = terraingen.config_schema()
    assert schema["schema_version"] == 1
    root = {f["name"]: f for f in schema["root"]["fields"]}
    assert list(root)[:3] == ["schema_version", "surface", "source"]
    assert root["layers"]["description"].endswith(".")
    source = schema["definitions"][root["source"]["schema"]["ref"]]
    flat = next(a for a in source["alternatives"] if a["type"] == "flat")
    pixel_size = next(f for f in flat["fields"] if f["name"] == "pixel_size")
    assert pixel_size["unit"] == "m" and pixel_size["default"] == 2.0


def test_catalog_lists_every_type():
    catalog = terraingen.config_catalog()
    assert catalog["schema_version"] == 1
    assert [s["type"] for s in catalog["surface"]] == ["plane", "sphere", "ellipsoid", "shape_model"]
    assert "craters" in [layer["type"] for layer in catalog["layers"]]
    assert "density" in catalog["models"]


def test_every_catalog_layer_parses():
    for layer in terraingen.config_catalog()["layers"]:
        terraingen.Config.from_dict({"schema_version": 1, "layers": [layer]})


def crater_config(**layer):
    return {"schema_version": 1, "layers": [{"type": "craters", **layer}]}


@pytest.mark.parametrize("production", ["neukum", "extended_neukum", "ivanov"])
@pytest.mark.parametrize("chronology", ["neukum", "ivanov"])
def test_production_surfaces_round_trip_and_validate(production, chronology):
    density = {
        "type": "production_surface",
        "production": production,
        "chronology": chronology,
        "age_gyr": 2.0,
    }
    config = terraingen.Config.from_dict(
        crater_config(
            density={"type": "minimum", "distributions": [density, {"type": "trask"}]}, min_diameter=20
        )
    )
    assert terraingen.Config.from_json(config.to_json()) == config
    assert config.to_dict()["layers"][0]["density"]["distributions"][0] == density
    config.validate()


def test_steady_state_ages_and_morphometry_round_trip():
    ages = {
        "type": "steady_state",
        "age_gyr": 3.0,
        "production": "ivanov",
        "equilibrium": "hartmann",
        "chronology": "ivanov",
    }
    config = terraingen.Config.from_dict(crater_config(ages=ages, morphometry="mars"))
    layer = config.to_dict()["layers"][0]
    assert layer["ages"] == ages and layer["morphometry"] == "mars"
    assert terraingen.Config.from_dict(config.to_dict()) == config


def test_crater_layers_default_to_a_power_law_with_uniform_ages():
    layer = terraingen.Config.from_dict(crater_config()).to_dict()["layers"][0]
    assert layer["density"]["type"] == "power_law"
    assert layer["ages"] == {"type": "uniform", "lifetime_gyr": 1.0}
    assert layer["morphometry"] == "lunar"


def test_removed_body_presets_are_config_errors():
    with pytest.raises(terraingen.ConfigError) as density:
        terraingen.Config.from_dict(crater_config(density={"type": "lunar_surface"}))
    assert density.value.path == "layers[0].density.type"
    with pytest.raises(terraingen.ConfigError) as body:
        terraingen.Config.from_dict(crater_config(body="moon"))
    assert body.value.path == "layers[0].body"


def test_an_empty_minimum_fails_validation_at_the_density():
    config = terraingen.Config.from_dict(crater_config(density={"type": "minimum", "distributions": []}))
    with pytest.raises(terraingen.ConfigError) as empty:
        config.validate()
    assert empty.value.path == "layers[0].density"


@pytest.mark.parametrize("radius", [0, -1737400])
def test_a_sphere_needs_a_positive_radius(radius):
    config = terraingen.Config.from_dict(
        {"schema_version": 1, "surface": {"type": "sphere", "radius": radius}}
    )
    with pytest.raises(terraingen.ConfigError) as sphere:
        config.validate()
    assert sphere.value.path == "surface.radius"
