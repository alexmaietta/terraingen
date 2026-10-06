#include "terraingen/pipeline/tiling.h"

#include "terraingen/noise/kernels/hash.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr int kMaxLevel = 60;

std::int64_t FloorDivide(std::int64_t value, std::int64_t divisor) {
    const std::int64_t quotient = value / divisor;
    return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

void CheckLevel(int level) {
    if (level < 0 || level > kMaxLevel) {
        throw std::invalid_argument("Tile level must be between 0 and " +
                                    std::to_string(kMaxLevel));
    }
}

}  // namespace

std::size_t TileKeyHash::operator()(const TileKey& key) const {
    const UVec3 hash =
        Pcg3dHash({static_cast<std::uint32_t>(key.level),
                   static_cast<std::uint32_t>(key.x) ^ static_cast<std::uint32_t>(key.x >> 32),
                   static_cast<std::uint32_t>(key.y) ^ static_cast<std::uint32_t>(key.y >> 32)});
    return (static_cast<std::size_t>(hash.x) << 32) ^ hash.y;
}

TileScheme TileScheme::Covering(const RasterGrid& grid, std::size_t cells) {
    const GeoTransform& t = grid.transform;
    if (grid.width == 0 || grid.height == 0 || cells == 0) {
        throw std::invalid_argument("A tile scheme needs a non-empty grid and cells");
    }
    if (t.rowRotation != 0.0 || t.colRotation != 0.0 || !(t.pixelWidth > 0.0) ||
        t.pixelHeight != -t.pixelWidth) {
        throw std::invalid_argument("A tile scheme covers north-up grids with square cells");
    }
    const double tileSize = static_cast<double>(cells) * t.pixelWidth;
    const double extent =
        std::fmax(static_cast<double>(grid.width), static_cast<double>(grid.height)) * t.pixelWidth;
    double rootSize = tileSize;
    while (rootSize < extent) {
        rootSize *= 2.0;
    }
    return {{t.originX, t.originY}, rootSize, cells};
}

void TileScheme::Validate() const {
    if (!(std::isfinite(rootSize) && rootSize > 0.0)) {
        throw std::invalid_argument("Tile root size must be positive and finite");
    }
    if (!(std::isfinite(origin.x) && std::isfinite(origin.y))) {
        throw std::invalid_argument("Tile origin must be finite");
    }
    if (cells == 0) {
        throw std::invalid_argument("Tiles need at least one cell");
    }
}

double TileScheme::TileSize(int level) const {
    CheckLevel(level);
    return std::ldexp(rootSize, -level);
}

double TileScheme::Spacing(int level) const {
    return TileSize(level) / static_cast<double>(cells);
}

int TileScheme::LevelFor(double spacing) const {
    if (!(std::isfinite(spacing) && spacing > 0.0)) {
        throw std::invalid_argument("Spacing must be positive and finite");
    }
    int level = 0;
    while (level < kMaxLevel && Spacing(level) > spacing) {
        ++level;
    }
    return level;
}

TileKey TileScheme::TileAt(double x, double y, int level) const {
    const double size = TileSize(level);
    return {level, static_cast<std::int64_t>(std::floor((x - origin.x) / size)),
            static_cast<std::int64_t>(std::floor((origin.y - y) / size))};
}

std::vector<TileKey> TileScheme::TilesCovering(const Bounds2d& bounds, int level) const {
    const double size = TileSize(level);
    const auto x0 = static_cast<std::int64_t>(std::floor((bounds.minX - origin.x) / size));
    const auto x1 = static_cast<std::int64_t>(std::ceil((bounds.maxX - origin.x) / size));
    const auto y0 = static_cast<std::int64_t>(std::floor((origin.y - bounds.maxY) / size));
    const auto y1 = static_cast<std::int64_t>(std::ceil((origin.y - bounds.minY) / size));
    std::vector<TileKey> keys;
    for (std::int64_t y = y0; y < y1; ++y) {
        for (std::int64_t x = x0; x < x1; ++x) {
            keys.push_back({level, x, y});
        }
    }
    return keys;
}

RasterGrid TileScheme::Grid(const TileKey& key) const {
    const double size = TileSize(key.level);
    return {cells, cells,
            GeoTransform::NorthUp(origin.x + static_cast<double>(key.x) * size,
                                  origin.y - static_cast<double>(key.y) * size,
                                  size / static_cast<double>(cells))};
}

TileKey TileScheme::Parent(const TileKey& key) {
    CheckLevel(key.level);
    if (key.level == 0) {
        throw std::invalid_argument("Level 0 tiles have no parent");
    }
    return {key.level - 1, FloorDivide(key.x, 2), FloorDivide(key.y, 2)};
}

std::array<TileKey, 4> TileScheme::Children(const TileKey& key) {
    CheckLevel(key.level + 1);
    const int level = key.level + 1;
    return {TileKey{level, 2 * key.x, 2 * key.y}, TileKey{level, 2 * key.x + 1, 2 * key.y},
            TileKey{level, 2 * key.x, 2 * key.y + 1}, TileKey{level, 2 * key.x + 1, 2 * key.y + 1}};
}

}  // namespace terraingen
