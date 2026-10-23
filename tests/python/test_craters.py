"""layers/craters: generation, crater lists, application, spacing, selection, outlines and secondaries."""

import math

import numpy as np
import pytest

import terraingen

REGION = (-200.0, -200.0, 200.0, 200.0)
ALL_CRATERS = terraingen.CraterSelection(manual=True)


def generate_lunar_craters(lunar_craters, region=REGION):
    """Fresh lunar craters 8 to 100 m across over `region`, seed 3."""
    density, _ = lunar_craters(3.5)
    options = terraingen.CraterGenerationOptions(d_min=8.0, d_max=100.0)
    return terraingen.generate_craters(
        terraingen.PlanarCells(), region, density, terraingen.FreshAges(), 3, options
    )


def carved_raster(craters, options=None):
    """A 200 x 200 raster at 2 m with `craters` applied with `options`."""
    raster = terraingen.RasterGrid.centered(200, 200, 2.0).make_raster()
    terraingen.apply_craters(
        raster,
        craters,
        terraingen.FassettThomsonProfile(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
        options=options or terraingen.CraterApplyOptions(),
        spacing=2.0,
    )
    return raster


def layer_craters(models, **generation):
    """The crater table of a seed-2 layer over `REGION` with extra generation options."""
    options = terraingen.CraterLayerOptions(
        seed=2,
        generation=terraingen.CraterGenerationOptions(d_min=10.0, d_max=100.0, **generation),
    )
    return terraingen.CraterLayer(models, options).craters(REGION)


def conflicting_pair():
    a, b = terraingen.Crater(0.0, 0.0, 20.0, id=1), terraingen.Crater(10.0, 0.0, 20.0, id=2)
    a.manual = b.manual = False
    return a, b


def test_generate_craters_respects_the_minimum_diameter(lunar_craters):
    craters = generate_lunar_craters(lunar_craters)
    assert len(craters) > 50 and craters[0].diameter >= 8.0


def test_generate_craters_is_stable_for_sub_regions(lunar_craters):
    craters = generate_lunar_craters(lunar_craters)
    again = generate_lunar_craters(lunar_craters, (-200.0, -200.0, 0.0, 200.0))
    assert set(again.table["id"]) <= set(craters.table["id"])


def test_crater_list_round_trips_through_a_table(lunar_craters):
    craters = generate_lunar_craters(lunar_craters)
    copied = terraingen.CraterList.from_table(craters.table)
    assert len(copied) == len(craters) and copied[5].id == craters[5].id


def test_crater_list_appends_and_extends(lunar_craters):
    copied = terraingen.CraterList.from_table(generate_lunar_craters(lunar_craters).table)
    listed = terraingen.CraterList([terraingen.Crater(0.0, 0.0, 50.0, age_gyr=1.0)])
    listed.append(terraingen.Crater(80.0, 0.0, 30.0))
    listed.extend(copied)
    assert len(listed) == len(copied) + 2


def test_crater_list_index_out_of_range_raises():
    listed = terraingen.CraterList([terraingen.Crater(0.0, 0.0, 50.0)])
    with pytest.raises(IndexError):
        listed[len(listed)]


def test_crater_fields_are_writable():
    crater = terraingen.Crater(1.0, 2.0, 3.0)
    crater.age_gyr = 0.5
    assert (crater.x, crater.y, crater.diameter, crater.age_gyr, crater.id) == (1.0, 2.0, 3.0, 0.5, 0)


def test_crater_takes_an_explicit_depth_and_rim_height():
    plain = terraingen.Crater(1.0, 2.0, 6.0)
    assert math.isnan(plain.depth) and math.isnan(plain.rim_height)
    assert "depth" not in repr(plain)
    crater = terraingen.Crater(1.0, 2.0, 6.0, depth=0.9, rim_height=0.2)
    assert (crater.depth, crater.rim_height) == (0.9, 0.2)
    assert "depth=0.9" in repr(crater) and "rim_height=0.2" in repr(crater)
    crater.rim_height = float("nan")
    assert math.isnan(crater.rim_height)
    listed = terraingen.CraterList([crater, plain])
    assert listed.depths[0] == 0.9 and math.isnan(listed.depths[1])
    assert math.isnan(listed.rim_heights[0])


def test_crater_list_reads_depth_and_rim_height_columns():
    table = {
        "x": [0.0, 5.0],
        "y": [0.0, 0.0],
        "diameter": [6.0, 8.0],
        "depth": [0.9, float("nan")],
        "rim_height": [0.2, float("nan")],
    }
    craters = terraingen.CraterList.from_table(table)
    assert (craters[0].depth, craters[0].rim_height) == (0.9, 0.2)
    assert math.isnan(craters[1].depth)
    attributes_only = terraingen.CraterList.from_table(
        {"x": [0.0], "y": [0.0], "diameter": [6.0], "rim_height": [0.3]}
    )
    assert math.isnan(attributes_only[0].rim_height), "a rim height without depths is an attribute"
    with pytest.raises(ValueError):
        terraingen.CraterList.from_table({"x": [0.0], "y": [0.0], "diameter": [6.0], "depth": [1.0, 2.0]})


def test_apply_craters_carves_an_explicit_depth():
    raster = terraingen.RasterGrid.centered(200, 200, 0.5).make_raster()
    craters = terraingen.CraterList(
        [terraingen.Crater(0.0, 0.0, 40.0, age_gyr=1.5, depth=6.0, rim_height=1.5)]
    )
    degradation = terraingen.FreshnessDegradation(
        terraingen.UniformAges(2.0), terraingen.lunar_fresh_crater_morphometry
    )
    terraingen.apply_craters(
        raster, craters, terraingen.MartinParkesDunstanProfile(), degradation, spacing=0.5
    )
    assert raster.values.max() == pytest.approx(1.5, abs=0.05)
    assert raster.values.max() - raster.values.min() == pytest.approx(6.0, abs=0.1)
    with pytest.raises(ValueError, match="Fassett-Thomson"):
        terraingen.apply_craters(
            raster, craters, terraingen.FassettThomsonProfile(), degradation, spacing=0.5
        )


def test_crater_bands_span_the_diameter_range():
    bands = terraingen.crater_bands(8.0, 100.0)
    assert bands[0].low == 8.0 and bands[-1].high == 100.0


def test_min_diameter_for_count_exceeds_the_lower_bound(lunar_craters):
    density, _ = lunar_craters(3.5)
    assert terraingen.count_limit_diameter(density, 1e6, 100.0, 8.0, 1000.0) > 8.0


def test_manual_crater_id_is_deterministic():
    assert terraingen.manual_crater_id(1.0, 2.0, 3.0) == terraingen.manual_crater_id(1.0, 2.0, 3.0)


def test_apply_craters_carves_the_raster(lunar_craters):
    assert carved_raster(generate_lunar_craters(lunar_craters)).values.min() < -5.0


def test_crater_apply_order_puts_the_largest_first(lunar_craters):
    craters = generate_lunar_craters(lunar_craters)
    order = terraingen.crater_apply_order(craters, terraingen.CraterOrder.LARGEST_FIRST)
    assert craters[order[0]].diameter == max(craters[i].diameter for i in range(len(craters)))


def test_rim_smoothing_lowers_the_highest_rim(lunar_craters):
    craters = generate_lunar_craters(lunar_craters)
    rounded = terraingen.CraterApplyOptions(rim_smoothing=terraingen.RimSmoothing(sigma_radii=0.1))
    assert carved_raster(craters, rounded).values.max() < carved_raster(craters).values.max()
    assert terraingen.RimSmoothing().weight(1.0) == 0.0
    assert rounded.rim_smoothing.weight(1.0) == 1.0


def test_crater_texture_changes_the_craters_only_when_set(lunar_craters):
    craters = generate_lunar_craters(lunar_craters)
    plain = carved_raster(craters).values
    off = terraingen.CraterApplyOptions(texture=terraingen.CraterTexture(seed=3))
    assert (carved_raster(craters, off).values == plain).all()
    texture = terraingen.CraterTexture(wall_streaks=0.01, ejecta=0.5, min_freshness=0.0)
    textured = terraingen.CraterApplyOptions(texture=texture)
    assert textured.texture.ejecta_reach_radii == 3.0
    assert (carved_raster(craters, textured).values != plain).any()
    assert terraingen.ejecta_thickness_share(1.0, 3.0) == 1.0
    with pytest.raises(ValueError):
        terraingen.validate_crater_texture(terraingen.CraterTexture(mantling=2.0))


def test_crater_noise_changes_the_craters_only_when_set(lunar_craters):
    craters = generate_lunar_craters(lunar_craters)
    plain = carved_raster(craters).values
    off = terraingen.CraterApplyOptions(noise=terraingen.CraterNoise(seed=3))
    assert (carved_raster(craters, off).values == plain).all()
    noisy = terraingen.CraterApplyOptions(
        noise=terraingen.CraterNoise(amplitude=0.02, rim_amplitude=0.02, octaves=3)
    )
    assert noisy.noise.octaves == 3
    assert (carved_raster(craters, noisy).values != plain).any()
    with pytest.raises(ValueError):
        terraingen.validate_crater_noise(terraingen.CraterNoise(octaves=0))


def test_crater_blend_weight_is_one_inside_the_blend():
    assert terraingen.CraterBlend().weight(0.5) == 1.0


def test_crater_blend_weight_falls_linearly_across_the_blend():
    blend = terraingen.CraterBlend(start_radii=0.8, end_radii=1.2, shape=terraingen.BlendShape.LINEAR)
    assert blend.weight(1.0) == pytest.approx(0.5)


def test_fresh_crater_attributes_keep_the_fresh_depth_ratio():
    fresh = terraingen.lunar_fresh_crater_morphometry(100.0)
    attributes = terraingen.compute_crater_attributes(
        terraingen.Crater(0.0, 0.0, 100.0),
        terraingen.CraterState(fresh, fresh, 0.0),
        terraingen.FassettThomsonProfile(),
    )
    assert attributes.depth_ratio == pytest.approx(fresh.depth_ratio, rel=0.05)


def test_overlapping_craters_conflict_and_the_first_takes_precedence():
    a, b = conflicting_pair()
    spacing = terraingen.CraterSpacing(diameter_ratio=2.0, separation=1.0)
    assert terraingen.craters_conflict(a, b, spacing) and terraingen.crater_takes_precedence(a, b)


def test_thin_by_spacing_drops_the_conflicting_crater():
    spacing = terraingen.CraterSpacing(diameter_ratio=2.0, separation=1.0)
    assert len(terraingen.thin_by_spacing(terraingen.CraterList(list(conflicting_pair())), spacing)) == 1


def test_spacing_reach_covers_the_diameter():
    spacing = terraingen.CraterSpacing(diameter_ratio=2.0, separation=1.0)
    assert terraingen.spacing_reach(20.0, 20.0, spacing) >= 20.0


def test_crater_spacing_thins_generated_craters(lunar_models):
    loose = layer_craters(lunar_models(), spacing=terraingen.CraterSpacing(separation=0.0))
    spaced = layer_craters(
        lunar_models(), spacing=terraingen.CraterSpacing(diameter_ratio=2.0, separation=1.0)
    )
    assert len(spaced["x"]) < len(loose["x"])


def test_max_craters_caps_generation(lunar_models):
    assert len(layer_craters(lunar_models(), max_craters=10)["x"]) == 10


def test_irregular_outlines_are_elongated():
    irregular = terraingen.CraterIrregularity(enabled=True, elongated_share=1.0, selection=ALL_CRATERS)
    outline = terraingen.irregular_outline(terraingen.Crater(0.0, 0.0, 50.0), irregular)
    assert outline.ellipticity > 1.0 and not outline.round and outline.max_scale >= 1.0


def test_marked_outlines_shift_only_the_wall():
    marked = terraingen.CraterIrregularity(
        enabled=True,
        selection=ALL_CRATERS,
        rough_share=0.0,
        elongated_share=0.0,
        marks_share=1.0,
        marks_knots=12,
    )
    assert (marked.marks_inner_radii, marked.marks_outer_radii, marked.marks_fade_radii) == (0.3, 0.9, 0.1)
    outline = terraingen.irregular_outline(terraingen.Crater(0.0, 0.0, 50.0, 0.0, 5), marked)
    assert len(outline.marks) == 12 and not outline.round
    assert outline.mark_shift(1.0, 0.0, 0.1) == 0.0 and outline.mark_shift(1.0, 0.0, 1.0) == 0.0
    assert abs(outline.mark_shift(1.0, 0.0, 0.6)) <= 1.25 * marked.marks_amplitude.b


def test_max_outline_scale_bounds_every_outline():
    irregular = terraingen.CraterIrregularity(enabled=True, elongated_share=1.0, selection=ALL_CRATERS)
    outline = terraingen.irregular_outline(terraingen.Crater(0.0, 0.0, 50.0), irregular)
    assert terraingen.max_outline_scale(irregular) >= outline.max_scale


def test_plane_datum_fits_a_tilted_ground():
    datum = terraingen.fit_crater_datum(
        terraingen.Crater(0.0, 0.0, 50.0),
        terraingen.CraterApplyOptions(datum=terraingen.CraterDatum.PLANE),
        lambda x, y: 2.0 + 0.1 * x,
    )
    assert datum == pytest.approx((2.0, 0.1, 0.0), abs=1e-6)


def test_default_datum_needs_no_fit():
    crater = terraingen.Crater(0.0, 0.0, 50.0)
    assert terraingen.fit_crater_datum(crater, terraingen.CraterApplyOptions(), lambda x, y: 0.0) is None


def test_crater_selection_filters_by_diameter():
    selection = terraingen.CraterSelection(min_diameter=10.0)
    assert selection.includes(terraingen.Crater(0.0, 0.0, 20.0))
    assert not selection.includes(terraingen.Crater(0.0, 0.0, 5.0))


def test_crater_selection_can_exclude_manual_craters():
    selection = terraingen.CraterSelection(min_diameter=10.0)
    selection.manual = False
    assert not selection.includes(terraingen.Crater(0.0, 0.0, 20.0)), "craters made in Python are manual"


def test_secondary_options_select_their_primaries():
    selection = terraingen.CraterSelection(min_diameter=3000.0, max_age_gyr=0.5)
    secondaries = terraingen.SecondaryCraterOptions(selection=selection)
    assert secondaries.selection.min_diameter == 3000.0 and secondaries.selection.max_age_gyr == 0.5


def test_irregularity_options_select_generated_craters_only():
    irregularity = terraingen.CraterIrregularity()
    irregularity.selection.max_diameter = 50.0
    assert irregularity.selection.max_diameter == 50.0 and not irregularity.selection.manual


def test_options_are_documented_and_listed_from_their_config_fields():
    assert "Unit: m." in terraingen.CraterSelection.min_diameter.__doc__
    noise = terraingen.CraterNoise(0.02, octaves=3)
    assert noise.amplitude == 0.02 and noise.octaves == 3
    assert repr(noise).startswith("CraterNoise(amplitude=0.02, ")
    with pytest.raises(TypeError):
        terraingen.CraterBlend(0.0, 1.0, terraingen.BlendShape.LINEAR, 4.0)
    with pytest.raises(ValueError):
        terraingen.CraterNoise(octaves=0).validate()
    assert "physical order" in terraingen.CraterOrder.OLDEST_FIRST.__doc__


def test_unbounded_selection_bounds_read_as_none():
    selection = terraingen.CraterSelection(max_diameter=50.0)
    assert selection.max_diameter == 50.0 and selection.max_age_gyr is None
    selection.max_diameter = None
    assert selection.max_diameter is None
    assert selection.includes(terraingen.Crater(0.0, 0.0, 1e6))


def test_singer_range_for_size_inverts_the_largest_secondary():
    largest = terraingen.singer_max_secondary_diameter(20000.0, 60000.0)
    assert terraingen.singer_max_range(20000.0, largest) == pytest.approx(60000.0)


def test_singer_range_is_infinite_for_secondaries_too_small_to_reach():
    assert math.isinf(terraingen.singer_max_range(800.0, 10.0))


def test_singer_largest_secondary_of_a_small_primary():
    assert terraingen.singer_max_secondary_diameter(2000.0, 5000.0) > 20.0


def test_elliott_median_ray_length():
    assert terraingen.elliott_median_ray_length(2000.0) == pytest.approx(6590.0)


def test_secondaries_fall_beyond_the_onset_range(lunar_models):
    primary = terraingen.CraterLayer(
        lunar_models(),
        terraingen.CraterLayerOptions(
            generate_random=False,
            generation=terraingen.CraterGenerationOptions(
                manual=[terraingen.Crater(0.0, 0.0, 2000.0, age_gyr=0.01)]
            ),
        ),
    )
    options = terraingen.SecondaryCraterOptions(seed=4, onset_count=20.0, min_diameter_m=20.0)
    secondaries = terraingen.SecondaryCraterLayer(
        primary,
        terraingen.FassettThomsonProfile(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry, depth_ratio=0.11),
        options,
    )
    outer = secondaries.outer_range(2000.0)
    table = secondaries.secondaries((-outer, -outer, outer, outer))
    ranges = np.hypot(table["x"], table["y"])
    assert len(ranges) > 50 and ranges.min() >= terraingen.secondary_onset_range(2000.0)
    layout = terraingen.RayLayout(seed=2)
    rayed = terraingen.SecondaryCraterOptions(
        seed=4,
        onset_count=20.0,
        min_diameter_m=20.0,
        rays=terraingen.SecondaryRays(enabled=True, layout=layout),
        shapes=terraingen.SecondaryShapes(elongated_share=1.0),
    )
    terraingen.validate_secondary_crater_options(rayed)
    assert rayed.rays.layout.seed == 2 and rayed.shapes.ellipticity.b == pytest.approx(1.8)
    directions = terraingen.ray_directions(terraingen.Crater(0.0, 0.0, 2000.0), layout)
    assert len(directions) >= 6 and all(0.0 <= d.azimuth_deg < 360.0 for d in directions)
    bright = terraingen.CraterRays(layout=layout)
    assert bright.layout.seed == 2
    with pytest.raises(ValueError):
        terraingen.validate_ray_layout(terraingen.RayLayout(width_deg=terraingen.ValueDistribution(a=200.0)))


def full_apply_options():
    return terraingen.CraterApplyOptions(
        order=terraingen.CraterOrder.LARGEST_FIRST,
        datum=terraingen.CraterDatum.MEAN_HEIGHT,
        interior_replacement=0.5,
        blend=terraingen.CraterBlend(start_radii=0.7, end_radii=1.3, shape=terraingen.BlendShape.LINEAR),
        fade_px=(1.0, 2.0),
        irregularity=terraingen.CraterIrregularity(
            enabled=True,
            roughness=terraingen.ValueDistribution(terraingen.ValueDistribution.Kind.UNIFORM, 0.02, 0.05),
            elongated_share=0.5,
        ),
    )


def test_crater_apply_options_keep_nested_settings():
    apply = full_apply_options()
    assert apply.irregularity.roughness.b == 0.05 and apply.blend.shape == terraingen.BlendShape.LINEAR


def test_crater_apply_options_give_finite_heights(lunar_models, crater_layer, noise_relief):
    layer = crater_layer(lunar_models(), 10.0, 100.0, apply=full_apply_options())
    noise = noise_relief(1, 200.0, 5.0)
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(129, 129, 2.0), [noise, layer])
    assert np.isfinite(terraingen.evaluate(terrain).values).all()


def test_crater_apply_options_reject_unknown_fields():
    with pytest.raises(AttributeError):
        terraingen.CraterApplyOptions(bogus=1)


def test_validate_crater_spacing_rejects_a_ratio_below_one():
    with pytest.raises(ValueError):
        terraingen.validate_crater_spacing(terraingen.CraterSpacing(diameter_ratio=0.5))


def test_validate_crater_selection_rejects_negative_ages():
    with pytest.raises(ValueError):
        terraingen.validate_crater_selection(terraingen.CraterSelection(min_age_gyr=-1.0))


def test_validate_crater_apply_options_accepts_the_defaults():
    terraingen.validate_crater_apply_options(terraingen.CraterApplyOptions())


def lunar_crater_terrain(lunar_models, crater_layer, noise_relief):
    craters = crater_layer(lunar_models(), 6.0, 60.0, seed=3)
    terrain = terraingen.TerrainGenerator(
        terraingen.FlatSource(129, 129, 2.0), [noise_relief(1, 200.0, 10.0, octaves=5), craters]
    )
    return craters, terrain


def test_crater_layer_generates_craters_in_its_diameter_range(lunar_models, crater_layer, noise_relief):
    _, terrain = lunar_crater_terrain(lunar_models, crater_layer, noise_relief)
    table = terraingen.placed_craters(terrain).table
    assert len(table["x"]) > 10
    assert table["diameter"].min() >= 6.0 and table["diameter"].max() < 60.0


def test_crater_layer_evaluates_the_same_on_one_thread(lunar_models, crater_layer, noise_relief):
    _, terrain = lunar_crater_terrain(lunar_models, crater_layer, noise_relief)
    heights = terraingen.evaluate(terrain).values
    assert np.array_equal(terraingen.evaluate(terrain, threads=1).values, heights)


def test_terrain_keeps_its_crater_layer(lunar_models, crater_layer, noise_relief):
    craters, terrain = lunar_crater_terrain(lunar_models, crater_layer, noise_relief)
    assert terrain.layers[1] is craters


def test_a_manual_crater_is_carved_alone():
    models = terraingen.CraterModels(
        terraingen.PowerLawSfd(1e-6, 2.0),
        terraingen.UniformAges(3.5),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
        terraingen.MartinParkesDunstanProfile(),
    )
    manual = terraingen.CraterLayer(
        models,
        terraingen.CraterLayerOptions(
            generate_random=False,
            generation=terraingen.CraterGenerationOptions(
                manual=[terraingen.Crater(0.0, 0.0, 60.0, age_gyr=1.0)]
            ),
        ),
    )
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(65, 65, 2.0), [manual])
    table = terraingen.placed_craters(terrain).table
    assert list(table["diameter"]) == [60.0]
    assert table["manual"][0] == 1 and table["depth_ratio"][0] > 0.1
    assert terraingen.evaluate(terrain).values[32, 32] < 0
    built = terraingen.CraterLayer.manual(models, [terraingen.Crater(0.0, 0.0, 60.0, age_gyr=1.0)])
    assert not built.options.generate_random and terraingen.CraterLayerOptions().generate_random
    rebuilt = terraingen.TerrainGenerator(terraingen.FlatSource(65, 65, 2.0), [built])
    assert np.array_equal(terraingen.evaluate(rebuilt).values, terraingen.evaluate(terrain).values)


def test_crater_layer_accepts_fresh_ages_and_a_plateau_profile(crater_layer):
    fresh = terraingen.CraterModels(
        terraingen.PowerLawSfd(1e-3, 2.0),
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry, depth_ratio=0.1),
        terraingen.FassettThomsonProfile(terraingen.FassettThomsonRim.PLATEAU),
    )
    assert crater_layer(fresh, 4.0, 20.0) is not None


def test_slope_response_needs_the_plane_datum():
    slope = terraingen.CraterSlopeResponse(enabled=True, upslope_rim=0.2)
    assert slope.floor_offset.b == pytest.approx(0.194)
    with pytest.raises(ValueError):
        terraingen.validate_crater_apply_options(terraingen.CraterApplyOptions(slope=slope))
    terraingen.validate_crater_apply_options(
        terraingen.CraterApplyOptions(datum=terraingen.CraterDatum.PLANE, slope=slope)
    )


def test_crater_density_maps_scale_the_count():
    count = terraingen.DensityMap(
        field=terraingen.ConstantField(3.0),
        measure=terraingen.DensityMeasure.COUNT,
        min_diameter=4.0,
        area_unit=1e6,
    )
    law = terraingen.PowerLawSfd(0.05, 2.0)
    assert terraingen.map_measure(count, law) == pytest.approx(0.05 / 16.0)
    assert terraingen.DensityScale(count, law).at(0.0, 0.0) == pytest.approx(3e-6 / (0.05 / 16.0))
    options = terraingen.CraterLayerOptions(
        density_map=terraingen.DensityMap(field=terraingen.ConstantField(2.0))
    )
    assert options.density_map.measure == terraingen.DensityMeasure.SCALE
    with pytest.raises(ValueError):
        terraingen.validate_density_map(terraingen.DensityMap(measure=terraingen.DensityMeasure.COUNT))


def test_slopes_wear_craters_faster_and_large_craters_can_outrank_smaller_ones():
    slope = terraingen.CraterSlopeResponse(critical_gradient=1.0)
    assert terraingen.slope_wear_rate(slope, 0.6, 0.0) == pytest.approx(1.0 / 0.64)
    young = terraingen.Crater(0.0, 0.0, 9.0, 0.1, 9)
    old = terraingen.Crater(0.0, 0.0, 11.0, 2.0, 1)
    assert terraingen.crater_takes_precedence(young, old)
    assert terraingen.crater_takes_precedence(old, young, outrank_from=10.0)
    fresh = terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry)
    assert fresh.worn_state(young, 3.0).current.depth_ratio == fresh.state(young).current.depth_ratio


def test_outline_heading_is_in_degrees():
    irregular = terraingen.CraterIrregularity(
        enabled=True, elongated_share=1.0, selection=ALL_CRATERS, heading_deg=30.0, heading_spread_deg=0.0
    )
    outline = terraingen.irregular_outline(terraingen.Crater(0.0, 0.0, 50.0), irregular)
    assert outline.heading_deg == pytest.approx(30.0)
