import math

import numpy as np
import pytest

import terraingen


def test_noise_in_3d_and_layer_helpers():
    x = np.array([0.3, 1.7e6, -2.5e5], dtype=np.float64)
    y = np.array([0.2, -3.1e5, 7.0], dtype=np.float64)
    z = np.array([0.1, 2.2e5, 1.0], dtype=np.float64)
    values = terraingen.perlin_noise3(x, y, z, 4)
    assert values.shape == (3,) and np.all(np.abs(values) <= 1.0)
    assert values[0] == pytest.approx(terraingen.perlin_noise3(0.3, 0.2, 0.1, 4))
    assert terraingen.perlin_noise3(1.0, 2.0, 3.0, 4) == 0.0
    assert -1.0 <= terraingen.value_noise3(0.5, 0.25, 0.75, 4) <= 1.0
    assert terraingen.has_basis_noise3(terraingen.NoiseBasis.VALUE) and not terraingen.has_basis_noise3(terraingen.NoiseBasis.SIMPLEX)
    assert terraingen.basis_noise3(terraingen.NoiseBasis.PERLIN, 1, 2, 3, 0.0, 0.0, 0.0, 4) == 0.0
    params = terraingen.FractalParams(seed=2, wavelength=1000.0, amplitude=10.0, octaves=4)
    heights = terraingen.fractal3(x, y, z, params)
    assert np.all(np.abs(heights) <= 10.0) and heights[1] == pytest.approx(terraingen.fractal3(1.7e6, -3.1e5, 2.2e5, params))
    stack = terraingen.SyntheticTerrainParams([terraingen.NoiseLayer(params)])
    np.testing.assert_allclose(terraingen.evaluate_noise_layers3(x, y, z, stack), heights)
    layer = terraingen.NoiseLayer(params, warp_amplitude=20.0, warp_wavelength=300.0)
    dx, dy = terraingen.domain_warp2(10.0, 20.0, layer)
    assert abs(dx) <= 20.0 and abs(dy) <= 20.0
    assert -1.0 <= terraingen.evaluate_noise_layer(10.0, 20.0, layer) <= 1.0
    with pytest.raises(ValueError):
        terraingen.perlin_noise3(x, y[:2], z, 1)


def test_raster_io_and_processing(tmp_path):
    raster = terraingen.Raster.from_array(
        np.arange(64, dtype=np.float32).reshape(8, 8), terraingen.RasterGrid.centred(8, 8, 2.0).transform
    )
    keys = terraingen.geographic_sphere_keys(1737400.0)
    assert keys.is_geographic and not keys.empty and terraingen.GeoKeys().empty
    options = terraingen.GeoTiffWriteOptions(compression=terraingen.TiffCompression.DEFLATE, tile_size=16)
    terraingen.write_geotiff(tmp_path / "a.tif", raster, keys, options)
    info = terraingen.read_geotiff_info(tmp_path / "a.tif")
    assert (info.width, info.height, info.bands) == (8, 8, 1)
    assert info.compression == terraingen.TiffCompression.DEFLATE and info.tiled
    assert info.sample_format == terraingen.TiffSampleFormat.FLOAT and info.geo_keys == keys
    with pytest.raises(terraingen.GeoTiffError):
        terraingen.read_geotiff_info(tmp_path / "missing.tif")
    assert issubclass(terraingen.GeoTiffError, RuntimeError)

    blurred = terraingen.gaussian_blur(raster, 1.0)
    assert blurred.values.std() < raster.values.std()
    assert terraingen.gaussian_blur(raster, 1.0, 0.0).values[0, 0] == pytest.approx(blurred.values[0, 0], abs=5.0)
    finer = terraingen.resample(raster, 1.0, terraingen.ResampleMethod.BILINEAR)
    assert finer.shape == (16, 16)
    onto = terraingen.resample_to(raster, terraingen.RasterGrid.centred(4, 4, 2.0), terraingen.ResampleMethod.NEAREST)
    assert onto.shape == (4, 4)
    assert terraingen.sample_raster(raster, -7.0, 7.0, terraingen.ResampleMethod.NEAREST) == 0.0
    grid = terraingen.RasterGrid.centred(10, 20, 2.0)
    assert grid.area == pytest.approx(800.0) and grid.pixel_size == 2.0
    assert grid.surface_area(terraingen.Surface()) == pytest.approx(800.0) and grid.spacing(terraingen.Surface()) == 2.0
    assert grid.make_raster(3.0).values.mean() == 3.0


def test_cells_and_surface_helpers():
    planar = terraingen.PlanarCells()
    cells = planar.cells_covering((0.0, 0.0, 25.0, 15.0), 10.0)
    assert len(cells) == 6 and planar.cell_area(cells[0], 10.0) == pytest.approx(100.0)
    px, py = planar.sample_point(0.5, 0.5, terraingen.CellIndex(1, 2), 10.0)
    assert (px, py) == pytest.approx((15.0, 25.0))
    sphere = terraingen.CubeSphereCells(1737400.0)
    cell = sphere.cell_of(23.0, 1.0, 50000.0)
    assert cell in sphere.neighbours(cell, 50000.0, 1) and sphere.cells_per_edge(50000.0) > 10
    assert sphere.cell_area(cell, 50000.0) == pytest.approx(50000.0**2, rel=0.5)
    assert np.linalg.norm(terraingen.CubeSphereCells.direction(0, 0.0, 0.0)) == pytest.approx(1.0)
    moon = terraingen.body_sphere(terraingen.Body.MOON)
    east, north, up = moon.frame((0.0, 0.0))
    assert up == pytest.approx((1.0, 0.0, 0.0)) and north == pytest.approx((0.0, 0.0, 1.0))
    assert moon.point_of((0.0, 1.0, 0.0)) == pytest.approx((90.0, 0.0))
    assert moon.expand((0.0, 0.0, 1.0, 1.0), 30000.0)[0] < 0.0
    assert moon.contains((170.0, -5.0, 190.0, 5.0), (-175.0, 0.0))
    assert moon == terraingen.body_sphere(terraingen.Body.MOON) and moon != terraingen.Surface()
    assert terraingen.body_named("mars") == terraingen.Body.MARS and terraingen.body_named("pluto") is None


def test_meshes_and_scenes(tmp_path):
    cube = terraingen.base_solid_mesh(terraingen.BaseSolid.CUBE)
    assert cube.volume == pytest.approx(8.0) and cube.surface_area == pytest.approx(24.0)
    assert cube.bounds == ((-1.0, -1.0, -1.0), (1.0, 1.0, 1.0))
    positions, triangles = cube.arrays
    rebuilt = terraingen.TriangleMesh(positions, triangles.astype(np.uint32))
    assert rebuilt.volume == pytest.approx(8.0)
    sphere = terraingen.icosphere(3)
    assert np.allclose(np.linalg.norm(sphere.arrays[0], axis=1), 1.0)
    assert sphere.vertex_normals().shape == (sphere.vertex_count, 3)
    creased, normals = cube.with_creases(math.radians(30.0))
    assert creased.vertex_count == 24 and normals.shape == (24, 3)
    turn = terraingen.RigidTransform.rotation_z(math.pi / 2)
    assert turn.apply((1.0, 0.0, 0.0)) == pytest.approx((0.0, 1.0, 0.0), abs=1e-12)
    moved = terraingen.RigidTransform.translation_by((5.0, 0.0, 0.0)).after(turn)
    assert moved.apply((1.0, 0.0, 0.0)) == pytest.approx((5.0, 1.0, 0.0))
    assert cube.transformed(moved).bounds[0][0] == pytest.approx(4.0)
    assert cube.scaled((2.0, 1.0, 1.0)).volume == pytest.approx(16.0)
    assert cube.subdivided(1).triangle_count == 4 * cube.triangle_count
    cube.write_ply(tmp_path / "cube.ply", normals=cube.vertex_normals())
    terraingen.write_glb_scene(
        tmp_path / "scene.glb",
        [cube, sphere],
        [terraingen.SceneNode(0), terraingen.SceneNode(1, scale=(2.0, 2.0, 2.0), pose=moved)],
    )
    assert (tmp_path / "scene.glb").read_bytes()[:4] == b"glTF"


def test_crater_lists_generation_and_application():
    density = terraingen.lunar_surface(3.5).density
    ages = terraingen.FreshAges()
    options = terraingen.CraterGenerationOptions(min_diameter=8.0, max_diameter=100.0)
    region = (-200.0, -200.0, 200.0, 200.0)
    craters = terraingen.generate_craters(terraingen.PlanarCells(), region, density, ages, 3, options)
    assert len(craters) > 50 and craters[0].diameter >= 8.0
    again = terraingen.generate_craters(terraingen.PlanarCells(), (-200.0, -200.0, 0.0, 200.0), density, ages, 3, options)
    ids = set(craters.table["id"])
    assert set(again.table["id"]) <= ids
    copied = terraingen.CraterList.from_table(craters.table)
    assert len(copied) == len(craters) and copied[5].id == craters[5].id
    listed = terraingen.CraterList([terraingen.Crater(0.0, 0.0, 50.0, age_gyr=1.0)])
    listed.append(terraingen.Crater(80.0, 0.0, 30.0))
    listed.extend(copied)
    assert len(listed) == len(copied) + 2
    with pytest.raises(IndexError):
        listed[len(listed)]
    bands = terraingen.crater_bands(8.0, 100.0)
    assert bands[0].low == 8.0 and bands[-1].high == 100.0
    assert terraingen.min_diameter_for_count(density, 1e6, 100.0, 8.0, 1000.0) > 8.0
    assert terraingen.manual_crater_id(1.0, 2.0, 3.0) == terraingen.manual_crater_id(1.0, 2.0, 3.0)

    raster = terraingen.RasterGrid.centred(200, 200, 2.0).make_raster()
    terraingen.apply_craters(raster, craters, terraingen.FassettThomsonProfile(), terraingen.NoDegradation(), spacing=2.0)
    assert raster.values.min() < -5.0
    order = terraingen.crater_apply_order(craters, terraingen.CraterOrder.LARGEST_FIRST)
    assert craters[order[0]].diameter == max(c.diameter for c in [craters[i] for i in range(len(craters))])
    smoothed = terraingen.Raster.from_array(raster.values.copy(), raster.transform)
    terraingen.smooth_crater_rims(smoothed, craters, terraingen.RimSmoothing(sigma_radii=0.1), 2.0)
    assert smoothed.values.max() < raster.values.max()
    assert terraingen.crater_blend_weight(0.5, terraingen.CraterBlend()) == 1.0
    assert terraingen.resolution_weight(1.0, 1.0, terraingen.ResolutionFade()) == 0.0
    fresh = terraingen.lunar_fresh_crater_morphometry(100.0)
    attributes = terraingen.compute_crater_attributes(
        terraingen.Crater(0.0, 0.0, 100.0), terraingen.CraterState(fresh, fresh, 0.0), terraingen.FassettThomsonProfile()
    )
    assert attributes.depth_ratio == pytest.approx(fresh.depth_ratio, rel=0.05)
    spacing = terraingen.CraterSpacing(diameter_ratio=2.0, separation=1.0)
    a, b = terraingen.Crater(0.0, 0.0, 20.0, id=1), terraingen.Crater(10.0, 0.0, 20.0, id=2)
    a.manual = b.manual = False
    assert terraingen.craters_conflict(a, b, spacing) and terraingen.crater_takes_precedence(a, b)
    assert len(terraingen.thin_by_spacing(terraingen.CraterList([a, b]), spacing)) == 1
    assert terraingen.spacing_reach(20.0, 20.0, spacing) >= 20.0
    irregular = terraingen.CraterIrregularity(enabled=True, elongated_share=1.0, manual=True)
    outline = terraingen.outline_of(terraingen.Crater(0.0, 0.0, 50.0), irregular)
    assert outline.ellipticity > 1.0 and not outline.round and outline.max_scale >= 1.0
    assert terraingen.max_outline_scale(irregular) >= outline.max_scale
    datum = terraingen.fit_crater_datum(
        terraingen.Crater(0.0, 0.0, 50.0), terraingen.CraterApplyOptions(datum=terraingen.CraterDatum.PLANE), lambda x, y: 2.0 + 0.1 * x
    )
    assert datum == pytest.approx((2.0, 0.1, 0.0), abs=1e-6)
    assert terraingen.fit_crater_datum(terraingen.Crater(0.0, 0.0, 50.0), terraingen.CraterApplyOptions(), lambda x, y: 0.0) is None


def test_boulders_models_and_layers(lunar_models):
    craters = terraingen.CraterLayer(
        lunar_models(), terraingen.CraterLayerOptions(generation=terraingen.CraterGenerationOptions(min_diameter=3.0, max_diameter=20.0))
    )
    options = terraingen.BoulderLayerOptions(coverage=0.08, min_diameter=0.2, max_diameter=3.0, models=terraingen.BoulderModelThreshold(min_diameter=0.6))
    boulders = terraingen.BoulderLayer(options, craters)
    evaluated = terraingen.evaluate(terraingen.Terrain(terraingen.FlatSource(201, 201, 0.1), [craters, boulders]))
    assert len(evaluated.boulder_list) == len(evaluated.boulders["x"]) and len(evaluated.crater_list) == len(evaluated.craters["x"])
    boulder = evaluated.boulder_models[0]
    assert boulder.boulder.model and boulder.boulder.exposed_height < boulder.boulder.height
    placed = boulder.mesh
    assert placed.vertex_count == boulder.shape.mesh.vertex_count
    top = terraingen.top_surface(placed, boulder.boulder.x, boulder.boulder.y)
    assert top is not None and terraingen.top_surface(placed, 1e6, 1e6) is None
    again = terraingen.place_boulder_model(boulder.boulder, boulder.shape, boulder.boulder.ground)
    assert again.apply((0.0, 0.0, 0.0)) == pytest.approx(boulder.placement.apply((0.0, 0.0, 0.0)))
    mesh = terraingen.boulder_mesh(boulder.boulder, terraingen.BoulderShapeOptions(), 0.0)
    assert mesh.vertex_count > 0
    moon = terraingen.body_sphere(terraingen.Body.MOON)
    site = terraingen.placement_at_site(boulder.placement, (0.0, 0.0), (0.0, 0.0), moon)
    assert site.apply((0.0, 0.0, 0.0)) == pytest.approx(boulder.placement.apply((0.0, 0.0, 0.0)), abs=1e-6)
    boulder = terraingen.Boulder(0.0, 0.0, 1.0, 0.5, burial=0.2)
    assert terraingen.add_boulder(0.0, 0.0, 0.0, boulder) > 0.0 and boulder.exposed_height == pytest.approx(0.4)
    assert terraingen.boulder_throw_distance(100.0, 5.0, 1.62) > 0
    assert terraingen.boulder_candidates(options).cumulative(0.5) > 0
    assert boulders.density_at(0.5, 0.0, 0.0) > 0
    assert terraingen.perlin_structure_function(1.0) > terraingen.perlin_structure_function(0.1)
    assert terraingen.perlin3_structure_function(1.0) > 0
    rough = terraingen.RoughnessLayer(terraingen.RoughnessOptions())
    assert len(rough.wavelengths()) > 1
    region = terraingen.RegionLayer(terraingen.ScaleLayer(2.0), terraingen.RegionShape.circle(0.0, 0.0, 5.0))
    assert isinstance(region.inner, terraingen.ScaleLayer)
    doubled = terraingen.FunctionLayer(lambda x, y, spacing, height: height + x)
    heights = terraingen.evaluate(terraingen.Terrain(terraingen.FlatSource(8, 8, 1.0), [doubled]), threads=4).heights.values
    np.testing.assert_allclose(heights[0], np.arange(-3.5, 4.5))


def test_albedo_bodies_distributions_and_pipeline(tmp_path, lunar_models):
    assert terraingen.optical_maturity(0.08, 0.08 * 1.19) == pytest.approx(0.0, abs=1e-9)
    assert terraingen.reflectance_at_maturity(0.11, 0.2, 0.4) > 0.11
    assert terraingen.OpticalMaturityOrigin(ratio=1.2).ratio == 1.2
    assert terraingen.hsv_to_rgb(0.0, 1.0, 0.5) == pytest.approx((0.5, 0.0, 0.0))
    young = terraingen.Crater(0.0, 0.0, 250.0, age_gyr=0.002)
    craters = terraingen.CraterLayer(
        lunar_models(), terraingen.CraterLayerOptions(generate=False, generation=terraingen.CraterGenerationOptions(manual=[young]))
    )
    bright = terraingen.CraterAlbedo(craters)
    assert bright.excess(young, 0.0, 0.0, 8.0) > 0 and bright.reach(young) > 125.0
    impact = bright.impact(young)
    assert 0.0 <= impact.angle_degrees <= 90.0 and impact.speed_ratio >= 1.0
    albedo = terraingen.Albedo(terraingen.SyntheticAlbedo(value=0.11), [bright])
    grid = terraingen.RasterGrid.centred(9, 9, 8.0)
    band = terraingen.evaluate_albedo(albedo, grid)[0].values
    assert terraingen.evaluate_albedo_at(albedo, 0.0, 0.0, 8.0) == pytest.approx([band[4, 4]])

    assert terraingen.neukum_lunar_chronology_rate(3.8) > terraingen.neukum_lunar_chronology_rate(1.0) > 0
    assert terraingen.ivanov_mars_chronology_rate(3.0) > 0
    assert terraingen.lunar_fresh_crater_depth_ratio(100.0) == pytest.approx(terraingen.lunar_fresh_crater_morphometry(100.0).depth_ratio)
    assert terraingen.fassett_thomson_2014_diffusion_age(3.0) > 0
    assert terraingen.exponential_integral(1, 1.0) == pytest.approx(0.2193839, rel=1e-5)
    piecewise = terraingen.PiecewisePowerLawSfd([(0.0, 0.1, 2.0), (10.0, 1.0, 3.0)])
    assert [s.start for s in piecewise.segments] == [0.0, 10.0]
    table = terraingen.TabulatedSfd([1.0, 10.0], [1e-1, 1e-3])
    assert table.diameters == [1.0, 10.0] and table.cumulative_values == pytest.approx([1e-1, 1e-3])
    assert terraingen.LogPolynomialSfd([-2.0, -2.0], 1.0, 100.0).coefficients == [-2.0, -2.0]
    power = terraingen.PowerLawSfd(0.05, 2.0)
    assert terraingen.SummedSfd([(2.0, power)]).terms[0][0] == 2.0
    assert len(terraingen.MinimumSfd([power, terraingen.trask_equilibrium()]).distributions) == 2
    assert len(terraingen.SplicedSfd([(1.0, power)], 1.0, 100.0).pieces) == 1
    surface = terraingen.ProductionSurface(terraingen.extended_neukum_production_function(), 3.0)
    assert surface.production.kind == terraingen.SfdKind.PRODUCTION_RATE

    config = terraingen.Config.from_dict(
        {
            "schema_version": 1,
            "surface": {"type": "sphere", "body": "moon"},
            "source": {"type": "flat", "width": 17, "height": 17, "pixel_size": 100},
            "layers": [{"type": "noise", "stack": [{"wavelength": 2000, "amplitude": 10}]}],
            "albedo": {"source": {"type": "synthetic", "value": 0.12}},
        }
    )
    assert config.surface.is_sphere and config.grid().width == 17
    terrain = terraingen.build_terrain(config)
    assert terrain.surface.is_sphere and len(terrain.layers) == 1
    heights = terraingen.evaluate(terrain).heights.values
    np.testing.assert_array_equal(heights, terraingen.generate_terrain(config).heights.values)
    built_albedo = terraingen.build_albedo(config, terrain)
    assert terraingen.evaluate_albedo(built_albedo, config.grid())[0].values.mean() == pytest.approx(0.12)
    assert terraingen.filter_margin(terrain, 100.0) == 0.0
    assert terraingen.TileScheme.wrap_body_tile(terraingen.TileKey(0, -1, 0)) == terraingen.TileKey(0, 1, 0)


def test_backends_and_validators():
    statuses = terraingen.backends()
    assert [s.backend for s in statuses] == [terraingen.Backend.CPU, terraingen.Backend.VULKAN]
    assert statuses[0].available
    assert terraingen.backend_named("Vulkan") == terraingen.Backend.VULKAN and terraingen.backend_named("x") is None
    terraingen.set_default_backend(terraingen.Backend.CPU)
    assert terraingen.default_backend() == terraingen.Backend.CPU
    assert terraingen.resolve_backend(terraingen.Backend.CPU) == terraingen.Backend.CPU
    terraingen.set_default_backend(terraingen.Backend.AUTO)
    for validate, bad in [
        (terraingen.validate_resolution_fade, terraingen.ResolutionFade(start_samples=3.0, end_samples=1.0)),
        (terraingen.validate_crater_spacing, terraingen.CraterSpacing(diameter_ratio=0.5)),
        (terraingen.validate_boulder_layer_options, terraingen.BoulderLayerOptions(coverage=-1.0)),
        (terraingen.validate_rim_smoothing, terraingen.RimSmoothing(strength=2.0)),
        (terraingen.validate_crater_rays, terraingen.CraterRays(excess=-1.0)),
        (terraingen.validate_exposure_params, terraingen.ExposureParams(reach=-1.0)),
        (terraingen.validate_albedo_detail, terraingen.AlbedoDetail(octaves=0)),
    ]:
        with pytest.raises(ValueError):
            validate(bad)
    terraingen.validate_crater_apply_options(terraingen.CraterApplyOptions())
    terraingen.validate_boulder_shape_options(terraingen.BoulderShapeOptions())


def test_crater_selections_falloffs_and_secondary_laws():
    selection = terraingen.CraterSelection(min_diameter=10.0)
    assert selection.includes(terraingen.Crater(0.0, 0.0, 20.0))
    assert not selection.includes(terraingen.Crater(0.0, 0.0, 5.0))
    selection.manual = False
    assert not selection.includes(terraingen.Crater(0.0, 0.0, 20.0)), "craters made in Python are manual"
    with pytest.raises(ValueError):
        terraingen.validate_crater_selection(terraingen.CraterSelection(min_age_gyr=-1.0))
    secondaries = terraingen.SecondaryCraterOptions(min_primary_diameter=3000.0, max_age_gyr=0.5)
    assert secondaries.selection.min_diameter == 3000.0 and secondaries.selection.max_age_gyr == 0.5
    irregularity = terraingen.CraterIrregularity(max_diameter=50.0)
    assert irregularity.selection.max_diameter == 50.0 and not irregularity.manual
    albedo = terraingen.CraterAlbedoOptions(min_diameter=100.0)
    assert albedo.selection.min_diameter == 100.0

    assert terraingen.smoothstep(0.5) == 0.5 and terraingen.smoothstep(2.0) == 1.0
    assert terraingen.falloff(1.0, 0.8, 1.2, terraingen.BlendShape.LINEAR) == pytest.approx(0.5)
    blend = terraingen.CraterBlend(start_radii=0.8, end_radii=1.2, shape=terraingen.BlendShape.LINEAR)
    assert terraingen.crater_blend_weight(1.0, blend) == pytest.approx(0.5)

    largest = terraingen.singer_max_secondary_diameter(20000.0, 60000.0)
    assert terraingen.singer_range_for_size(20000.0, largest) == pytest.approx(60000.0)
    assert math.isinf(terraingen.singer_range_for_size(800.0, 10.0))
    assert terraingen.elliott_median_ray_length(2000.0) == pytest.approx(6590.0)

    state = terraingen.CraterState(terraingen.CraterMorphometry(0.2, 0.04), terraingen.CraterMorphometry(0.1, 0.02))
    assert state.freshness == pytest.approx(0.5)
    boulders = terraingen.BoulderLayerOptions(qa=1.5, bedrock_abundance=2.0, max_lean=0.1, shape_prototypes=8)
    assert (boulders.qa, boulders.bedrock_abundance, boulders.max_lean, boulders.shape_prototypes) == (1.5, 2.0, 0.1, 8)
