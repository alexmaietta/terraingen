terraingen_add_library(
    layers
    SOURCES
    anchored_plane.cpp
    bedforms.cpp
    density_map.cpp
    dunes.cpp
    exposure.cpp
    fade.cpp
    ground.cpp
    layer.cpp
    material.cpp
    noise_relief.cpp
    masked.cpp
    pavement.cpp
    roughness.cpp
    roughness_estimate.cpp
    sand_fill.cpp
    sand_shadow.cpp
    source_resolved.cpp
    spectral_continuation.cpp
    strata.cpp
    sample_block.cpp
    transforms.cpp
    boulders/abundance.cpp
    boulders/boulder_abundance.cpp
    boulders/boulder_clusters.cpp
    boulders/ejecta_index.cpp
    boulders/boulder_layer.cpp
    boulders/boulder_models.cpp
    boulders/boulder_options.cpp
    boulders/boulder_fragments.cpp
    boulders/boulder_shape.cpp
    craters/crater.cpp
    craters/crater_layer.cpp
    craters/crater_placer.cpp
    craters/rays.cpp
    craters/secondaries.cpp
    craters/secondary_model.cpp
    craters/ages/age_model.cpp
    craters/ages/freshness.cpp
    craters/ages/steady_state.cpp
    craters/degradation/degradation_model.cpp
    craters/degradation/diffusion.cpp
    craters/degradation/diffusivities.cpp
    craters/degradation/freshness.cpp
    craters/generation/cell_cache.cpp
    craters/generation/population.cpp
    craters/generation/resolved_limit.cpp
    craters/generation/spacing.cpp
    craters/profiles/complex.cpp
    craters/profiles/fassett_thomson.cpp
    craters/profiles/martin_parkes_dunstan.cpp
    craters/shaping/apply.cpp
    craters/shaping/apply_options.cpp
    craters/shaping/crater_kernel.cpp
    craters/shaping/attributes.cpp
    craters/shaping/datum.cpp
    craters/shaping/explicit_state.cpp
    craters/shaping/irregularity.cpp
    craters/shaping/morphometry.cpp
    craters/shaping/prepared.cpp
    craters/shaping/slope.cpp
    PUBLIC
    terraingen_distribution
    terraingen_gpu
    terraingen_mesh
    terraingen_noise
    terraingen_random
    terraingen_raster
    terraingen_sources
    terraingen_util)
terraingen_add_kernel(terraingen_layers terraingen_kernel_noise_relief NoiseReliefKernel
                      terraingen/layers/kernels/noise_relief.h kernels/noise_relief.slang)
terraingen_add_kernel(terraingen_layers terraingen_kernel_craters CraterShapeKernel
                      terraingen/layers/kernels/craters.h kernels/craters.slang)
terraingen_add_kernel(terraingen_layers terraingen_kernel_crater_datums CraterDatumKernel
                      terraingen/layers/kernels/craters.h kernels/crater_datums.slang)
terraingen_add_kernel(terraingen_layers terraingen_kernel_crater_body_datums CraterBodyDatumKernel
                      terraingen/layers/kernels/craters.h kernels/crater_body_datums.slang)
