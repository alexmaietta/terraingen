// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/boulders/boulder_layer.h"

#include "terraingen/noise/hash.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/bits.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace terraingen {

const ManualBoulder* BoulderLayer::Fixed(std::uint64_t id) const {
    const auto found = fixed_.find(id);
    return found != fixed_.end() ? &found->second : nullptr;
}

bool BoulderLayer::Settles(const Boulder& boulder) const {
    if (!options_.pose.settle) {
        return false;
    }
    const ManualBoulder* fixed = boulder.parent == 0 ? Fixed(boulder.id) : nullptr;
    return fixed == nullptr || !fixed->FixesOrientation();
}

double BoulderLayer::Wear(double diameter, const BoulderAbundance& abundance, double density,
                          std::uint64_t id) const {
    RandomStream random = AttributeStream(id, BoulderDraw::Wear);
    const double thrown = random.NextDouble();
    if (abundance.ejecta > 0.0 && density > 0.0) {
        const double fromCrater = abundance.ejecta * ejectaSizes_->Incremental(diameter) / density;
        if (thrown < fromCrater) {
            return abundance.ejectaWear;
        }
    }
    return random.NextDouble();
}

Boulder BoulderLayer::DrawPose(const ThinnedCandidate& thinned, double spacing,
                               const ManualBoulder* fixed) const {
    const Crater& candidate = thinned.candidate;
    RandomStream random = AttributeStream(candidate.id, BoulderDraw::Pose);
    const BoulderPose& pose = options_.pose;
    const double ratio =
        ValueRange{pose.minHeightRatio, pose.maxHeightRatio}.Lerp(random.NextDouble());
    double wear = Wear(candidate.diameter, thinned.abundance, thinned.density, candidate.id);
    if (fixed != nullptr && fixed->wear) {
        wear = *fixed->wear;
    }
    double share = random.NextDouble();
    if (pose.burialByWear > 0.0) {
        share = (1.0 - pose.burialByWear) * share + pose.burialByWear * wear;
    }
    const double burial = ValueRange{pose.minBurial, pose.maxBurial}.Lerp(share);
    const double yaw = 2.0 * kPi * random.NextDouble();
    const double lean = (pose.maxLeanDegrees * kDegree) * random.NextDouble();
    Boulder boulder{
        candidate.x, candidate.y, candidate.diameter, ratio * candidate.diameter, burial,
        yaw,         lean,        candidate.id};
    boulder.wear = wear;
    boulder.model = options_.rendering.modelThreshold.IsModel(boulder.diameter, spacing);
    if (fixed != nullptr) {
        boulder.height = fixed->height.value_or(boulder.height);
        boulder.burial = fixed->burial.value_or(boulder.burial);
        if (fixed->yawDegrees) {
            boulder.yaw = *fixed->yawDegrees * kDegree;
        }
        if (fixed->leanDegrees) {
            boulder.lean = *fixed->leanDegrees * kDegree;
        }
        boulder.shapeSeed = fixed->shapeSeed;
        boulder.model = fixed->model.value_or(boulder.model);
    }
    return boulder;
}

std::optional<Boulder> BoulderLayer::PlaceCandidate(const ThinnedCandidate& thinned, double spacing,
                                                    const Ground* ground) const {
    const ManualBoulder* fixed = thinned.candidate.manual ? Fixed(thinned.candidate.id) : nullptr;
    Boulder boulder = DrawPose(thinned, spacing, fixed);
    const BoulderPose& pose = options_.pose;
    if (pose.maxSlopeDegrees && ground != nullptr && !thinned.candidate.manual &&
        !Roll(boulder, *ground)) {
        return std::nullopt;
    }
    ReadGround(boulder, ground);
    if (pose.sideShare > 0.0 || pose.endShare > 0.0 || (pose.maxTiltDegrees * kDegree) > 0.0 ||
        pose.followSlope || (fixed != nullptr && fixed->FixesOrientation())) {
        Settle(boulder, ground, fixed);
    }
    if (options_.ventifacts.share > 0.0) {
        Abrade(boulder);
    }
    return boulder;
}

void BoulderLayer::ReadGround(Boulder& boulder, const Ground* ground) const {
    boulder.ground.reset();
    if (ground != nullptr) {
        const double height =
            static_cast<double>(ground->HeightAt(boulder.x, boulder.y, options_.groundSpacing));
        if (!std::isnan(height)) {
            boulder.ground = height;
        }
    }
}

void BoulderLayer::Abrade(Boulder& boulder) const {
    const BoulderVentifacts& ventifacts = options_.ventifacts;
    RandomStream random = AttributeStream(boulder.id, BoulderDraw::Abrade);
    if (random.NextDouble() >= ventifacts.share) {
        return;
    }
    const double reach = 0.5 * boulder.diameter * (1.0 - boulder.wear);
    const auto facet = [&](double wind, double pick) {
        return BoulderFacet{wind + kPi,
                            ValueRange{(ventifacts.minAngleDegrees * kDegree),
                                       (ventifacts.maxAngleDegrees * kDegree)}
                                .Lerp(pick),
                            reach};
    };
    boulder.facets[0] = facet((ventifacts.windAzimuthDegrees * kDegree), random.NextDouble());
    boulder.facetCount = 1;
    const double keel = random.NextDouble();
    const double second = random.NextDouble();
    if (ventifacts.secondWindAzimuthDegrees && keel < ventifacts.keelShare) {
        boulder.facets[1] = facet((*ventifacts.secondWindAzimuthDegrees * kDegree), second);
        boulder.facetCount = 2;
    }
}

bool BoulderLayer::Roll(Boulder& boulder, const Ground& ground) const {
    constexpr double kMaxSteps = 128.0;
    const BoulderPose& pose = options_.pose;
    const double limit = std::tan((*pose.maxSlopeDegrees * kDegree));
    const double reach = pose.rollDistance.value_or(0.0);
    const double step = std::fmax(boulder.diameter, reach / kMaxSteps);
    double rolled = 0.0;
    for (;;) {
        const std::optional<Vec2d> g = Gradient(boulder, ground);
        const double gradient = g ? std::hypot(g->x, g->y) : 0.0;
        if (!(gradient > limit)) {
            return true;
        }
        if (rolled + step > reach) {
            return false;
        }
        const Vec2d to = surface_.Locate({boulder.x, boulder.y},
                                         {-g->x / gradient * step, -g->y / gradient * step});
        boulder.x = to.x;
        boulder.y = to.y;
        rolled += step;
    }
}

std::optional<Vec2d> BoulderLayer::Gradient(const Boulder& boulder, const Ground& ground) const {
    return GroundGradient(ground, surface_, {boulder.x, boulder.y}, 0.5 * boulder.diameter,
                          options_.groundSpacing);
}

Vec3d BoulderLayer::UpAxis(RandomStream& random, const ManualBoulder* fixedTilt) const {
    const BoulderPose& pose = options_.pose;
    Vec3d up{0.0, 0.0, 1.0};
    double cosine = 1.0;
    double azimuth = 0.0;
    if ((pose.maxTiltDegrees * kDegree) > 0.0) {
        cosine = 1.0 - random.NextDouble() * (1.0 - std::cos(pose.maxTiltDegrees * kDegree));
        const double sine = std::sqrt(std::fmax(0.0, 1.0 - cosine * cosine));
        azimuth = 2.0 * kPi * random.NextDouble();
        up = {sine * std::cos(azimuth), sine * std::sin(azimuth), cosine};
    }
    if (fixedTilt == nullptr) {
        return up;
    }
    if (!((pose.maxTiltDegrees * kDegree) > 0.0)) {
        azimuth = 2.0 * kPi * random.NextDouble();
    }
    const double tilt =
        fixedTilt->tiltDegrees ? *fixedTilt->tiltDegrees * kDegree : std::acos(cosine);
    const double toward =
        fixedTilt->tiltAzimuthDegrees ? *fixedTilt->tiltAzimuthDegrees * kDegree : azimuth;
    return {std::sin(tilt) * std::cos(toward), std::sin(tilt) * std::sin(toward), std::cos(tilt)};
}

void BoulderLayer::Settle(Boulder& boulder, const Ground* ground,
                          const ManualBoulder* fixed) const {
    const BoulderPose& pose = options_.pose;
    RandomStream random = AttributeStream(boulder.id, BoulderDraw::Settle);
    const double pick = random.NextDouble();
    boulder.rest = pick < pose.endShare                    ? BoulderRest::End
                   : pick < pose.endShare + pose.sideShare ? BoulderRest::Side
                                                           : BoulderRest::Face;
    if (fixed != nullptr && fixed->rest) {
        boulder.rest = *fixed->rest;
    }
    const bool fixesTilt = fixed != nullptr && (fixed->tiltDegrees.has_value() ||
                                                fixed->tiltAzimuthDegrees.has_value());
    Vec3d up = UpAxis(random, fixesTilt ? fixed : nullptr);
    bool sloped = false;
    if (pose.followSlope && ground != nullptr) {
        if (const std::optional<Vec2d> g = Gradient(boulder, *ground)) {
            const double slope = std::atan(std::hypot(g->x, g->y));
            up = TiltRotation(slope, std::atan2(-g->y, -g->x)).Apply(up);
            sloped = true;
        }
    }
    boulder.tilt = std::acos(std::clamp(up.z, -1.0, 1.0));
    boulder.tiltAzimuth = std::atan2(up.y, up.x);
    if (fixesTilt && !sloped) {
        if (fixed->tiltDegrees) {
            boulder.tilt = *fixed->tiltDegrees * kDegree;
        }
        if (fixed->tiltAzimuthDegrees) {
            boulder.tiltAzimuth = *fixed->tiltAzimuthDegrees * kDegree;
        }
    }
}

void BoulderLayer::ReadGrounds(std::vector<Boulder>& boulders,
                               const std::vector<std::size_t>& indices,
                               const Ground* ground) const {
    std::vector<Vec2d> points;
    points.reserve(indices.size());
    for (const std::size_t n : indices) {
        boulders[n].ground.reset();
        points.push_back({boulders[n].x, boulders[n].y});
    }
    if (ground == nullptr || points.empty()) {
        return;
    }
    std::vector<float> heights(points.size());
    ground->HeightsAt(points, heights, options_.groundSpacing);
    for (std::size_t k = 0; k < indices.size(); ++k) {
        if (!std::isnan(heights[k])) {
            boulders[indices[k]].ground = heights[k];
        }
    }
}

BoulderLayer::BreakStage BoulderLayer::Stage(const Boulder& parent) const {
    const BoulderFragmentation& fragmentation = *options_.fragmentation;
    const bool rubble = parent.wear >= fragmentation.rubbleWear;
    BreakStage stage;
    stage.opening = rubble || fragmentation.rubbleWear <= fragmentation.splitWear
                        ? 1.0
                        : (parent.wear - fragmentation.splitWear) /
                              (fragmentation.rubbleWear - fragmentation.splitWear);
    stage.scatter = !rubble                          ? 0.0
                    : fragmentation.rubbleWear < 1.0 ? (parent.wear - fragmentation.rubbleWear) /
                                                           (1.0 - fragmentation.rubbleWear)
                                                     : 1.0;
    return stage;
}

Boulder BoulderLayer::PlacePiece(const Boulder& parent, const BoulderFragment& fragment,
                                 std::size_t k, const PieceFrame& frame, double push,
                                 double twist) const {
    const BoulderFragmentation& fragmentation = *options_.fragmentation;
    const BoulderPlacement& placement = frame.placement;
    const double scatter = frame.stage.scatter;
    const auto [low, high] = ZRange(fragment.shape.mesh.positions, [&](const Vec3d& p) {
        return placement.Apply(fragment.center + p * fragment.scale);
    });
    const Vec3d center = placement.Apply(fragment.center);
    const double out = std::hypot(center.x, center.y);
    const double angle = out > 0.0 ? std::atan2(center.y, center.x) : 2.0 * kPi * push;
    const double gap = (frame.stage.opening * fragmentation.gapRatio +
                        scatter * fragmentation.spreadRatio * (0.5 + 0.5 * push)) *
                       parent.diameter;
    Boulder piece = parent;
    piece.facetCount = 0;
    const IdWords id = SplitId(parent.id);
    const UVec3 key =
        Pcg3dHash({id.low, id.high, kBoulderPieceStream + static_cast<std::uint32_t>(k)});
    piece.id = JoinWords(key.x, key.y);
    piece.parent = parent.id;
    piece.piece = static_cast<std::uint32_t>(k);
    piece.diameter = parent.diameter * fragment.scale;
    piece.height = parent.height * fragment.shape.shortAxis * fragment.scale / frame.shortAxis;
    piece.yaw = parent.yaw + scatter * kPi * (2.0 * twist - 1.0);
    piece.lean = parent.lean * (1.0 - scatter);
    piece.tilt = parent.tilt * (1.0 - scatter);
    const double sunk = std::clamp(-low / (high - low), 0.0, 0.95);
    piece.burial = std::fmax(sunk, sunk + scatter * (options_.pose.maxBurial - sunk));
    const Vec2d at = surface_.Locate(
        {parent.x, parent.y}, {center.x + gap * std::cos(angle), center.y + gap * std::sin(angle)});
    piece.x = at.x;
    piece.y = at.y;
    return piece;
}

std::vector<Boulder> BoulderLayer::Break(const Boulder& parent, double spacing,
                                         double smallest) const {
    if (!options_.fragmentation) {
        return {parent};
    }
    const BoulderFragmentation& fragmentation = *options_.fragmentation;
    if (parent.diameter < fragmentation.minDiameter || parent.wear < fragmentation.splitWear ||
        Fixed(parent.id) != nullptr) {
        return {parent};
    }
    const auto shape = Shape(parent);
    const auto fragments = CachedFragments(ShapeSeed(parent, options_.rendering.shapePrototypes));
    Boulder origin = parent;
    origin.x = 0.0;
    origin.y = 0.0;
    const PieceFrame frame{PlaceBoulderModel(origin, *shape, 0.0, Settles(parent)), Stage(parent),
                           shape->shortAxis};
    const IdWords id = SplitId(parent.id);
    RandomStream random(options_.seed, kBoulderPieceStream, id.low, id.high);
    std::vector<Boulder> pieces;
    for (std::size_t k = 0; k < fragments->size(); ++k) {
        const double push = random.NextDouble();
        const double twist = random.NextDouble();
        Boulder piece = PlacePiece(parent, (*fragments)[k], k, frame, push, twist);
        if (piece.diameter < smallest) {
            continue;
        }
        piece.model = options_.rendering.modelThreshold.IsModel(piece.diameter, spacing);
        pieces.push_back(piece);
    }
    return pieces;
}

}  // namespace terraingen
