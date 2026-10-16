/// @file
/// @brief Points split into a lattice cell and a fraction, and the frozen lattice hash keys.
#pragma once

#include "terraingen/noise/hash.h"

namespace terraingen {

/// A 2D point as its integer lattice cell and its fraction within the cell.
struct LatticePoint2 {
    int32_t ix = 0;   ///< Cell x.
    int32_t iy = 0;   ///< Cell y.
    float fx = 0.0f;  ///< Fraction in x, in [0, 1).
    float fy = 0.0f;  ///< Fraction in y, in [0, 1).
};

/// A 3D point as its integer lattice cell and its fraction within the cell.
struct LatticePoint3 {
    int32_t ix = 0;   ///< Cell x.
    int32_t iy = 0;   ///< Cell y.
    int32_t iz = 0;   ///< Cell z.
    float fx = 0.0f;  ///< Fraction in x, in [0, 1).
    float fy = 0.0f;  ///< Fraction in y, in [0, 1).
    float fz = 0.0f;  ///< Fraction in z, in [0, 1).
};

/// The lattice cell and fraction of (`x`, `y`) in lattice units, split at floor(x) and floor(y).
inline LatticePoint2 SplitLattice2(float x, float y) {
    const float cellX = floor(x);
    const float cellY = floor(y);
    const LatticePoint2 point = {int32_t(cellX), int32_t(cellY), x - cellX, y - cellY};
    return point;
}

/// The lattice cell and fraction of (`x`, `y`, `z`) in lattice units.
inline LatticePoint3 SplitLattice3(float x, float y, float z) {
    const float cellX = floor(x);
    const float cellY = floor(y);
    const float cellZ = floor(z);
    const LatticePoint3 point = {int32_t(cellX), int32_t(cellY), int32_t(cellZ),
                                 x - cellX,      y - cellY,      z - cellZ};
    return point;
}

#ifndef __SLANG__
/// `SplitLattice2` in double precision: the fraction is found before narrowing, so it
/// stays exact far from the origin.
inline LatticePoint2 SplitLatticePrecise2(double x, double y) {
    const double cellX = floor(x);
    const double cellY = floor(y);
    return {int32_t(static_cast<std::int64_t>(cellX)), int32_t(static_cast<std::int64_t>(cellY)),
            float(x - cellX), float(y - cellY)};
}

/// `SplitLattice3` in double precision, as `SplitLatticePrecise2`.
inline LatticePoint3 SplitLatticePrecise3(double x, double y, double z) {
    const double cellX = floor(x);
    const double cellY = floor(y);
    const double cellZ = floor(z);
    return {int32_t(static_cast<std::int64_t>(cellX)),
            int32_t(static_cast<std::int64_t>(cellY)),
            int32_t(static_cast<std::int64_t>(cellZ)),
            float(x - cellX),
            float(y - cellY),
            float(z - cellZ)};
}
#endif

namespace detail {

/// The `pcg3d` key of 2D lattice point (`x`, `y`): the seed is the third word. Frozen for
/// reproducibility: Perlin, value and Worley noise in 2D and simplex noise hash this key.
/// @param x Lattice x, wrapped to 32 bits.
/// @param y Lattice y, wrapped to 32 bits.
constexpr UVec3 SeededLatticeKey2(uint32_t x, uint32_t y, uint32_t seed) {
    const UVec3 key = {x, y, seed};
    return key;
}

/// The `pcg3d` key of a corner of 3D Perlin noise: the hashed seed is added to z. Frozen
/// for reproducibility.
/// @param x Lattice x, wrapped to 32 bits.
/// @param y Lattice y, wrapped to 32 bits.
/// @param z Lattice z, wrapped to 32 bits.
constexpr UVec3 PerlinLatticeKey3(uint32_t x, uint32_t y, uint32_t z, uint32_t seed) {
    const UVec3 key = {x, y, z + PcgHash(seed)};
    return key;
}

/// The `pcg3d` key of a corner of 3D value noise: `seedKey`, the hashed seed
/// (`PcgHash(seed)`), is XORed into z. Frozen for reproducibility.
/// @param x Lattice x, wrapped to 32 bits.
/// @param y Lattice y, wrapped to 32 bits.
/// @param z Lattice z, wrapped to 32 bits.
/// @param seedKey `PcgHash` of the noise seed.
constexpr UVec3 ValueLatticeKey3(uint32_t x, uint32_t y, uint32_t z, uint32_t seedKey) {
    const UVec3 key = {x, y, z ^ seedKey};
    return key;
}

/// The `pcg3d` key of a cell of 3D Worley noise: `seedKey`, the hashed seed, is XORed into x so
/// the feature points do not follow the value noise lattice. Frozen for reproducibility.
/// @param seedKey `PcgHash` of the noise seed.
constexpr UVec3 CellularLatticeKey3(uint32_t x, uint32_t y, uint32_t z, uint32_t seedKey) {
    const UVec3 key = {x ^ seedKey, y, z};
    return key;
}

/// The `pcg3d` key of a fractal octave's seed and lattice offset: (seed, octave, `word`).
/// Frozen for reproducibility.
/// @param seed The fractal seed.
/// @param octave The octave, from 0.
/// @param word 0 for the seed and x and y offsets, 1 for the z offset of 3D fractals.
constexpr UVec3 OctaveKey(uint32_t seed, uint32_t octave, uint32_t word) {
    const UVec3 key = {seed, octave, word};
    return key;
}

}  // namespace detail

}  // namespace terraingen
