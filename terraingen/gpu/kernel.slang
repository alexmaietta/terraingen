// What every TerrainGen compute kernel binds: the grid strip's heights, its column and row
// positions and the push constants of RunKernel (terraingen/gpu/kernel.cpp). Kernel sources
// include this, bind their kernel struct at 3 and any extra buffers from 4 on.

[[vk::binding(0, 0)]] RWStructuredBuffer<float> heights;
[[vk::binding(1, 0)]] StructuredBuffer<float> xs;
[[vk::binding(2, 0)]] StructuredBuffer<float> ys;

struct Params {
    uint width;
    uint rows;
    uint noData;
    float noDataValue;
    uint firstRow;
};
[[vk::push_constant]] ConstantBuffer<Params> params;

namespace terraingen {

static const uint kNoDataNone = 0u;
static const uint kNoDataNan = 1u;

/// Whether the thread `id` falls inside the strip.
bool InStrip(uint3 id) {
    return id.x < params.width && id.y < params.rows;
}

/// Whether `height` is the grid's no-data value, which kernels leave as it is.
bool IsNoData(float height) {
    if (params.noData == kNoDataNan) {
        return isnan(height);
    }
    return params.noData != kNoDataNone && height == params.noDataValue;
}

}  // namespace terraingen
