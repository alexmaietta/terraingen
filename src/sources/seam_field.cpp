// SPDX-License-Identifier: BSD-3-Clause

#include "sources/seam_field.h"

#include "terraingen/raster/resample.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace terraingen::detail {

namespace {

/// Posts along each side of a field tile.
constexpr std::size_t kTileSide = 64;

/// Rows of the lattice filled at once when finding where the earlier source has data.
constexpr std::size_t kStripRows = 64;

/// Posts from a gap within which the earlier source's posts hold their height difference.
constexpr std::size_t kBandPosts = 3;

/// Posts at the lattice's edges that are never solved for, so stencils stay on the lattice.
constexpr std::size_t kBorderPosts = 3;

/// Gauss-Seidel sweeps before and after each coarse correction.
constexpr int kSmoothingSweeps = 2;

/// Pairs of forward and backward Gauss-Seidel sweeps on the coarsest level.
constexpr int kCoarsestSweeps = 50;

/// Unknown posts at or below which a level is the coarsest.
constexpr std::size_t kCoarsestPosts = 32;

/// The most levels of a multigrid hierarchy.
constexpr std::size_t kMaxLevels = 24;

/// The residual, relative to the first, at which the conjugate gradients stop.
constexpr double kTolerance = 1e-10;

/// The most conjugate gradient iterations of a solve.
constexpr int kMaxIterations = 200;

/// Weights of the coarse posts a fine post's correction is interpolated from: its parent, the
/// parent's neighbor across the fine post's column, across its row, and diagonally.
constexpr std::array<double, 4> kTransfer{9.0 / 16.0, 3.0 / 16.0, 3.0 / 16.0, 1.0 / 16.0};

/// A lattice's posts and the distances between them.
struct Posts {
    std::size_t width = 0;
    std::size_t height = 0;
    double dx = 0.0;  ///< Between columns, in the lattice's units.
    double dy = 0.0;  ///< Between rows, in the lattice's units.
    GeoTransform transform{};

    std::size_t Size() const {
        return width * height;
    }

    Vec2d Center(std::size_t col, std::size_t row) const {
        return transform.PixelToWorld(static_cast<double>(col) + 0.5,
                                      static_cast<double>(row) + 0.5);
    }
};

/// 1 at each post where `source` has data at `spacing`, 0 elsewhere, filled a strip of rows at a
/// time.
std::vector<std::uint8_t> DataMask(const Source& source, const Posts& posts, double spacing) {
    std::vector<std::uint8_t> data(posts.Size(), 0);
    for (std::size_t top = 0; top < posts.height; top += kStripRows) {
        const std::size_t rows = std::min(kStripRows, posts.height - top);
        Raster<float> strip(posts.width, rows);
        GeoTransform transform = posts.transform;
        const Vec2d origin = posts.transform.PixelToWorld(0.0, static_cast<double>(top));
        transform.originX = origin.x;
        transform.originY = origin.y;
        strip.SetTransform(transform);
        source.Fill(strip, spacing);
        for (std::size_t row = 0; row < rows; ++row) {
            for (std::size_t col = 0; col < posts.width; ++col) {
                data[(top + row) * posts.width + col] = strip.IsMissing(strip(col, row)) ? 0 : 1;
            }
        }
    }
    return data;
}

/// 1 at each post within `radius` posts along both axes of a post without data.
std::vector<std::uint8_t> NearGaps(const std::vector<std::uint8_t>& data, const Posts& posts,
                                   std::size_t radius) {
    const std::size_t width = posts.width;
    const std::size_t height = posts.height;
    std::vector<std::uint8_t> across(data.size(), 0);
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t col = 0; col < width; ++col) {
            const std::size_t last = std::min(width - 1, col + radius);
            for (std::size_t c = col >= radius ? col - radius : 0; c <= last; ++c) {
                if (data[row * width + c] == 0) {
                    across[row * width + col] = 1;
                    break;
                }
            }
        }
    }
    std::vector<std::uint8_t> marked(data.size(), 0);
    for (std::size_t row = 0; row < height; ++row) {
        const std::size_t last = std::min(height - 1, row + radius);
        for (std::size_t col = 0; col < width; ++col) {
            for (std::size_t r = row >= radius ? row - radius : 0; r <= last; ++r) {
                if (across[r * width + col] != 0) {
                    marked[row * width + col] = 1;
                    break;
                }
            }
        }
    }
    return marked;
}

/// The chamfer distance in the lattice's units from each post to the nearest post with data,
/// over steps to the eight neighbors; with `carried`, each post without data takes the value of
/// `carried` at the post its distance comes from.
std::vector<float> DataDistances(const std::vector<std::uint8_t>& data, const Posts& posts,
                                 std::vector<float>* carried) {
    const std::size_t width = posts.width;
    const std::size_t height = posts.height;
    std::vector<float> distance(data.size(), std::numeric_limits<float>::infinity());
    for (std::size_t k = 0; k < data.size(); ++k) {
        if (data[k] != 0) {
            distance[k] = 0.0f;
        }
    }
    const auto dx = static_cast<float>(posts.dx);
    const auto dy = static_cast<float>(posts.dy);
    const auto diagonal = static_cast<float>(std::hypot(posts.dx, posts.dy));
    const auto relax = [&](std::size_t to, std::size_t from, float step) {
        const float through = distance[from] + step;
        if (through < distance[to]) {
            distance[to] = through;
            if (carried) {
                (*carried)[to] = (*carried)[from];
            }
        }
    };
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t col = 0; col < width; ++col) {
            const std::size_t k = row * width + col;
            if (data[k] != 0) {
                continue;
            }
            if (col > 0) {
                relax(k, k - 1, dx);
            }
            if (row > 0) {
                relax(k, k - width, dy);
                if (col > 0) {
                    relax(k, k - width - 1, diagonal);
                }
                if (col + 1 < width) {
                    relax(k, k - width + 1, diagonal);
                }
            }
        }
    }
    for (std::size_t row = height; row-- > 0;) {
        for (std::size_t col = width; col-- > 0;) {
            const std::size_t k = row * width + col;
            if (data[k] != 0) {
                continue;
            }
            if (col + 1 < width) {
                relax(k, k + 1, dx);
            }
            if (row + 1 < height) {
                relax(k, k + width, dy);
                if (col + 1 < width) {
                    relax(k, k + width + 1, diagonal);
                }
                if (col > 0) {
                    relax(k, k + width - 1, diagonal);
                }
            }
        }
    }
    return distance;
}

/// Sets each post without data within `width` of a post with data to the height difference at
/// the nearest edge post, faded by a smoothstep from 1 at the edge to 0 at `width`.
void Feather(SeamField& field, const std::vector<std::uint8_t>& data, const Posts& posts,
             double width) {
    std::vector<float> carried(data.size(), 0.0f);
    for (std::size_t row = 0; row < posts.height; ++row) {
        for (std::size_t col = 0; col < posts.width; ++col) {
            if (data[row * posts.width + col] != 0) {
                carried[row * posts.width + col] =
                    field.Post(static_cast<std::ptrdiff_t>(col), static_cast<std::ptrdiff_t>(row));
            }
        }
    }
    const std::vector<float> distance = DataDistances(data, posts, &carried);
    for (std::size_t row = 0; row < posts.height; ++row) {
        for (std::size_t col = 0; col < posts.width; ++col) {
            const std::size_t k = row * posts.width + col;
            if (data[k] != 0 || !(distance[k] < width)) {
                continue;
            }
            const double t = static_cast<double>(distance[k]) / width;
            const double weight = 1.0 - t * t * (3.0 - 2.0 * t);
            field.Set(col, row, static_cast<float>(weight * static_cast<double>(carried[k])));
        }
    }
}

/// A post's key, ordering posts by row, then column; a few posts before the lattice's first row
/// and column have keys too.
using Key = std::uint64_t;

/// Posts before the first row and column that have keys.
constexpr std::int64_t kKeyOffset = 16;

Key KeyAt(std::int64_t col, std::int64_t row) {
    return (static_cast<Key>(row + kKeyOffset) << 32) | static_cast<Key>(col + kKeyOffset);
}

std::int64_t KeyCol(Key key) {
    return static_cast<std::int64_t>(key & 0xffffffffu) - kKeyOffset;
}

std::int64_t KeyRow(Key key) {
    return static_cast<std::int64_t>(key >> 32) - kKeyOffset;
}

/// The posts around a post that a 5-point stencil reads.
constexpr std::array<std::array<std::int64_t, 2>, 4> kNear5{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};

/// The posts around a post that a 13-point stencil reads.
constexpr std::array<std::array<std::int64_t, 2>, 12> kNear13{{{-1, 0},
                                                               {1, 0},
                                                               {0, -1},
                                                               {0, 1},
                                                               {-2, 0},
                                                               {2, 0},
                                                               {0, -2},
                                                               {0, 2},
                                                               {-1, -1},
                                                               {1, -1},
                                                               {-1, 1},
                                                               {1, 1}}};

/// Calls `visit` with each post a stencil of `order` 1 (5 points) or 2 (13 points) reads around
/// `key`, but not `key` itself.
template <typename Visit> void VisitStencil(Key key, int order, Visit visit) {
    const std::int64_t col = KeyCol(key);
    const std::int64_t row = KeyRow(key);
    if (order == 1) {
        for (const auto& offset : kNear5) {
            visit(KeyAt(col + offset[0], row + offset[1]));
        }
    } else {
        for (const auto& offset : kNear13) {
            visit(KeyAt(col + offset[0], row + offset[1]));
        }
    }
}

/// One level of the multigrid hierarchy of a seam solve: the posts solved for and those the
/// stencil reads around them, in row-major order, then a slot that always reads 0.
///
/// The operator is B = -Laplacian + 1/L^2 on 5 points for `Height` and B applied twice, on 13
/// points, for `Slope`; posts not solved for hold fixed values.
struct SolveLevel {
    std::vector<Key> keys;                 ///< The posts, sorted.
    std::vector<std::uint8_t> unknown;     ///< Whether each post is solved for; 0 for the slot.
    std::vector<std::uint32_t> neighbors;  ///< Left, right, upper, lower post of each, or the slot.
    std::vector<std::uint32_t> coarser;    ///< The 4 posts of the next level each unknown reads.
    double cx = 0.0;                       ///< 1 / dx^2.
    double cy = 0.0;                       ///< 1 / dy^2.
    double center = 0.0;                   ///< B's weight of the center post.
    double diagonal = 0.0;                 ///< The operator's weight of the center post.
    std::vector<double> solution;          ///< The correction at this level.
    std::vector<double> rhs;               ///< What the operator should give at each unknown.

    /// The slot that reads 0, past the posts.
    std::uint32_t Slot() const {
        return static_cast<std::uint32_t>(keys.size());
    }

    /// The index of the post at `key`, or the slot without one.
    std::uint32_t Find(Key key) const {
        const auto it = std::lower_bound(keys.begin(), keys.end(), key);
        return it != keys.end() && *it == key ? static_cast<std::uint32_t>(it - keys.begin())
                                              : Slot();
    }
};

/// The level solving for `unknowns` (sorted, unique) with posts `dx` by `dy` apart.
SolveLevel MakeLevel(const std::vector<Key>& unknowns, int order, double dx, double dy,
                     double screening) {
    SolveLevel level;
    std::vector<Key> keys = unknowns;
    for (const Key key : unknowns) {
        VisitStencil(key, order, [&](Key around) { keys.push_back(around); });
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    level.keys = std::move(keys);
    const std::uint32_t slot = level.Slot();
    level.unknown.assign(static_cast<std::size_t>(slot) + 1, 0);
    for (const Key key : unknowns) {
        level.unknown[level.Find(key)] = 1;
    }
    level.neighbors.assign(4 * (static_cast<std::size_t>(slot) + 1), slot);
    for (std::uint32_t k = 0; k < slot; ++k) {
        const std::int64_t col = KeyCol(level.keys[k]);
        const std::int64_t row = KeyRow(level.keys[k]);
        const Key left = KeyAt(col - 1, row);
        const Key right = KeyAt(col + 1, row);
        level.neighbors[4 * std::size_t{k}] =
            k > 0 && level.keys[k - 1] == left ? k - 1 : level.Find(left);
        level.neighbors[4 * std::size_t{k} + 1] =
            k + 1 < slot && level.keys[k + 1] == right ? k + 1 : level.Find(right);
        level.neighbors[4 * std::size_t{k} + 2] = level.Find(KeyAt(col, row - 1));
        level.neighbors[4 * std::size_t{k} + 3] = level.Find(KeyAt(col, row + 1));
    }
    level.cx = 1.0 / (dx * dx);
    level.cy = 1.0 / (dy * dy);
    level.center = 2.0 * level.cx + 2.0 * level.cy + screening;
    level.diagonal = order == 1 ? level.center
                                : level.center * level.center + 2.0 * level.cx * level.cx +
                                      2.0 * level.cy * level.cy;
    level.solution.assign(static_cast<std::size_t>(slot) + 1, 0.0);
    level.rhs.assign(static_cast<std::size_t>(slot) + 1, 0.0);
    return level;
}

/// Points each unknown post of `fine` at the posts of `coarse` its correction is interpolated
/// from (`kTransfer`).
void Link(SolveLevel& fine, const SolveLevel& coarse) {
    const std::uint32_t slot = fine.Slot();
    fine.coarser.assign(4 * static_cast<std::size_t>(slot), coarse.Slot());
    for (std::uint32_t k = 0; k < slot; ++k) {
        if (fine.unknown[k] == 0) {
            continue;
        }
        const std::int64_t col = KeyCol(fine.keys[k]);
        const std::int64_t row = KeyRow(fine.keys[k]);
        const std::int64_t parentCol = col / 2;
        const std::int64_t parentRow = row / 2;
        const std::int64_t acrossCol = parentCol + (col % 2 == 1 ? 1 : -1);
        const std::int64_t acrossRow = parentRow + (row % 2 == 1 ? 1 : -1);
        std::uint32_t* to = &fine.coarser[4 * std::size_t{k}];
        to[0] = coarse.Find(KeyAt(parentCol, parentRow));
        to[1] = coarse.Find(KeyAt(acrossCol, parentRow));
        to[2] = coarse.Find(KeyAt(parentCol, acrossRow));
        to[3] = coarse.Find(KeyAt(acrossCol, acrossRow));
    }
}

/// B = -Laplacian + 1/L^2 of `values` at post `k`.
double ApplyScreened(const SolveLevel& level, const std::vector<double>& values, std::uint32_t k) {
    const std::uint32_t* adjacent = &level.neighbors[4 * std::size_t{k}];
    return level.center * values[k] - level.cx * (values[adjacent[0]] + values[adjacent[1]]) -
           level.cy * (values[adjacent[2]] + values[adjacent[3]]);
}

/// The operator of `order` (B, or B applied twice) of `values` at post `k`.
double Apply(const SolveLevel& level, int order, const std::vector<double>& values,
             std::uint32_t k) {
    if (order == 1) {
        return ApplyScreened(level, values, k);
    }
    const std::uint32_t* adjacent = &level.neighbors[4 * std::size_t{k}];
    return level.center * ApplyScreened(level, values, k) -
           level.cx * (ApplyScreened(level, values, adjacent[0]) +
                       ApplyScreened(level, values, adjacent[1])) -
           level.cy * (ApplyScreened(level, values, adjacent[2]) +
                       ApplyScreened(level, values, adjacent[3]));
}

/// One Gauss-Seidel sweep of the level's unknowns, in row-major order or its reverse.
void Sweep(SolveLevel& level, int order, bool forward) {
    const std::uint32_t slot = level.Slot();
    for (std::uint32_t i = 0; i < slot; ++i) {
        const std::uint32_t k = forward ? i : slot - 1 - i;
        if (level.unknown[k] != 0) {
            level.solution[k] +=
                (level.rhs[k] - Apply(level, order, level.solution, k)) / level.diagonal;
        }
    }
}

/// A symmetric multigrid V-cycle from level `depth` down: an approximate solution of the
/// level's operator for its `rhs`, starting from 0, with 0 at every post not solved for.
void VCycle(std::vector<SolveLevel>& levels, std::size_t depth, int order) {
    SolveLevel& level = levels[depth];
    std::fill(level.solution.begin(), level.solution.end(), 0.0);
    if (depth + 1 == levels.size()) {
        for (int sweep = 0; sweep < kCoarsestSweeps; ++sweep) {
            Sweep(level, order, true);
            Sweep(level, order, false);
        }
        return;
    }
    for (int sweep = 0; sweep < kSmoothingSweeps; ++sweep) {
        Sweep(level, order, true);
    }
    SolveLevel& coarse = levels[depth + 1];
    std::fill(coarse.rhs.begin(), coarse.rhs.end(), 0.0);
    const std::uint32_t slot = level.Slot();
    for (std::uint32_t k = 0; k < slot; ++k) {
        if (level.unknown[k] == 0) {
            continue;
        }
        const double residual = level.rhs[k] - Apply(level, order, level.solution, k);
        for (std::size_t q = 0; q < kTransfer.size(); ++q) {
            coarse.rhs[level.coarser[4 * std::size_t{k} + q]] += 0.25 * kTransfer[q] * residual;
        }
    }
    for (std::size_t c = 0; c < coarse.rhs.size(); ++c) {
        if (coarse.unknown[c] == 0) {
            coarse.rhs[c] = 0.0;
        }
    }
    VCycle(levels, depth + 1, order);
    for (std::uint32_t k = 0; k < slot; ++k) {
        if (level.unknown[k] == 0) {
            continue;
        }
        double correction = 0.0;
        for (std::size_t q = 0; q < kTransfer.size(); ++q) {
            correction += kTransfer[q] * coarse.solution[level.coarser[4 * std::size_t{k} + q]];
        }
        level.solution[k] += correction;
    }
    for (int sweep = 0; sweep < kSmoothingSweeps; ++sweep) {
        Sweep(level, order, false);
    }
}

/// The multigrid levels of `unknowns` (sorted), each coarser level halving the posts, linked.
std::vector<SolveLevel> BuildHierarchy(std::vector<Key> keys, int order, double dx, double dy,
                                       double screening) {
    std::vector<SolveLevel> levels;
    for (;;) {
        levels.push_back(MakeLevel(keys, order, dx, dy, screening));
        if (keys.size() <= kCoarsestPosts || levels.size() == kMaxLevels) {
            break;
        }
        std::vector<Key> coarse;
        coarse.reserve(keys.size());
        for (const Key key : keys) {
            coarse.push_back(KeyAt(KeyCol(key) / 2, KeyRow(key) / 2));
        }
        std::sort(coarse.begin(), coarse.end());
        coarse.erase(std::unique(coarse.begin(), coarse.end()), coarse.end());
        keys = std::move(coarse);
        dx *= 2.0;
        dy *= 2.0;
    }
    for (std::size_t depth = 0; depth + 1 < levels.size(); ++depth) {
        Link(levels[depth], levels[depth + 1]);
    }
    return levels;
}

/// The solution of the finest level's operator for `residual`, by conjugate gradients
/// preconditioned with multigrid V-cycles over `levels`.
std::vector<double> PreconditionedCg(std::vector<SolveLevel>& levels, int order,
                                     std::vector<double> residual) {
    SolveLevel& fine = levels.front();
    const std::uint32_t slot = fine.Slot();
    const auto forUnknowns = [&](const auto& visit) {
        for (std::uint32_t k = 0; k < slot; ++k) {
            if (fine.unknown[k] != 0) {
                visit(k);
            }
        }
    };
    const auto dot = [&](const std::vector<double>& a, const std::vector<double>& b) {
        double sum = 0.0;
        forUnknowns([&](std::uint32_t k) { sum += a[k] * b[k]; });
        return sum;
    };
    const auto precondition = [&] {
        fine.rhs = residual;
        VCycle(levels, 0, order);
        return fine.solution;
    };
    std::vector<double> solution(residual.size(), 0.0);
    std::vector<double> applied(residual.size(), 0.0);
    const double target = kTolerance * std::sqrt(dot(residual, residual));
    std::vector<double> direction = precondition();
    double fit = dot(residual, direction);
    for (int iteration = 0; iteration < kMaxIterations && fit > 0.0; ++iteration) {
        for (std::uint32_t k = 0; k < slot; ++k) {
            applied[k] = fine.unknown[k] != 0 ? Apply(fine, order, direction, k) : 0.0;
        }
        const double curvature = dot(direction, applied);
        if (!(curvature > 0.0)) {
            break;
        }
        const double step = fit / curvature;
        forUnknowns([&](std::uint32_t k) {
            solution[k] += step * direction[k];
            residual[k] -= step * applied[k];
        });
        if (std::sqrt(dot(residual, residual)) <= target) {
            break;
        }
        const std::vector<double> preconditioned = precondition();
        const double next = dot(residual, preconditioned);
        const double ratio = next / fit;
        fit = next;
        forUnknowns(
            [&](std::uint32_t k) { direction[k] = preconditioned[k] + ratio * direction[k]; });
    }
    return solution;
}

/// Solves the operator of `order` = 0 for the posts `unknowns` (sorted, one connected region)
/// with the posts around them fixed at their values in `field`, and sets them in `field`.
void SolveRegion(SeamField& field, const std::vector<Key>& unknowns, int order, double dx,
                 double dy, double screening) {
    std::vector<SolveLevel> levels = BuildHierarchy(unknowns, order, dx, dy, screening);
    const SolveLevel& fine = levels.front();
    const std::uint32_t slot = fine.Slot();
    const auto post = [&](std::uint32_t k) {
        return std::make_pair(KeyCol(fine.keys[k]), KeyRow(fine.keys[k]));
    };
    std::vector<double> fixed(static_cast<std::size_t>(slot) + 1, 0.0);
    for (std::uint32_t k = 0; k < slot; ++k) {
        if (fine.unknown[k] == 0) {
            const auto [col, row] = post(k);
            fixed[k] = static_cast<double>(
                field.Post(static_cast<std::ptrdiff_t>(col), static_cast<std::ptrdiff_t>(row)));
        }
    }
    std::vector<double> residual(fixed.size(), 0.0);
    for (std::uint32_t k = 0; k < slot; ++k) {
        residual[k] = fine.unknown[k] != 0 ? -Apply(fine, order, fixed, k) : 0.0;
    }
    const std::vector<double> solution = PreconditionedCg(levels, order, std::move(residual));
    for (std::uint32_t k = 0; k < slot; ++k) {
        if (fine.unknown[k] != 0) {
            const auto [col, row] = post(k);
            field.Set(static_cast<std::size_t>(col), static_cast<std::size_t>(row),
                      static_cast<float>(solution[k]));
        }
    }
}

/// `unknowns` (sorted) split into the regions a stencil of `order` connects, each sorted, in the
/// order of their first post.
std::vector<std::vector<Key>> Regions(const std::vector<Key>& unknowns, int order) {
    std::vector<std::uint8_t> seen(unknowns.size(), 0);
    std::vector<std::vector<Key>> regions;
    std::vector<std::size_t> queue;
    for (std::size_t start = 0; start < unknowns.size(); ++start) {
        if (seen[start] != 0) {
            continue;
        }
        seen[start] = 1;
        queue.assign(1, start);
        std::vector<Key> region;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            const Key key = unknowns[queue[head]];
            region.push_back(key);
            VisitStencil(key, order, [&](Key around) {
                const auto it = std::lower_bound(unknowns.begin(), unknowns.end(), around);
                if (it == unknowns.end() || *it != around) {
                    return;
                }
                const auto index = static_cast<std::size_t>(it - unknowns.begin());
                if (seen[index] == 0) {
                    seen[index] = 1;
                    queue.push_back(index);
                }
            });
        }
        std::sort(region.begin(), region.end());
        regions.push_back(std::move(region));
    }
    return regions;
}

}  // namespace

SeamField::SeamField(const RasterGrid& lattice)
    : lattice_(lattice), tileColumns_((lattice.width + kTileSide - 1) / kTileSide),
      tiles_(tileColumns_ * ((lattice.height + kTileSide - 1) / kTileSide)) {}

float SeamField::Post(std::ptrdiff_t col, std::ptrdiff_t row) const {
    if (col < 0 || row < 0 || static_cast<std::size_t>(col) >= lattice_.width ||
        static_cast<std::size_t>(row) >= lattice_.height) {
        return 0.0f;
    }
    const auto c = static_cast<std::size_t>(col);
    const auto r = static_cast<std::size_t>(row);
    const std::vector<float>& tile = tiles_[(r / kTileSide) * tileColumns_ + c / kTileSide];
    return tile.empty() ? 0.0f : tile[(r % kTileSide) * kTileSide + c % kTileSide];
}

void SeamField::Set(std::size_t col, std::size_t row, float value) {
    std::vector<float>& tile = tiles_[(row / kTileSide) * tileColumns_ + col / kTileSide];
    if (tile.empty()) {
        if (value == 0.0f) {
            return;
        }
        tile.assign(kTileSide * kTileSide, 0.0f);
    }
    tile[(row % kTileSide) * kTileSide + col % kTileSide] = value;
    set_ = true;
}

double SeamField::At(double x, double y) const {
    if (!set_) {
        return 0.0;
    }
    const Vec2d pixel = lattice_.transform.WorldToPixel(x, y);
    const double fx = pixel.x - 0.5;
    const double fy = pixel.y - 0.5;
    const double left = std::floor(fx);
    const double top = std::floor(fy);
    if (!(left >= -2.0 && top >= -2.0 && left <= static_cast<double>(lattice_.width) &&
          top <= static_cast<double>(lattice_.height))) {
        return 0.0;
    }
    const auto post = [this](double col, double row) {
        return Post(static_cast<std::ptrdiff_t>(col), static_cast<std::ptrdiff_t>(row));
    };
    return *InterpolateLattice(fx, fy, ResampleMethod::CatmullRom, post,
                               [](float) { return false; });
}

SeamField BuildSeamField(const Source& earlier, const Source& later, const RasterGrid& grid,
                         const ChainSeams& seams) {
    if (seams.mode == ChainSeamMode::None) {
        return {};
    }
    const RasterGrid lattice = SeamLattice(grid, seams);
    const GeoTransform& transform = lattice.transform;
    const Posts posts{lattice.width, lattice.height,
                      std::hypot(transform.pixelWidth, transform.colRotation),
                      std::hypot(transform.rowRotation, transform.pixelHeight), transform};
    const double spacing = grid.PixelSize();
    const std::vector<std::uint8_t> data = DataMask(earlier, posts, spacing);
    const auto has = [](std::uint8_t value) { return value != 0; };
    if (std::all_of(data.begin(), data.end(), has) || std::none_of(data.begin(), data.end(), has)) {
        return {};
    }
    SeamField field(lattice);
    const std::vector<std::uint8_t> band = NearGaps(data, posts, kBandPosts);
    for (std::size_t row = 0; row < posts.height; ++row) {
        for (std::size_t col = 0; col < posts.width; ++col) {
            const std::size_t k = row * posts.width + col;
            if (data[k] == 0 || band[k] == 0) {
                continue;
            }
            const Vec2d center = posts.Center(col, row);
            const float above = earlier.HeightAt(center.x, center.y, spacing);
            const float below = later.HeightAt(center.x, center.y, spacing);
            if (std::isfinite(above) && std::isfinite(below)) {
                field.Set(
                    col, row,
                    static_cast<float>(static_cast<double>(above) - static_cast<double>(below)));
            }
        }
    }
    if (seams.mode == ChainSeamMode::Feather) {
        Feather(field, data, posts, seams.width);
        return field;
    }
    const std::vector<float> distance = DataDistances(data, posts, nullptr);
    const double reach = SeamReach(seams);
    std::vector<Key> unknowns;
    for (std::size_t row = kBorderPosts; row + kBorderPosts < posts.height; ++row) {
        for (std::size_t col = kBorderPosts; col + kBorderPosts < posts.width; ++col) {
            const std::size_t k = row * posts.width + col;
            if (data[k] == 0 && static_cast<double>(distance[k]) <= reach) {
                unknowns.push_back(
                    KeyAt(static_cast<std::int64_t>(col), static_cast<std::int64_t>(row)));
            }
        }
    }
    const int order = seams.mode == ChainSeamMode::Slope ? 2 : 1;
    const double screening = 1.0 / (seams.decayLength * seams.decayLength);
    for (const std::vector<Key>& region : Regions(unknowns, order)) {
        SolveRegion(field, region, order, posts.dx, posts.dy, screening);
    }
    return field;
}

}  // namespace terraingen::detail
