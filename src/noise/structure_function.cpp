// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/noise/structure_function.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace terraingen {

namespace {

/// `PerlinStructureFunction(r) / r^2` at r = 2^(k / 4) for k from `kFirstQuarter`, estimated from
/// 2^26 pairs each.
constexpr int kFirstQuarter = -24;
constexpr std::array<double, 33> kStructureOverSquare{
    0.772366, 0.772241, 0.772129, 0.771929, 0.771444, 0.770975, 0.770202, 0.769434, 0.767832,
    0.766070, 0.763032, 0.759311, 0.753675, 0.745950, 0.735267, 0.720564, 0.700391, 0.673043,
    0.636947, 0.589660, 0.530574, 0.459215, 0.377706, 0.291690, 0.209524, 0.141536, 0.094237,
    0.065236, 0.046153, 0.032397, 0.022947, 0.016231, 0.011443};
constexpr double kLargeLag = 0.183120;

/// `Perlin3StructureFunction(r) / r^2` on the same lags, estimated from 2^24 pairs each.
constexpr std::array<double, 33> kStructure3OverSquare{
    0.494082, 0.494230, 0.494034, 0.493864, 0.493805, 0.493342, 0.493074, 0.492376, 0.491455,
    0.490546, 0.488813, 0.486484, 0.483336, 0.478718, 0.472398, 0.463827, 0.452007, 0.435827,
    0.414506, 0.386695, 0.351648, 0.308521, 0.258949, 0.205715, 0.153652, 0.108458, 0.074433,
    0.051596, 0.036333, 0.025663, 0.018156, 0.012839, 0.009067};
constexpr double kLargeLag3 = 0.145198;

/// A structure function at lag `r` from its `table` of value over r^2, `large` beyond the table.
double Interpolated(const std::array<double, 33>& table, double large, double r) {
    if (!(r > 0.0)) {
        return 0.0;
    }
    const double quarters = 4.0 * std::log2(r) - kFirstQuarter;
    if (quarters <= 0.0) {
        return table.front() * r * r;
    }
    const auto last = static_cast<double>(table.size() - 1);
    if (quarters >= last) {
        return large;
    }
    const auto k = static_cast<std::size_t>(quarters);
    const double t = quarters - static_cast<double>(k);
    return ((1.0 - t) * table[k] + t * table[k + 1]) * r * r;
}

}  // namespace

double PerlinStructureFunction(double r) {
    return Interpolated(kStructureOverSquare, kLargeLag, r);
}

double Perlin3StructureFunction(double r) {
    return Interpolated(kStructure3OverSquare, kLargeLag3, r);
}

}  // namespace terraingen
