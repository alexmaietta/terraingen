"""TerrainGen: scientific terrain generation for the lunar surface.

TerrainGen's log records go to the standard logging module on loggers named
"terraingen.<category>", such as "terraingen.craters". TerrainGen itself only produces records at
or above set_log_level(), which defaults to "warning"; lower it to see timing statements.
"""

import atexit
import logging as _logging

from ._terraingen import (
    MAX_NOISE_LAYERS,
    FractalParams,
    FractalType,
    GeoTransform,
    LayerBlend,
    NoiseBasis,
    NoiseLayer,
    RandomStream,
    Raster,
    RasterStats,
    SyntheticTerrainParams,
    TabulatedDistribution,
    __version__,
    api_version,
    basis_noise2,
    build_info,
    compute_stats,
    evaluate_noise_layers,
    fill_synthetic_terrain,
    fractal2,
    gain_from_hurst,
    generate_synthetic_terrain,
    get_log_level,
    hash_to_unit_float,
    pcg3d_hash,
    pcg_hash,
    perlin_noise2,
    philox4x32,
    sample_exponential,
    sample_normal,
    sample_poisson,
    sample_truncated_power_law,
    set_log_level,
    set_log_sink,
    simplex_noise2,
    validate_fractal_params,
    validate_noise_layer,
    validate_synthetic_terrain,
    value_noise2,
    version,
    worley2,
)

__all__ = [
    "MAX_NOISE_LAYERS",
    "FractalParams",
    "FractalType",
    "GeoTransform",
    "LayerBlend",
    "NoiseBasis",
    "NoiseLayer",
    "RandomStream",
    "Raster",
    "RasterStats",
    "SyntheticTerrainParams",
    "TabulatedDistribution",
    "__version__",
    "api_version",
    "basis_noise2",
    "build_info",
    "compute_stats",
    "evaluate_noise_layers",
    "fill_synthetic_terrain",
    "fractal2",
    "gain_from_hurst",
    "generate_synthetic_terrain",
    "get_log_level",
    "hash_to_unit_float",
    "pcg3d_hash",
    "pcg_hash",
    "perlin_noise2",
    "philox4x32",
    "sample_exponential",
    "sample_normal",
    "sample_poisson",
    "sample_truncated_power_law",
    "set_log_level",
    "set_log_sink",
    "simplex_noise2",
    "use_python_logging",
    "validate_fractal_params",
    "validate_noise_layer",
    "validate_synthetic_terrain",
    "value_noise2",
    "version",
    "worley2",
]

_PYTHON_LEVELS = {
    "trace": 5,
    "debug": _logging.DEBUG,
    "info": _logging.INFO,
    "warning": _logging.WARNING,
    "error": _logging.ERROR,
}


def _forward_to_logging(level, category, message):
    _logging.getLogger(f"terraingen.{category}").log(_PYTHON_LEVELS[level], message)


def use_python_logging():
    """Send TerrainGen's log records to the logging module (done on import)."""
    set_log_sink(_forward_to_logging)


use_python_logging()
atexit.register(set_log_sink, None)
