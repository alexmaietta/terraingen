"""layers/boulders: boulder options, placement, models, abundance and writers."""

import numpy as np
import pytest

import terraingen


def model_options(max_diameter=3.0):
    return terraingen.BoulderLayerOptions(
        coverage=0.08,
        min_diameter=0.2,
        max_diameter=max_diameter,
        models=terraingen.BoulderModelThreshold(min_diameter=0.6),
    )


def boulder_terrain(lunar_models, crater_layer, size=201):
    """Craters 3 to 20 m and boulders with models above 0.6 m on a `size` square grid at 0.1 m."""
    craters = crater_layer(lunar_models(), 3.0, 20.0)
    boulders = terraingen.BoulderLayer(model_options(), craters)
    return terraingen.Terrain(terraingen.FlatSource(size, size, 0.1), [craters, boulders])


@pytest.fixture
def first_model(lunar_models, crater_layer):
    return terraingen.placed_boulders(boulder_terrain(lunar_models, crater_layer)).models[0]


def test_boulder_and_crater_tables_match_their_lists(lunar_models, crater_layer):
    terrain = boulder_terrain(lunar_models, crater_layer)
    boulders = terraingen.placed_boulders(terrain)
    craters = terraingen.placed_craters(terrain)
    assert len(boulders.boulders) == len(boulders.table["x"])
    assert len(craters.craters) == len(craters.table["x"])


def test_modelled_boulders_are_partly_buried(first_model):
    assert first_model.boulder.model and first_model.boulder.exposed_height < first_model.boulder.height


def test_placed_boulder_mesh_keeps_the_shape_vertices(first_model):
    assert first_model.mesh.vertex_count == first_model.shape.mesh.vertex_count


def test_placed_boulder_mesh_is_its_placement_applied(first_model):
    placed = first_model.placement.place_mesh(first_model.shape.mesh).arrays[0]
    np.testing.assert_array_equal(first_model.mesh.arrays[0], placed)
    shape = first_model.shape.mesh.arrays[0]
    assert placed[0] == pytest.approx(first_model.placement.apply(tuple(shape[0])), abs=1e-12)


def test_top_surface_is_found_above_the_boulder_only(first_model):
    placed = first_model.mesh
    assert terraingen.top_surface(placed, first_model.boulder.x, first_model.boulder.y) is not None
    assert terraingen.top_surface(placed, 1e6, 1e6) is None


def test_place_boulder_model_reproduces_the_placement(first_model):
    again = terraingen.place_boulder_model(
        first_model.boulder, first_model.shape, first_model.boulder.ground, settle=True
    )
    assert again.apply((0.0, 0.0, 0.0)) == pytest.approx(first_model.placement.apply((0.0, 0.0, 0.0)))


def test_boulder_mesh_has_vertices(first_model):
    mesh = terraingen.boulder_mesh(first_model.boulder, terraingen.BoulderShapeOptions(), 0.0)
    assert mesh.vertex_count > 0


def test_placement_at_the_site_origin_is_unchanged(first_model):
    moon = terraingen.Surface.sphere(1737400.0)
    site = terraingen.placement_at_site(first_model.placement, (0.0, 0.0), (0.0, 0.0), moon)
    assert site.apply((0.0, 0.0, 0.0)) == pytest.approx(
        first_model.placement.apply((0.0, 0.0, 0.0)), abs=1e-6
    )


def test_add_boulder_raises_the_ground_by_the_exposed_height():
    boulder = terraingen.Boulder(0.0, 0.0, 1.0, 0.5, burial=0.2)
    assert terraingen.add_boulder(0.0, 0.0, 0.0, boulder) > 0.0 and boulder.exposed_height == pytest.approx(
        0.4
    )


def test_boulder_throw_distance_is_positive():
    assert terraingen.boulder_throw_distance(100.0, 5.0, 1.62) > 0


def test_boulder_candidates_cover_the_diameter_range():
    assert terraingen.boulder_candidates(model_options()).cumulative(0.5) > 0


def test_boulder_layer_density_is_positive(lunar_models, crater_layer):
    craters = crater_layer(lunar_models(), 3.0, 20.0)
    assert terraingen.BoulderLayer(model_options(), craters).density_at(0.5, 0.0, 0.0) > 0


def test_boulder_layer_places_boulders_with_models(lunar_models, crater_layer):
    placed = terraingen.placed_boulders(boulder_terrain(lunar_models, crater_layer, 301))
    assert len(placed.table["x"]) > 50 and len(placed.models) > 0
    assert placed.table["model"].sum() >= len(placed.models)


def test_placed_lists_and_models_are_written(tmp_path, lunar_models, crater_layer):
    terrain = boulder_terrain(lunar_models, crater_layer, 301)
    craters = terraingen.placed_craters(terrain)
    boulders = terraingen.placed_boulders(terrain)
    craters.write_list(tmp_path / "craters.csv")
    craters.write_list(tmp_path / "craters.json")
    boulders.write_list(tmp_path / "boulders.csv")
    boulders.write_list(tmp_path / "boulders.json")
    boulders.write_models(tmp_path / "boulders.glb", crease_angle_deg=45.0)
    assert (tmp_path / "craters.csv").read_text().startswith("x,y,diameter")
    assert (tmp_path / "boulders.json").read_text().startswith("{")
    assert (tmp_path / "boulders.glb").read_bytes()[:4] == b"glTF"


def test_boulder_layer_options_keep_their_fields():
    boulders = terraingen.BoulderLayerOptions(
        qa=1.5, bedrock_abundance=2.0, max_lean_deg=6.0, shape_prototypes=8
    )
    assert (boulders.qa, boulders.bedrock_abundance, boulders.max_lean_deg, boulders.shape_prototypes) == (
        1.5,
        2.0,
        6.0,
        8,
    )


def test_boulder_fade_sets_the_smallest_boulder_drawn_in_pixels():
    fade = terraingen.ResolutionFade(start_samples=4.0, end_samples=6.0)
    options = terraingen.BoulderLayerOptions(coverage=0.1, min_diameter=0.2, max_diameter=3.0, fade=fade)
    layer = terraingen.BoulderLayer(options)
    region = (-15.0, -15.0, 15.0, 15.0)
    every = layer.boulders(region)["diameter"]
    drawn = layer.boulders(region, 0.25)["diameter"]
    assert every.min() < 1.0 and drawn.min() >= 1.0
    assert len(drawn) == np.count_nonzero(every >= 1.0)
    assert "pixels" in terraingen.BoulderLayerOptions.fade.__doc__


def test_boulder_fillets_and_burial_by_wear_are_options():
    fillet = terraingen.BoulderFillet(height_share=0.4, width_ratio=0.25)
    boulders = terraingen.BoulderLayerOptions(burial_by_wear=0.5, fillet=fillet)
    assert (boulders.burial_by_wear, boulders.fillet.height_share) == (0.5, 0.4)
    boulder = terraingen.Boulder(0.0, 0.0, 2.0, 1.0, burial=0.25)
    assert terraingen.boulder_fillet_height(boulder, 1.25, fillet) == pytest.approx(0.075)
    assert terraingen.boulder_fillet_height(boulder, 2.0, fillet) == 0.0
    with pytest.raises(ValueError):
        terraingen.validate_boulder_fillet(terraingen.BoulderFillet(height_share=2.0))


def test_worn_boulders_break_into_pieces_that_replace_them():
    options = terraingen.BoulderLayerOptions(coverage=0.1, min_diameter=0.3, max_diameter=2.0)
    whole = terraingen.BoulderLayer(options)
    options.fragmentation = terraingen.BoulderFragmentation(min_diameter=0.3, split_wear=0.0)
    broken = terraingen.BoulderLayer(options)
    assert options.fragmentation.enabled
    region = (-20.0, -20.0, 20.0, 20.0)
    assert len(broken.boulders(region)["x"]) > len(whole.boulders(region)["x"])
    piece = terraingen.Boulder(0.0, 0.0, 1.0, 0.5)
    piece.parent, piece.piece = 7, 2
    assert (piece.parent, piece.piece, piece.wear) == (7, 2, 0.0)
    with pytest.raises(ValueError):
        terraingen.validate_boulder_fragmentation(terraingen.BoulderFragmentation(min_pieces=1))


def test_boulder_clusters_raise_the_abundance_near_their_centres():
    clusters = terraingen.BoulderClusters(share=0.8, spacing=40.0, radius=4.0)
    layer = terraingen.BoulderLayer(terraingen.BoulderLayerOptions(clusters=clusters))
    abundances = [layer.abundance_at(x + 0.5, y + 0.5)[0] for x in range(0, 200, 2) for y in range(0, 200, 2)]
    assert max(abundances) > 5.0 > 0.25 > min(abundances)
    assert max(abundances) <= terraingen.max_cluster_abundance(clusters) + 1e-9
    with pytest.raises(ValueError):
        terraingen.validate_boulder_clusters(terraingen.BoulderClusters(radius=60.0))


def test_thomas_boulder_clusters_may_overlap():
    clusters = terraingen.BoulderClusters(
        share=1.0, spacing=10.0, radius=3.0, model=terraingen.BoulderClusterModel.THOMAS
    )
    layer = terraingen.BoulderLayer(terraingen.BoulderLayerOptions(clusters=clusters))
    abundances = [layer.abundance_at(x + 0.5, y + 0.5)[0] for x in range(0, 200, 2) for y in range(0, 200, 2)]
    assert max(abundances) > 3.0 > 0.4 > min(abundances)
    assert max(abundances) <= terraingen.max_cluster_abundance(clusters) + 1e-9
    terraingen.validate_boulder_clusters(
        terraingen.BoulderClusters(spacing=10.0, radius=15.0, model=terraingen.BoulderClusterModel.THOMAS)
    )


def test_boulder_layer_options_take_custom_shape_styles():
    egg = terraingen.BoulderStyle(
        name="egg",
        rounding=terraingen.ValueRange(1.0, 1.0),
        noise_amplitude=0.0,
        intermediate=terraingen.ValueRange(0.8, 0.8),
    )
    boulders = terraingen.BoulderLayerOptions(
        shapes=terraingen.BoulderShapeOptions(styles=[egg]), shape_prototypes=8
    )
    assert boulders.shapes.styles[0].name == "egg" and boulders.shape_prototypes == 8


def unburied_options():
    return terraingen.BoulderLayerOptions(
        coverage=0.08,
        min_diameter=0.2,
        max_diameter=2.0,
        distribution=terraingen.synthterrain_boulder_distribution(terraingen.SynthterrainBoulders.HAWORTH),
        patches=terraingen.BoulderPatches(spread=1.0),
        min_burial=0.0,
        max_burial=0.0,
        relief=terraingen.BoulderRelief.DOME,
        models=terraingen.BoulderModelThreshold(min_diameter=1.0),
        exposure=terraingen.ExposureParams(reach=6.0),
    )


def test_boulder_options_keep_patches_and_exposure():
    options = unburied_options()
    assert options.patches.spread == 1.0 and options.exposure.reach == 6.0


def test_unburied_boulders_sit_on_the_ground():
    options = unburied_options()
    options.exposure = None
    boulders = terraingen.BoulderLayer(options).boulders((-10.0, -10.0, 10.0, 10.0))
    assert len(boulders["x"]) > 0 and (boulders["burial"] == 0.0).all()


def test_boulder_abundance_is_uniform_without_exposure():
    options = unburied_options()
    options.exposure = None
    assert terraingen.BoulderLayer(options).abundance_at(0.0, 0.0) == (1.0, 0.0)


def test_boulder_abundance_follows_exposed_bedrock(noise_relief):
    exposure = terraingen.ExposureParams(reach=8.0)
    boulders = terraingen.BoulderLayer(
        terraingen.BoulderLayerOptions(exposure=exposure, bedrock_abundance=4.0)
    )
    ground = terraingen.Terrain(terraingen.FlatSource(64, 64, 0.5), [noise_relief(3, 60.0, 5.0)])
    abundances = [boulders.abundance_at(x, 0.0, ground, 1)[0] for x in np.linspace(-15.0, 15.0, 31)]
    assert min(abundances) < max(abundances) <= 4.0 + 1e-9


def test_boulder_ejecta_defaults_to_freshness_aging():
    ejecta = terraingen.BoulderEjecta(
        min_crater_diameter=5.0, coverage=0.3, profile=terraingen.EjectaProfile.EXPONENTIAL
    )
    assert (
        ejecta.profile == terraingen.EjectaProfile.EXPONENTIAL
        and ejecta.aging == terraingen.EjectaAging.FRESHNESS
    )


def test_validate_boulder_layer_options_rejects_negative_coverage():
    with pytest.raises(ValueError):
        terraingen.validate_boulder_layer_options(terraingen.BoulderLayerOptions(coverage=-1.0))


def test_boulders_rest_on_sides_and_ends_and_tilt():
    options = terraingen.BoulderLayerOptions(coverage=0.08, side_share=0.3, end_share=0.1, max_tilt_deg=45.0)
    table = terraingen.BoulderLayer(options).boulders((-30.0, -30.0, 30.0, 30.0))
    rests = np.asarray(table["rest"])
    assert set(np.unique(rests)) <= {0, 1, 2} and (rests == 1).any() and (rests == 2).any()
    assert np.asarray(table["tilt_deg"]).max() <= 45.0 + 1e-9
    boulder = terraingen.Boulder(0.0, 0.0, 1.0, 0.5)
    boulder.rest = terraingen.BoulderRest.END
    assert boulder.rest == terraingen.BoulderRest.END


def test_boulder_max_slope_is_optional():
    options = terraingen.BoulderLayerOptions(max_slope_deg=30.0)
    assert options.max_slope_deg == 30.0 and terraingen.BoulderLayerOptions().max_slope_deg is None


def test_boulder_layers_take_maps_and_catalogs():
    options = terraingen.BoulderLayerOptions(
        density_map=terraingen.DensityMap(field=terraingen.ConstantField(2.0)),
        manual=[terraingen.ManualBoulder(1.0, 2.0, 5.0)],
        completeness=terraingen.ConstantField(4.0),
    )
    layer = terraingen.BoulderLayer(options)
    assert layer.abundance_at(0.5, 0.5)[0] == pytest.approx(2.0)
    assert options.manual[0].diameter == 5.0 and options.completeness.value_at(0.0, 0.0) == 4.0


def test_patches_raise_the_mean_a_map_divides_out():
    assert terraingen.mean_patch_abundance(terraingen.BoulderPatches(spread=4.0, size=20.0)) > 1.2
    assert terraingen.BoulderLayerOptions(bedrock_mean_size=250.0).bedrock_mean_size == 250.0


def test_boulder_layer_reads_the_ground_at_its_ground_spacing():
    assert terraingen.BoulderLayerOptions().ground_spacing == 1.0
    assert terraingen.BoulderLayerOptions(ground_spacing=0.5).ground_spacing == 0.5
    assert not hasattr(terraingen.BoulderLayerOptions(), "exposure_spacing")
    with pytest.raises(ValueError):
        terraingen.validate_boulder_layer_options(terraingen.BoulderLayerOptions(ground_spacing=0.0))


def test_boulders_follow_a_size_slope_map_and_roll_down_steep_ground():
    options = terraingen.BoulderLayerOptions(
        distribution=terraingen.PowerLawSfd(0.05, 3.0),
        slope_map=terraingen.SizeSlopeMap(field=terraingen.ConstantField(2.0), reference_diameter=1.0),
        max_slope_deg=22.0,
        roll_distance=20.0,
    )
    layer = terraingen.BoulderLayer(options)
    assert layer.density_at(2.0, 0.0, 0.0) == pytest.approx(0.05 * 2.0 * 2.0**-3.0)
    assert terraingen.size_slope_at(options.slope_map, 0.0, 0.0) == 2.0
    assert options.roll_distance == 20.0


def test_boulder_angles_are_in_degrees():
    boulder = terraingen.Boulder(0.0, 0.0, 1.0, 0.5, yaw_deg=30.0, lean_deg=10.0)
    boulder.tilt_deg, boulder.tilt_azimuth_deg = 20.0, 90.0
    assert (boulder.yaw_deg, boulder.lean_deg, boulder.tilt_deg, boulder.tilt_azimuth_deg) == (
        30.0,
        10.0,
        20.0,
        90.0,
    )


def test_manual_boulders_fix_their_pose_and_shape():
    plain = terraingen.ManualBoulder(1.0, 2.0, 1.5)
    assert plain.height is None and plain.yaw_deg is None and plain.rest is None
    assert plain.shape_seed is None and "ManualBoulder(x=1.0" in repr(plain)
    posed = terraingen.ManualBoulder(
        x=-3.0,
        y=4.0,
        diameter=2.0,
        id=17,
        height=0.9,
        burial=0.3,
        yaw_deg=30.0,
        lean_deg=10.0,
        tilt_deg=20.0,
        tilt_azimuth_deg=90.0,
        wear=0.25,
        rest=terraingen.BoulderRest.SIDE,
        shape_seed=5,
    )
    assert (posed.yaw_deg, posed.lean_deg, posed.tilt_deg, posed.tilt_azimuth_deg) == (
        30.0,
        10.0,
        20.0,
        90.0,
    )
    posed.validate()
    assert terraingen.manual_boulder_id(posed) == 17
    assert terraingen.manual_boulder_id(plain) == terraingen.manual_crater_id(1.0, 2.0, 1.5)
    table = terraingen.BoulderLayer(
        terraingen.BoulderLayerOptions(min_diameter=0.5, coverage=0.01, manual=[plain, posed])
    ).boulders((-10.0, -10.0, 10.0, 10.0))
    row = list(np.asarray(table["id"])).index(17)
    assert np.asarray(table["height"])[row] == 0.9 and np.asarray(table["burial"])[row] == 0.3
    assert np.asarray(table["wear"])[row] == 0.25 and np.asarray(table["rest"])[row] == 1
    with pytest.raises(ValueError):
        terraingen.ManualBoulder(0.0, 0.0, 1.0, burial=1.0).validate()
    with pytest.raises(ValueError):
        terraingen.BoulderLayer(terraingen.BoulderLayerOptions(manual=[posed, posed]))
