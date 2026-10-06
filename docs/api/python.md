# Python API

The `terraingen` package wraps the C++ library with nanobind. Rasters share their cells with NumPy
without copying, and long-running functions release the GIL. Build the extension module first
(`pip install -e .`, or a CMake build in `build/`) so the reference can be generated.

```{eval-rst}
.. automodule:: terraingen
   :no-members:
```

## Synthetic terrain

```{eval-rst}
.. autoclass:: terraingen.SyntheticTerrainParams
.. autoclass:: terraingen.NoiseLayer
.. autoclass:: terraingen.FractalParams
.. autoclass:: terraingen.NoiseBasis
.. autoclass:: terraingen.FractalType
.. autoclass:: terraingen.LayerBlend
.. autodata:: terraingen.MAX_NOISE_LAYERS
.. autofunction:: terraingen.generate_synthetic_terrain
.. autofunction:: terraingen.fill_synthetic_terrain
.. autofunction:: terraingen.evaluate_noise_layers
.. autofunction:: terraingen.gain_from_hurst
.. autofunction:: terraingen.validate_synthetic_terrain
.. autofunction:: terraingen.validate_noise_layer
.. autofunction:: terraingen.validate_fractal_params
```

## Noise

```{eval-rst}
.. autofunction:: terraingen.fractal2
.. autofunction:: terraingen.basis_noise2
.. autofunction:: terraingen.perlin_noise2
.. autofunction:: terraingen.simplex_noise2
.. autofunction:: terraingen.value_noise2
.. autofunction:: terraingen.worley2
```

## Hashing and random numbers

```{eval-rst}
.. autofunction:: terraingen.pcg_hash
.. autofunction:: terraingen.pcg3d_hash
.. autofunction:: terraingen.philox4x32
.. autofunction:: terraingen.hash_to_unit_float
.. autoclass:: terraingen.RandomStream
.. autoclass:: terraingen.TabulatedDistribution
.. autofunction:: terraingen.sample_exponential
.. autofunction:: terraingen.sample_normal
.. autofunction:: terraingen.sample_poisson
.. autofunction:: terraingen.sample_truncated_power_law
```

## Rasters

```{eval-rst}
.. autoclass:: terraingen.Raster
.. autoclass:: terraingen.GeoTransform
.. autoclass:: terraingen.RasterStats
.. autofunction:: terraingen.compute_stats
```

## Logging

```{eval-rst}
.. autofunction:: terraingen.set_log_level
.. autofunction:: terraingen.get_log_level
.. autofunction:: terraingen.set_log_sink
.. autofunction:: terraingen.use_python_logging
```

## Version

```{eval-rst}
.. autofunction:: terraingen.version
.. autofunction:: terraingen.api_version
.. autofunction:: terraingen.build_info
.. autodata:: terraingen.__version__
```
