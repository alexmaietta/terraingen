# Craters and boulders

A `CraterLayer` draws a crater population from a size-frequency distribution, gives each crater an
age, and cuts it into the terrain with a profile that degrades with that age.

## A crater layer

With no arguments, a crater layer takes the default lunar models: a 3.5 Gyr surface capped at the
Hartmann equilibrium, diffusion degradation matched to the crater ages, and the Martin, Parkes and
Dunstan profile, with irregular outlines.

=== "Python"

    ```python
    --8<-- "docs/snippets/craters/crater_layer.py:default_layer"
    ```

=== "C++"

    ```cpp
    --8<-- "docs/snippets/craters/crater_layer.cpp:default_layer"
    ```

## Published models

Each model can be chosen on its own. Here the density and ages come from one equilibrium surface: the
extended Neukum production function over 3.5 Gyr, capped by the Trask equilibrium.

=== "Python"

    ```python
    --8<-- "docs/snippets/craters/crater_layer.py:published_models"
    ```

=== "C++"

    ```cpp
    --8<-- "docs/snippets/craters/crater_layer.cpp:published_models"
    ```

Production outpaces equilibrium below a couple of hundred meters, so small craters stay at the
equilibrium density while large ones follow production.

<figure markdown>

![Cumulative crater density of a 3.5 Gyr lunar surface: the production function, the Trask equilibrium, their minimum, and the counts of the generated craters](../images/gallery/craters/size_frequency.jpg)

<figcaption>Generated craters follow the density, the lower of production and equilibrium.</figcaption>

</figure>

??? example "Reproduce this figure"

    ```python
    --8<-- "docs/gallery/craters/size_frequency.py"
    ```

## Degradation

The degradation model wears craters down with their age: not at all, shallower in proportion to age,
or by topographic diffusion.

<figure markdown>

![The same crater field under four degradation models: none, freshness, and diffusion with the Fassett and Thomson (2014) and Fassett et al. (2022) diffusivities](../images/gallery/craters/degradation.jpg)

<figcaption>The same craters under each degradation model.</figcaption>

</figure>

??? example "Reproduce this figure"

    ```python
    --8<-- "docs/gallery/craters/degradation.py"
    ```

## Heights and albedo

Crater layers also feed the albedo: ejecta and rays are bright when a crater is young and fade as it
ages.

<figure markdown>

![Craters on rolling terrain shown as heights in a colormap, beside the albedo map with the rays of a young crater at the center](../images/gallery/craters/heights_and_albedo.jpg)

<figcaption>Heights, and the albedo of the same craters with a young crater at the center.</figcaption>

</figure>

??? example "Reproduce this figure"

    ```python
    --8<-- "docs/gallery/craters/heights_and_albedo.py"
    ```

## The crater list

Every crater a layer places, with its position, diameter, depth and age, can be read back or written
to CSV or JSON.

=== "Python"

    ```python
    --8<-- "docs/snippets/craters/crater_layer.py:crater_list"
    ```

=== "C++"

    ```cpp
    --8<-- "docs/snippets/craters/crater_layer.cpp:crater_list"
    ```
