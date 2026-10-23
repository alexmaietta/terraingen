# SPDX-License-Identifier: BSD-3-Clause

"""report: statistics of evaluated grids and terrain reports as JSON and dicts."""

import json
import math

import numpy as np

import terraingen


def plane_raster(width, height, pixel_size, slope_x):
    """A north-up raster rising `slope_x` meters per meter eastward."""
    transform = terraingen.GeoTransform.north_up(0.0, height * pixel_size, pixel_size)
    x = (np.arange(width) + 0.5) * pixel_size
    values = np.tile(slope_x * x, (height, 1)).astype(np.float32)
    return terraingen.Raster.from_array(values, transform)


def test_report_grid_gives_exact_statistics_of_a_ramp():
    report = terraingen.report_grid(plane_raster(5, 3, 1.0, 1.0))
    assert report.grid.width == 5 and report.grid.height == 3
    assert (report.height_min, report.height_max, report.height_mean) == (0.5, 4.5, 2.5)
    assert math.isclose(report.height_std, math.sqrt(2.0))
    assert math.isclose(report.slope_mean_deg, 45.0)
    assert math.isclose(report.slope_median_deg, 45.0) and math.isclose(report.slope_p95_deg, 45.0)
    assert report.crater_bins == [] and report.boulder_count == 0


def test_report_grid_skips_cells_without_data():
    raster = plane_raster(4, 4, 1.0, 2.0)
    raster.values[0, 0] = np.nan
    report = terraingen.report_grid(raster)
    assert report.height_min == 1.0
    assert math.isclose(report.slope_p95_deg, math.degrees(math.atan(2.0)))


def test_report_grid_bins_the_placed_features(small_config):
    generated = terraingen.generate_from_config(
        terraingen.TerrainGenConfig.from_dict(small_config), threads=2
    )
    grid = generated.grids[0]
    craters = generated.placed_craters()
    boulders = generated.placed_boulders()
    target = terraingen.PowerLawSfd(0.01, 2.0)
    plane = terraingen.Surface.plane()
    report = terraingen.report_grid(
        grid.heights,
        plane,
        craters=craters,
        boulders=boulders,
        crater_target=target,
        threads=2,
    )
    assert sum(b.count for b in report.crater_bins) == len(craters.craters)
    area = terraingen.RasterGrid.from_raster(grid.heights).surface_area(plane)
    first = report.crater_bins[0]
    assert first.max_diameter == 2 * first.min_diameter
    assert math.isclose(first.expected, area * 0.01 * (first.min_diameter**-2 - first.max_diameter**-2))
    assert report.boulder_count == len(boulders.boulders) and report.boulder_model_count == 0
    cover = sum(math.pi * b.diameter**2 / 4 for b in boulders.boulders) / area
    assert math.isclose(report.boulder_cover, cover)


def test_terrain_reports_round_trip_through_json():
    report = terraingen.TerrainReport()
    report.grids = [terraingen.report_grid(plane_raster(3, 3, 2.0, 0.0))]
    report.layers = [terraingen.LayerTiming("noise", 0.25)]
    report.backend = "cpu"
    report.threads = 2
    report.seeds = [1, 2]
    report.seconds = 1.5
    parsed = json.loads(terraingen.report_to_json(report))
    assert parsed == report.to_dict()
    assert parsed["version"] == report.version != ""
    assert parsed["layers"] == [{"type": "noise", "seconds": 0.25}]
    assert parsed["seeds"] == [1, 2] and parsed["threads"] == 2
    grid = parsed["grids"][0]
    assert grid["grid"]["pixel_size"] == 2.0 and grid["slope_deg"]["mean"] == 0.0
    assert grid["boulders"] == {"count": 0, "models": 0, "cover": 0.0}
    assert "\n" not in terraingen.report_to_json(report, indent=-1)


def test_a_report_output_returns_and_writes_the_report(tmp_path, small_config):
    area = {key: small_config["outputs"][0][key] for key in ("pixel_size_m", "extent_m")}
    small_config["outputs"] += [
        {"type": "boulder_list", **area},
        {"type": "report", "path": "report.json"},
    ]
    config = terraingen.TerrainGenConfig.from_dict(small_config)
    generated = terraingen.generate_from_config(config, threads=2)
    report = generated.report
    assert isinstance(report, terraingen.TerrainReport)
    assert [layer.type for layer in report.layers] == ["noise", "craters", "boulders"]
    assert report.threads == 2 and report.backend == "cpu" and report.seeds == [1, 1]
    grid = generated.grids[0]
    assert len(report.grids) == 1
    stats = report.grids[0]
    assert sum(b.count for b in stats.crater_bins) == len(grid.craters.craters)
    assert stats.boulder_count == len(grid.boulders.boulders) > 0
    written = terraingen.write_outputs(config, generated, tmp_path)
    assert tmp_path / "report.json" in written
    assert json.loads((tmp_path / "report.json").read_text()) == report.to_dict()

    del small_config["outputs"][-1]
    assert terraingen.generate_from_config(terraingen.TerrainGenConfig.from_dict(small_config)).report is None
