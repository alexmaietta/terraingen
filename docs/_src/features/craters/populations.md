# Crater populations

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

![Cumulative crater density of a 3.5 Gyr lunar surface: the production function, the Trask equilibrium, their minimum, and the counts of the generated craters](../../images/gallery/craters/size_frequency.jpg)

<figcaption>Generated craters follow the density, the lower of production and equilibrium.</figcaption>

</figure>

??? example "Reproduce this figure"

    ```python
    --8<-- "docs/gallery/craters/size_frequency.py"
    ```
