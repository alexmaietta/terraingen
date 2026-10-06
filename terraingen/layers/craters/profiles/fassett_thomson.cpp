#include "terraingen/layers/craters/profiles/fassett_thomson.h"

#include <cmath>

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

}  // namespace terraingen
