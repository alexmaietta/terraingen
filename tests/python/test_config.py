# SPDX-License-Identifier: BSD-3-Clause

"""pipeline config: JSON round trips, errors, schema and catalog."""

import json

import numpy as np
import pytest

import terraingen


def test_config_repr_counts_its_parts(small_config):
    assert (
        repr(terraingen.TerrainGenConfig.from_dict(small_config)) == "TerrainGenConfig(3 layers, 2 outputs)"
    )


def test_config_round_trips_through_json_and_dicts(small_config):
    config = terraingen.TerrainGenConfig.from_dict(small_config)
    assert terraingen.parse_config(terraingen.config_to_json(config)) == config
    assert terraingen.TerrainGenConfig.from_dict(config.to_dict()) == config
    assert json.loads(terraingen.config_to_json(config, indent=4)) == config.to_dict()


def test_config_loads_from_a_file(tmp_path, small_config):
    path = tmp_path / "terrain.json"
    path.write_text(json.dumps(small_config))
    assert terraingen.load_config(path) == terraingen.TerrainGenConfig.from_dict(small_config)


def test_default_config_has_only_a_schema_version():
    assert terraingen.TerrainGenConfig() == terraingen.TerrainGenConfig.from_dict({"schema_version": 1})


def test_config_on_a_sphere_describes_its_grids():
    config = terraingen.TerrainGenConfig.from_dict(
        {
            "schema_version": 1,
            "body": {"type": "sphere", "radius": 1737400},
            "outputs": [
                {
                    "type": "heightmap",
                    "path": "h.tif",
                    "pixel_size_m": 100,
                    "extent_deg": {"min": [10, 20], "max": [10.05, 20.025]},
                },
                {"type": "mesh", "path": "moon.glb", "cells_per_face": 4},
                {"type": "crater_list", "path": "c.csv", "pixel_size_m": 50000},
                {"type": "report"},
            ],
        }
    )
    assert config.body().is_body
    site, mesh, whole, report = config.output_grids()
    cell = 100 / (1737400 * np.pi / 180)
    assert (site.width, site.height) == (int(np.ceil(0.05 / cell)), int(np.ceil(0.025 / cell)))
    assert site.bounds[0] == 10.0 and site.bounds[1] == 20.0
    assert mesh is None and report is None
    assert whole.bounds == pytest.approx((-180.0, -90.0, 180.0, 90.0))


def test_a_config_without_a_source_or_extent_cannot_write_a_heightmap():
    config = terraingen.TerrainGenConfig.from_dict(
        {"schema_version": 1, "outputs": [{"type": "heightmap", "path": "h.tif", "pixel_size_m": 1}]}
    )
    with pytest.raises(terraingen.ConfigError) as missing:
        config.validate()
    assert missing.value.path == "outputs[0].extent_m"
    config = terraingen.TerrainGenConfig.from_dict(
        {"schema_version": 1, "outputs": [{"type": "heightmap", "extent_m": {"min": [0, 0], "max": [4, 4]}}]}
    )
    with pytest.raises(terraingen.ConfigError) as missing:
        config.validate()
    assert missing.value.path == "outputs[0].pixel_size_m"


def test_unknown_keys_name_their_path():
    with pytest.raises(terraingen.ConfigError) as unknown:
        terraingen.TerrainGenConfig.from_dict({"schema_version": 1, "bogus": 1})
    assert unknown.value.path == "bogus"
    assert isinstance(unknown.value, ValueError)


def test_config_error_is_a_documented_class_of_the_module():
    assert issubclass(terraingen.ConfigError, ValueError)
    assert terraingen.ConfigError.__module__.startswith("terraingen")
    assert "JSON path" in terraingen.ConfigError.__doc__


def test_validation_errors_name_their_path():
    with pytest.raises(terraingen.ConfigError) as empty:
        terraingen.TerrainGenConfig.from_dict(
            {"schema_version": 1, "layers": [{"type": "noise", "stack": []}]}
        ).validate()
    assert empty.value.path == "layers[0].stack"


def test_missing_source_files_fail_validation():
    config = terraingen.TerrainGenConfig.from_dict(
        {"schema_version": 1, "source": {"type": "raster", "path": "none.tif"}}
    )
    with pytest.raises(terraingen.ConfigError) as missing:
        config.validate("/nonexistent")
    assert missing.value.path.startswith("source")
    with pytest.raises(terraingen.ConfigError) as removed:
        terraingen.TerrainGenConfig.from_dict(
            {"schema_version": 1, "base": {"type": "raster", "path": "x.tif"}}
        )
    assert removed.value.path == "base"


def test_a_synthetic_source_has_no_grid_and_upsamples_only_posts():
    synthetic = {"type": "synthetic", "elevation_m": 3, "stack": [{"wavelength": 40}]}
    config = terraingen.TerrainGenConfig.from_dict({"schema_version": 1, "source": synthetic})
    config.validate()
    synthetic["upsampling"] = "bilinear"
    with pytest.raises(terraingen.ConfigError) as unposted:
        terraingen.TerrainGenConfig.from_dict({"schema_version": 1, "source": synthetic}).validate()
    assert unposted.value.path == "source.upsampling"
    synthetic["pixel_size_m"] = 4
    config = terraingen.TerrainGenConfig.from_dict({"schema_version": 1, "source": synthetic})
    config.validate()
    assert config.to_dict()["source"]["upsampling"] == "bilinear"
    terrain = terraingen.build_generator(config)
    assert isinstance(terrain.source, terraingen.SyntheticSource)
    assert terrain.source.pixel_size == 4.0
    noise = terraingen.SyntheticSource(terrain.source.layers)
    expected = 3.0 + noise.height_at(8.0, 4.0, 4.0)
    assert terrain.source.height_at(8.0, 4.0, 4.0) == pytest.approx(expected)
    assert isinstance(terrain.source.upsampler, terraingen.KernelUpsampler)


def test_a_python_upsampler_registered_by_name_serves_a_config(tmp_path):
    class Stepped(terraingen.Upsampler):
        """Each post's height over its whole cell, raised by `lift` meters between posts."""

        def __init__(self, lift=0.0):
            super().__init__()
            self.lift = lift

        def height_at(self, posts, x, y, spacing):
            col, row = posts.transform().world_to_pixel(x, y)
            value = posts.read(int(np.floor(col)), int(np.floor(row)), 1, 1)[0, 0]
            return float(value) + self.lift

    terraingen.register_upsampler("stepped_test", Stepped)
    assert "stepped_test" in terraingen.registered_upsampler_names()
    heights = terraingen.Raster.from_array(
        np.arange(16, dtype=np.float32).reshape(4, 4), terraingen.GeoTransform.north_up(0.0, 40.0, 10.0)
    )
    terraingen.write_geotiff(str(tmp_path / "dem.tif"), heights)
    config = terraingen.TerrainGenConfig.from_dict(
        {
            "schema_version": 1,
            "source": {
                "type": "raster",
                "path": "dem.tif",
                "upsampling": {"type": "stepped_test", "lift": 0.5},
            },
            "outputs": [{"type": "heightmap", "path": "h.tif"}],
        }
    )
    config.validate(str(tmp_path))
    assert config.to_dict()["source"]["upsampling"] == {"type": "stepped_test", "lift": 0.5}
    terrain = terraingen.build_generator(config, str(tmp_path))
    assert terrain.source.height_at(12.0, 37.0, 1.0) == pytest.approx(1.5)
    assert (
        terrain.source.height_at(15.0, 35.0, 10.0) == 1.0 and terrain.source.height_at(15.0, 35.0, 1.0) == 1.5
    )
    fine = terraingen.evaluate(
        terrain, grid=terraingen.RasterGrid(8, 8, terraingen.GeoTransform.north_up(0.0, 40.0, 5.0))
    )
    assert fine.values[0, 2] == pytest.approx(1.5)
    unknown = terraingen.TerrainGenConfig.from_dict(
        {"schema_version": 1, "source": {"type": "raster", "path": "dem.tif", "upsampling": "nope_test"}}
    )
    with pytest.raises(terraingen.ConfigError) as error:
        unknown.validate(str(tmp_path))
    assert error.value.path == "source.upsampling"


def test_loading_a_missing_file_raises_a_config_error():
    with pytest.raises(terraingen.ConfigError):
        terraingen.load_config("/nonexistent/terrain.json")


def test_schema_describes_fields_with_units_and_choices():
    schema = terraingen.config_schema()
    assert schema["schema_version"] == 1
    root = {f["name"]: f for f in schema["root"]["fields"]}
    assert list(root)[:3] == ["schema_version", "body", "source"]
    assert root["layers"]["description"].endswith(".")
    assert root["source"]["schema"]["kind"] == "optional"
    outputs = schema["definitions"][root["outputs"]["schema"]["of"]["ref"]]
    heightmap = next(a for a in outputs["alternatives"] if a["type"] == "heightmap")
    fields = {f["name"]: f for f in heightmap["fields"]}
    assert fields["pixel_size_m"]["unit"] == "m" and fields["pixel_size_m"]["default"] is None
    extent = schema["definitions"][fields["extent_m"]["schema"]["of"]["ref"]]
    assert [f["name"] for f in extent["fields"]] == ["center", "size", "min", "max"]
    assert "report" in [a["type"] for a in outputs["alternatives"]]


def test_catalog_lists_every_type():
    catalog = terraingen.config_catalog()
    assert catalog["schema_version"] == 1
    assert [s["type"] for s in catalog["body"]] == ["plane", "sphere", "ellipsoid", "shape_model"]
    assert "craters" in [layer["type"] for layer in catalog["layers"]]
    assert "density" in catalog["models"]


def test_every_catalog_layer_parses():
    for layer in terraingen.config_catalog()["layers"]:
        terraingen.TerrainGenConfig.from_dict({"schema_version": 1, "layers": [layer]})


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
    config = terraingen.TerrainGenConfig.from_dict(
        crater_config(
            density={"type": "minimum", "distributions": [density, {"type": "trask"}]}, min_diameter_m=20
        )
    )
    assert terraingen.parse_config(terraingen.config_to_json(config)) == config
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
    config = terraingen.TerrainGenConfig.from_dict(crater_config(ages=ages, morphometry="mars"))
    layer = config.to_dict()["layers"][0]
    assert layer["ages"] == ages and layer["morphometry"] == "mars"
    assert terraingen.TerrainGenConfig.from_dict(config.to_dict()) == config


def test_freshness_ages_round_trip_and_validate():
    ages = {
        "type": "freshness",
        "lifetime_gyr": 1.5,
        "freshness": {"kind": "piecewise", "edges": [0.0, 0.3, 1.0], "weights": [1.0, 2.0]},
    }
    config = terraingen.TerrainGenConfig.from_dict(crater_config(ages=ages))
    written = config.to_dict()["layers"][0]["ages"]
    assert written["lifetime_gyr"] == 1.5
    assert written["freshness"]["kind"] == "piecewise" and written["freshness"]["weights"] == [1.0, 2.0]
    assert terraingen.parse_config(terraingen.config_to_json(config)) == config
    config.validate()
    ages["freshness"] = {"kind": "piecewise", "edges": [0.0, 1.0], "weights": [0.0]}
    with pytest.raises(terraingen.ConfigError) as empty:
        terraingen.TerrainGenConfig.from_dict(crater_config(ages=ages)).validate()
    assert empty.value.path == "layers[0].ages"


def test_crater_layers_default_to_a_steady_state_surface_below_trask():
    layer = terraingen.TerrainGenConfig.from_dict(crater_config()).to_dict()["layers"][0]
    assert layer["density"]["type"] == "minimum"
    assert [d["type"] for d in layer["density"]["distributions"]] == ["production_surface", "hartmann"]
    assert layer["ages"]["type"] == "steady_state" and layer["ages"]["equilibrium"] == "hartmann"
    assert layer["degradation"]["type"] == "diffusion"
    assert layer["morphometry"] == "lunar"


def test_removed_body_presets_are_config_errors():
    with pytest.raises(terraingen.ConfigError) as density:
        terraingen.TerrainGenConfig.from_dict(crater_config(density={"type": "lunar_surface"}))
    assert density.value.path == "layers[0].density.type"
    with pytest.raises(terraingen.ConfigError) as body:
        terraingen.TerrainGenConfig.from_dict(crater_config(body="moon"))
    assert body.value.path == "layers[0].body"


def test_an_empty_minimum_fails_validation_at_the_density():
    config = terraingen.TerrainGenConfig.from_dict(
        crater_config(density={"type": "minimum", "distributions": []})
    )
    with pytest.raises(terraingen.ConfigError) as empty:
        config.validate()
    assert empty.value.path == "layers[0].density"


@pytest.mark.parametrize("radius", [0, -1737400])
def test_a_sphere_needs_a_positive_radius(radius):
    config = terraingen.TerrainGenConfig.from_dict(
        {"schema_version": 1, "body": {"type": "sphere", "radius": radius}}
    )
    with pytest.raises(terraingen.ConfigError) as sphere:
        config.validate()
    assert sphere.value.path == "body.radius"


def test_build_generator_builds_on_a_given_source():
    heights = terraingen.Raster.from_array(
        np.full((200, 200), 100.0, dtype=np.float32), terraingen.GeoTransform.north_up(-2000.0, 2000.0, 20.0)
    )
    dem = terraingen.RasterSource(heights)
    crater = {"x": 0, "y": 0, "diameter": 50, "depth": 10}
    config = terraingen.TerrainGenConfig.from_dict(
        {"schema_version": 1, "layers": [{"type": "craters", "generate_random": False, "manual": [crater]}]}
    )
    terrain = terraingen.build_generator(config, source=dem)
    alone = terraingen.build_generator(config)
    assert terraingen.height_at(terrain, 300.0, 300.0, 1.0) == pytest.approx(100.0, abs=1e-4)
    assert terraingen.height_at(terrain, 0.0, 0.0, 1.0) == pytest.approx(
        100.0 + terraingen.height_at(alone, 0.0, 0.0, 1.0), abs=1e-3
    )
    with pytest.raises(terraingen.ConfigError):
        own = terraingen.TerrainGenConfig.from_dict({"schema_version": 1, "source": {"type": "synthetic"}})
        terraingen.build_generator(own, source=dem)
