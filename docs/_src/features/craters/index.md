# Craters

A `CraterLayer` draws a crater population from a size-frequency distribution, gives each crater an age, and cuts it into the terrain with a profile that degrades with that age.

## A crater layer

With no arguments, a crater layer takes the default lunar models: a 3.5 Gyr surface capped at the Hartmann equilibrium, diffusion degradation matched to the crater ages, and the Martin, Parkes and Dunstan profile, with irregular outlines.

=== "Python"

    ```python
    --8<-- "docs/snippets/craters/crater_layer.py:default_layer"
    ```

=== "C++"

    ```cpp
    --8<-- "docs/snippets/craters/crater_layer.cpp:default_layer"
    ```
