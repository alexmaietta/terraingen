# SPDX-License-Identifier: BSD-3-Clause

"""Crater layer snippets for the documentation, run by tests/python/test_docs_snippets.py."""

import terraingen

# --8<-- [start:default_layer]
craters = terraingen.CraterLayer()
terrain = terraingen.TerrainGenerator(terraingen.FlatSource(257, 257, 1.0), [craters])
heights = terraingen.evaluate(terrain)
# --8<-- [end:default_layer]

# --8<-- [start:published_models]
surface = terraingen.EquilibriumSurface(
    terraingen.extended_neukum_production_function(),
    terraingen.trask_equilibrium(),
    3.5,
    terraingen.neukum_lunar_chronology,
)
ages = surface.ages()
models = terraingen.CraterModels(
    surface.density(),
    ages,
    terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
    terraingen.FassettThomsonProfile(),
)
options = terraingen.CraterLayerOptions(
    seed=7, generation=terraingen.CraterGenerationOptions(d_min=4.0, d_max=120.0)
)
craters = terraingen.CraterLayer(models, options)
# --8<-- [end:published_models]

terrain = terraingen.TerrainGenerator(terraingen.FlatSource(257, 257, 1.0), [craters])

# --8<-- [start:crater_list]
table = terraingen.placed_craters(terrain).table
print(f"{len(table['diameter'])} craters, the largest {table['diameter'].max():.0f} m across")
# --8<-- [end:crater_list]

assert heights.shape == (257, 257)
assert len(table["diameter"]) > 0
