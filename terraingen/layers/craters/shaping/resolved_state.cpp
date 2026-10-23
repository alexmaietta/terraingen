// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/shaping/resolved_state.h"

#include "terraingen/layers/craters/degradation/diffusion.h"
#include "terraingen/layers/craters/profiles/complex.h"
#include "terraingen/layers/craters/profiles/fassett_thomson.h"
#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

/// "The manual crater at (x, y), D m across", which starts error messages.
std::string CraterName(const Crater& crater) {
    std::array<char, 128> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "The manual crater at (%g, %g), %g m across",
                  crater.x, crater.y, crater.diameter);
    return buffer.data();
}

/// The profile that shapes a crater of `diameter` meters from its fresh state: `profile` without
/// diffusion, and its simple profile below the complex transition.
const CraterProfile& FreshShape(const CraterProfile& profile, double diameter) {
    const CraterProfile* shape = &profile;
    if (const auto* diffused = dynamic_cast<const DiffusedProfile*>(shape)) {
        shape = &diffused->Fresh();
    }
    if (const auto* complex = dynamic_cast<const ComplexCraterProfile*>(shape)) {
        if (!complex->IsComplex(diameter)) {
            shape = &complex->Simple();
        }
    }
    return *shape;
}

/// The lowest point of the Fassett-Thomson fit within the rim, over the diameter.
double FitFloorRatio(const FassettThomsonProfile& profile, double diameter,
                     const CraterMorphometry& fresh) {
    constexpr double kInf = std::numeric_limits<double>::infinity();
    const CraterState unbounded{fresh, {kInf, fresh.rimHeightRatio}, 0.0};
    double floor = kInf;
    for (int step = 0; step <= 200; ++step) {
        const double distance = 0.0025 * static_cast<double>(step) * diameter;
        floor = std::fmin(floor, profile.Height(distance, diameter, unbounded) / diameter);
    }
    return floor;
}

/// Checks that a Fassett-Thomson simple crater can take `crater`'s explicit depth and rim height.
/// @throws std::invalid_argument otherwise.
void CheckFassettThomson(const FassettThomsonProfile& profile, const Crater& crater,
                         const CraterMorphometry& fresh) {
    const std::string honored = "; the Martin-Parkes-Dunstan profile and complex craters honor "
                                "both depth and rim height";
    if (!std::isnan(crater.rimHeight)) {
        throw std::invalid_argument(CraterName(crater) +
                                    " has a rim height, which the Fassett-Thomson profile fixes" +
                                    honored);
    }
    const double deepest = fresh.rimHeightRatio - FitFloorRatio(profile, crater.diameter, fresh);
    if (crater.depth / crater.diameter > deepest * (1.0 + 1e-12)) {
        throw std::invalid_argument(CraterName(crater) +
                                    " is deeper than the Fassett-Thomson fit (" +
                                    std::to_string(deepest * crater.diameter) + " m)" + honored);
    }
}

/// The state of a crater of explicit depth ratio `depth` under a diffused profile, from `fresh`.
/// @throws std::invalid_argument for a rim height, or a depth shallower than the oldest table.
CraterState DiffusedState(const DiffusedProfile& profile, const Crater& crater,
                          CraterMorphometry fresh, double depth) {
    if (!std::isnan(crater.rimHeight)) {
        throw std::invalid_argument(
            CraterName(crater) + " has a rim height, which a diffused profile ties to its depth; "
                                 "give the depth alone");
    }
    const double diameter = crater.diameter;
    double tau = 0.0;
    if (depth < fresh.depthRatio) {
        const double oldest =
            profile.Morphometry(fresh, diameter, profile.Ages().back()).depthRatio;
        if (oldest > depth) {
            throw std::invalid_argument(CraterName(crater) +
                                        " is shallower than diffusion wears it (" +
                                        std::to_string(oldest * diameter) + " m)");
        }
        tau = profile.AgeAtDepthRatio(fresh, diameter, depth);
    }
    if (!(tau > 0.0)) {
        fresh.depthRatio = depth;
        return {fresh, fresh, 0.0};
    }
    return {fresh, profile.Morphometry(fresh, diameter, tau), tau};
}

}  // namespace

CraterState ResolvedCraterState(const CraterProfile& profile, const DegradationModel& degradation,
                                const Crater& crater, double rate) {
    if (std::isnan(crater.depth) && std::isnan(crater.rimHeight)) {
        return rate > 1.0 ? degradation.WornState(crater, rate) : degradation.State(crater);
    }
    if (!(IsPositive(crater.depth) &&
          (std::isnan(crater.rimHeight) || IsNonNegative(crater.rimHeight)))) {
        throw std::invalid_argument(CraterName(crater) +
                                    " needs a positive depth, and a rim height that is unset or "
                                    "not negative");
    }
    CraterMorphometry fresh = degradation.State(crater).fresh;
    const double diameter = crater.diameter;
    const double depth = crater.depth / diameter;
    if (const auto* ft =
            dynamic_cast<const FassettThomsonProfile*>(&FreshShape(profile, diameter))) {
        CheckFassettThomson(*ft, crater, fresh);
    }
    if (const auto* diffused = dynamic_cast<const DiffusedProfile*>(&profile)) {
        return DiffusedState(*diffused, crater, fresh, depth);
    }
    CraterMorphometry current{depth, 0.0};
    if (!std::isnan(crater.rimHeight)) {
        current.rimHeightRatio = crater.rimHeight / diameter;
    } else {
        const double kept =
            fresh.depthRatio > 0.0 ? std::clamp(depth / fresh.depthRatio, 0.0, 1.0) : 1.0;
        current.rimHeightRatio = fresh.rimHeightRatio * kept;
    }
    fresh.depthRatio = std::fmax(fresh.depthRatio, current.depthRatio);
    fresh.rimHeightRatio = std::fmax(fresh.rimHeightRatio, current.rimHeightRatio);
    return {fresh, current, 0.0};
}

CraterState ResolvedCraterState(const CraterShapeModels& models, const Crater& crater,
                                double rate) {
    return ResolvedCraterState(*models.profile, *models.degradation, crater, rate);
}

}  // namespace terraingen
