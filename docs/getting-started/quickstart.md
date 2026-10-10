# Quickstart

A 1 km square of cratered lunar terrain: rolling fractal relief with craters from 8 to 300 m at the density and ages of a 3.5 Gyr old surface. Each version writes the heights as a GeoTIFF and the craters as a CSV, and finds the same 1365 craters.

## Install

=== "Python"

    ```sh
    pip install terraingen
    ```

    The wheel includes the Python package, the `terraingen` command and the example configurations.

=== "C++"

    Download a release archive or Debian package, or build and install from source:

    ```sh
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    cmake --install build --prefix /path/to/prefix
    ```

    Then configure your own project with `-DCMAKE_PREFIX_PATH=/path/to/prefix` so `find_package(terraingen)` finds it.

## Generate a terrain

=== "Python"

    ```python
    import terraingen

    # Rolling fBm relief on a 513 x 513 grid of 2 m samples.
    fbm = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=800.0, amplitude=40.0))
    relief = terraingen.NoiseRelief([fbm])

    # Craters from 8 to 300 m with the density and ages of a 3.5 Gyr lunar surface.
    surface = terraingen.lunar_surface(3.5)
    models = terraingen.CraterModels(
        density=surface.density,
        ages=surface.ages,
        degradation=terraingen.FreshnessDegradation(surface.ages),
        profile=terraingen.FassettThomsonProfile(),
    )
    options = terraingen.CraterLayerOptions(
        seed=1, generation=terraingen.CraterGenerationOptions(min_diameter=8.0, max_diameter=300.0)
    )
    craters = terraingen.CraterLayer(models, options)

    terrain = terraingen.Terrain(terraingen.FlatSource(513, 513, 2.0), [relief, craters])
    evaluated = terraingen.evaluate(terrain)

    print(len(evaluated.craters["diameter"]), "craters")
    terraingen.write_geotiff("terrain.tif", evaluated.heights)
    evaluated.write_crater_list("craters.csv")
    ```

=== "C++"

    ```cpp
    #include "terraingen/terraingen.h"

    #include <iostream>
    #include <memory>

    int main() {
        // Rolling fBm relief on a 513 x 513 grid of 2 m samples.
        terraingen::NoiseLayer fbm;
        fbm.fractal.seed = 1;
        fbm.fractal.wavelength = 800.0f;
        fbm.fractal.amplitude = 40.0f;

        // Craters from 8 to 300 m with the density and ages of a 3.5 Gyr lunar surface.
        const terraingen::SurfaceModel surface = terraingen::LunarSurface(3.5);
        const terraingen::CraterModels models{
            surface.density, surface.ages,
            std::make_shared<terraingen::FreshnessDegradation>(surface.ages,
                                                               terraingen::LunarFreshCraterMorphometry),
            std::make_shared<terraingen::FassettThomsonProfile>()};
        terraingen::CraterLayerOptions options;
        options.seed = 1;
        options.generation.dMin = 8.0;
        options.generation.dMax = 300.0;

        terraingen::Terrain terrain;
        terrain.source = std::make_shared<terraingen::FlatSource>(513, 513, 2.0);
        terrain.layers.push_back(
            std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}));
        terrain.layers.push_back(std::make_shared<terraingen::CraterLayer>(models, options));

        const terraingen::EvaluatedTerrain evaluated = terraingen::Evaluate(terrain);
        std::cout << evaluated.features.craters.Size() << " craters\n";
        terraingen::WriteGeoTiff("terrain.tif", evaluated.heights);
        terraingen::WriteCraterList(evaluated.features.craters, "craters.csv",
                                    terraingen::CraterListFormat::Csv);
    }
    ```

    Build it against the installed package with CMake:

    ```cmake
    cmake_minimum_required(VERSION 3.22)
    project(my_terrain LANGUAGES CXX)

    find_package(terraingen REQUIRED)

    add_executable(my_terrain main.cpp)
    target_link_libraries(my_terrain PRIVATE terraingen::terraingen)
    ```

=== "Command line"

    The same terrain from a JSON configuration, one of the examples:

    ```sh
    terraingen examples/config/craters/example_craters_simple.json --output-dir output
    ```

    Add `--gui` to open it in the 3D GUI, in builds that have one.

## Next steps

<div class="grid cards" markdown>

-   :material-layers-triple:{ .lg .middle } __How a terrain is built__

    ---

    Sources, layers and filters, and how they combine.

    [:octicons-arrow-right-24: Concepts](../concepts/terrain.md)

-   :material-code-json:{ .lg .middle } __Configuration files__

    ---

    Describe a whole terrain in JSON and run it with `terraingen`.

    [:octicons-arrow-right-24: Configs](../concepts/configs.md)

-   :material-puzzle:{ .lg .middle } __Extend it__

    ---

    Your own sources, layers and crater models, in C++ or Python.

    [:octicons-arrow-right-24: Extending](../extending/index.md)

</div>
