# Extending TerrainGen

Every part of a terrain is a base class you can subclass, in C++ or Python: bring your own data sources, relief, crater science or albedo, and use them alongside the built-in ones.

## Terrain

<div class="grid cards" markdown>

-   :material-database-import:{ .lg .middle } __Custom sources__

    ---

    Heights from your own data or functions.

    [:octicons-arrow-right-24: Read more](custom-sources.md)

-   :material-layers-plus:{ .lg .middle } __Custom layers__

    ---

    Relief of your own on top of a source.

    [:octicons-arrow-right-24: Read more](custom-layers.md)

-   :material-filter-variant:{ .lg .middle } __Custom filters__

    ---

    Transforms of the heights after every layer.

    [:octicons-arrow-right-24: Read more](custom-filters.md)

</div>

## Craters

<div class="grid cards" markdown>

-   :material-chart-bell-curve:{ .lg .middle } __Custom size-frequency distributions__

    ---

    Crater and rock densities of your own.

    [:octicons-arrow-right-24: Read more](custom-distributions.md)

-   :material-timer-sand:{ .lg .middle } __Custom crater age models__

    ---

    How old each crater is.

    [:octicons-arrow-right-24: Read more](custom-age-models.md)

-   :material-chart-line:{ .lg .middle } __Custom crater profiles__

    ---

    Crater shapes of your own.

    [:octicons-arrow-right-24: Read more](custom-crater-profiles.md)

-   :material-weather-windy:{ .lg .middle } __Custom degradation models__

    ---

    How craters change with age.

    [:octicons-arrow-right-24: Read more](custom-degradation.md)

</div>

## Albedo and Python

<div class="grid cards" markdown>

-   :material-brightness-6:{ .lg .middle } __Custom albedo layers__

    ---

    Surface brightness of your own.

    [:octicons-arrow-right-24: Read more](custom-albedo.md)

-   :material-language-python:{ .lg .middle } __Writing models in Python__

    ---

    Subclass any base class in Python, and what it costs.

    [:octicons-arrow-right-24: Read more](python.md)

</div>
