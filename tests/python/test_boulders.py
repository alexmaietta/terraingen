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


def evaluated_boulders(lunar_models, crater_layer, size=201):
    """Craters 3 to 20 m and boulders with models above 0.6 m on a `size` square grid at 0.1 m."""
    craters = crater_layer(lunar_models(), 3.0, 20.0)
    boulders = terraingen.BoulderLayer(model_options(), craters)
    return terraingen.evaluate(
        terraingen.Terrain(terraingen.FlatSource(size, size, 0.1), [craters, boulders])
    )


@pytest.fixture
def first_model(lunar_models, crater_layer):
    return evaluated_boulders(lunar_models, crater_layer).boulder_models[0]


def test_boulder_and_crater_tables_match_their_lists(lunar_models, crater_layer):
    evaluated = evaluated_boulders(lunar_models, crater_layer)
    assert len(evaluated.boulder_list) == len(evaluated.boulders["x"])
    assert len(evaluated.crater_list) == len(evaluated.craters["x"])


def test_modelled_boulders_are_partly_buried(first_model):
    assert first_model.boulder.model and first_model.boulder.exposed_height < first_model.boulder.height


def test_placed_boulder_mesh_keeps_the_shape_vertices(first_model):
    assert first_model.mesh.vertex_count == first_model.shape.mesh.vertex_count


def test_top_surface_is_found_above_the_boulder_only(first_model):
    placed = first_model.mesh
    assert terraingen.top_surface(placed, first_model.boulder.x, first_model.boulder.y) is not None
    assert terraingen.top_surface(placed, 1e6, 1e6) is None


def test_place_boulder_model_reproduces_the_placement(first_model):
    again = terraingen.place_boulder_model(first_model.boulder, first_model.shape, first_model.boulder.ground)
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
    evaluated = evaluated_boulders(lunar_models, crater_layer, 301)
    assert len(evaluated.boulders["x"]) > 50 and evaluated.boulder_model_count > 0
    assert evaluated.boulders["model"].sum() >= evaluated.boulder_model_count


def test_evaluated_lists_and_models_are_written(tmp_path, lunar_models, crater_layer):
    evaluated = evaluated_boulders(lunar_models, crater_layer, 301)
    evaluated.write_crater_list(tmp_path / "craters.csv")
    evaluated.write_crater_list(tmp_path / "craters.json")
    evaluated.write_boulder_list(tmp_path / "boulders.csv")
    evaluated.write_boulder_list(tmp_path / "boulders.json")
    evaluated.write_boulder_models(tmp_path / "boulders.glb")
    assert (tmp_path / "craters.csv").read_text().startswith("x,y,diameter")
    assert (tmp_path / "boulders.json").read_text().startswith("{")
    assert (tmp_path / "boulders.glb").read_bytes()[:4] == b"glTF"


def test_boulder_layer_options_keep_their_fields():
    boulders = terraingen.BoulderLayerOptions(qa=1.5, bedrock_abundance=2.0, max_lean=0.1, shape_prototypes=8)
    assert (boulders.qa, boulders.bedrock_abundance, boulders.max_lean, boulders.shape_prototypes) == (
        1.5,
        2.0,
        0.1,
        8,
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
