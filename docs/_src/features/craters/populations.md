# Crater populations

Each model can be chosen on its own. Here the density and ages come from one equilibrium surface: the extended Neukum production function over 3.5 Gyr, capped by the Trask equilibrium.

=== "Python"

    ```python
    --8<-- "docs/snippets/craters/crater_layer.py:published_models"
    ```

=== "C++"

    ```cpp
    --8<-- "docs/snippets/craters/crater_layer.cpp:published_models"
    ```

Production outpaces equilibrium below a couple of hundred meters, so small craters stay at the equilibrium density while large ones follow production.
