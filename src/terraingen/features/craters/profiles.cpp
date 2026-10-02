#include "terraingen/features/craters/profiles.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

namespace {

double Interior(double x) {
    return -0.228809953 + x * (0.227533882 + x * (0.083116795 - x * 0.039499407));
}

double Exterior(double x) {
    return 0.188253307 + x * (-0.187050452 + x * (0.01844746 + x * 0.01505647));
}

}  // namespace

double FassettThomsonProfile::Height(double distance, double diameter,
                                     const CraterState& state) const {
    const double x = distance / (0.5 * diameter);
    if (x > 1.5) {
        return 0.0;
    }
    double h = 0.0;
    switch (rim_) {
    case FassettThomsonRim::Original:
    case FassettThomsonRim::AtOneRadius: {
        const double rim = rim_ == FassettThomsonRim::Original ? 0.98 : 1.0;
        if (x <= 0.2) {
            h = -0.181;
        } else if (x <= rim) {
            h = Interior(x);
        } else {
            h = Exterior(x);
        }
        break;
    }
    case FassettThomsonRim::Plateau:
        if (x <= 0.98) {
            h = Interior(x);
        } else if (x <= 1.02) {
            h = kFreshRimHeightRatio;
        } else {
            h = Exterior(x);
        }
        break;
    }
    return std::fmax(h, kFreshRimHeightRatio - state.current.depthRatio) * diameter;
}

double MartinParkesDunstanProfile::Height(double distance, double diameter,
                                          const CraterState& state) const {
    const double depth = state.current.depthRatio * diameter;
    const double rim = state.current.rimHeightRatio * diameter;
    const double freshRim = state.fresh.rimHeightRatio * diameter;
    if (!(depth > 0.0 && rim >= 0.0 && rim <= freshRim)) {
        throw std::invalid_argument(
            "Crater state needs a positive depth and 0 <= rim height <= fresh rim height");
    }
    const double radius = 0.5 * diameter;
    const double x = distance / radius - 1.0;
    if (x >= 1.0) {
        return 0.0;
    }
    const double k = freshRim - rim + depth;
    const double alpha = (rim - freshRim) / k;
    if (x <= alpha) {
        return (k * x + 2.0 * k) * x + freshRim;
    }
    if (x <= 0.0) {
        return k * (alpha + 1.0) / alpha * x * x + rim;
    }
    const double ejectaScale = 0.14 * std::pow(radius, 0.74);
    const double offset = rim - ejectaScale;
    const double gradient = -ejectaScale / 8.0 - offset;
    const auto ejecta = [&](double at) {
        return ejectaScale / ((at + 1.0) * (at + 1.0) * (at + 1.0)) + offset + gradient * at;
    };
    const double beta = std::fmin(1.0, 1.5 * (freshRim - rim) / k);
    if (x <= beta) {
        const double a = (rim - ejecta(beta)) / (2.0 * beta * beta * beta);
        return (a * x - 3.0 * a * beta) * x * x + rim;
    }
    return ejecta(x);
}

}  // namespace terraingen
