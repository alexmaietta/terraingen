# Concepts

How TerrainGen is put together: what a terrain is made of, the science behind its craters, boulders and albedo, and how results stay the same on any grid, tile or thread count.

## Building terrains

<div class="grid cards" markdown>

-   __Terrains, sources, layers and filters__

    ---

    Heights from a source, relief added by layers, then transformed by filters.

    [Read more](terrain.md)

-   __Configuration files__

    ---

    The JSON format, its schema, units and defaults.

    [Read more](configs.md)

-   __Surfaces, bodies and coordinates__

    ---

    Planes and spheres, the Moon, Mars and Mercury, and their coordinates.

    [Read more](surfaces.md)

-   __Tiles and levels of detail__

    ---

    Quadtree tiles that match each other at every level.

    [Read more](tiles.md)

-   __Seeds and reproducibility__

    ---

    Counter-based random numbers: the same terrain for the same seed, everywhere.

    [Read more](reproducibility.md)

</div>

## Surface features

<div class="grid cards" markdown>

-   __Size-frequency distributions__

    ---

    Crater and boulder densities, production functions and chronologies.

    [Read more](distributions.md)

-   __Crater populations and ages__

    ---

    Craters drawn from a distribution, with ages from a surface history.

    [Read more](craters.md)

-   __Crater profiles and degradation__

    ---

    Fresh crater shapes and how they soften with age.

    [Read more](crater-profiles.md)

-   __Boulders and boulder models__

    ---

    Boulder sizes and placement, and 3D boulder shapes.

    [Read more](boulders.md)

-   __Noise and synthetic relief__

    ---

    Layered fractal noise for the terrain between craters.

    [Read more](noise.md)

-   __Albedo__

    ---

    Surface brightness, from orbital maps to bright crater rays.

    [Read more](albedo.md)

</div>

## Data and running

<div class="grid cards" markdown>

-   __Rasters and GeoTIFF__

    ---

    Rasters, geotransforms and GeoTIFF reading and writing.

    [Read more](rasters.md)

-   __Outputs__

    ---

    Heightmaps, crater and boulder lists, albedo, meshes and glTF scenes.

    [Read more](outputs.md)

-   __GPU backends__

    ---

    Where evaluation runs, and the Vulkan compute backend.

    [Read more](gpu.md)

-   __Logging__

    ---

    Log levels, categories and sinks.

    [Read more](logging.md)

</div>
