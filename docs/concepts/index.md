# Concepts

How TerrainGen is put together: what a terrain is made of, the science behind its craters, rocks and albedo, and how results stay the same on any grid, tile or thread count.

## Building terrains

<div class="grid cards" markdown>

-   :material-layers-triple:{ .lg .middle } __Terrains, sources, layers and filters__

    ---

    Heights from a source, relief added by layers, then transformed by filters.

    [:octicons-arrow-right-24: Read more](terrain.md)

-   :material-code-json:{ .lg .middle } __Configuration files__

    ---

    The JSON format, its schema, units and defaults.

    [:octicons-arrow-right-24: Read more](configs.md)

-   :material-earth:{ .lg .middle } __Surfaces, bodies and coordinates__

    ---

    Planes and spheres, the Moon, Mars and Mercury, and their coordinates.

    [:octicons-arrow-right-24: Read more](surfaces.md)

-   :material-grid:{ .lg .middle } __Tiles and levels of detail__

    ---

    Quadtree tiles that match each other at every level.

    [:octicons-arrow-right-24: Read more](tiles.md)

-   :material-dice-multiple:{ .lg .middle } __Seeds and reproducibility__

    ---

    Counter-based random numbers: the same terrain for the same seed, everywhere.

    [:octicons-arrow-right-24: Read more](reproducibility.md)

</div>

## Surface features

<div class="grid cards" markdown>

-   :material-chart-bell-curve:{ .lg .middle } __Size-frequency distributions__

    ---

    Crater and rock densities, production functions and chronologies.

    [:octicons-arrow-right-24: Read more](distributions.md)

-   :material-circle-double:{ .lg .middle } __Crater populations and ages__

    ---

    Craters drawn from a distribution, with ages from a surface history.

    [:octicons-arrow-right-24: Read more](craters.md)

-   :material-chart-line:{ .lg .middle } __Crater profiles and degradation__

    ---

    Fresh crater shapes and how they soften with age.

    [:octicons-arrow-right-24: Read more](crater-profiles.md)

-   :material-cube-outline:{ .lg .middle } __Rocks and boulders__

    ---

    Rock sizes and placement, and 3D boulder shapes.

    [:octicons-arrow-right-24: Read more](rocks.md)

-   :material-waves:{ .lg .middle } __Noise and synthetic relief__

    ---

    Layered fractal noise for the terrain between craters.

    [:octicons-arrow-right-24: Read more](noise.md)

-   :material-brightness-6:{ .lg .middle } __Albedo__

    ---

    Surface brightness, from orbital maps to bright crater rays.

    [:octicons-arrow-right-24: Read more](albedo.md)

</div>

## Data and running

<div class="grid cards" markdown>

-   :material-image-area:{ .lg .middle } __Rasters and GeoTIFF__

    ---

    Rasters, geotransforms and GeoTIFF reading and writing.

    [:octicons-arrow-right-24: Read more](rasters.md)

-   :material-export:{ .lg .middle } __Outputs__

    ---

    Heightmaps, crater and rock lists, albedo, meshes and glTF scenes.

    [:octicons-arrow-right-24: Read more](outputs.md)

-   :material-expansion-card:{ .lg .middle } __GPU backends__

    ---

    Where evaluation runs, and the Vulkan compute backend.

    [:octicons-arrow-right-24: Read more](gpu.md)

-   :material-text-box-outline:{ .lg .middle } __Logging__

    ---

    Log levels, categories and sinks.

    [:octicons-arrow-right-24: Read more](logging.md)

</div>
