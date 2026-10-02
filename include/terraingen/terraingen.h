#pragma once

/// The whole TerrainGen library.

#include "terraingen/core/build_info.h"
#include "terraingen/core/log.h"
#include "terraingen/core/version.h"
#include "terraingen/craters.h"
#include "terraingen/distributions.h"
#include "terraingen/kernels/fractal.h"
#include "terraingen/kernels/hash.h"
#include "terraingen/kernels/layers.h"
#include "terraingen/kernels/perlin.h"
#include "terraingen/kernels/philox.h"
#include "terraingen/kernels/simplex.h"
#include "terraingen/kernels/value.h"
#include "terraingen/kernels/worley.h"
#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/geo_transform.h"
#include "terraingen/raster/raster.h"
#include "terraingen/raster/raster_stats.h"
#include "terraingen/spatial/cells.h"
#include "terraingen/surfaces/synthetic_terrain.h"
