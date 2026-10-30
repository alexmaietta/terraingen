// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/albedo/crater_albedo_options.h"

#include "albedo/bands.h"
#include "terraingen/albedo/albedo_fields.h"
#include "terraingen/layers/craters/secondary_model.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

double RayRetention::Myr(double diameter) const {
    double myr = largeMyr;
    if (diameter <= smallDiameter) {
        myr = smallMyr;
    } else if (diameter < midDiameter) {
        const double t = std::log(diameter / smallDiameter) / std::log(midDiameter / smallDiameter);
        myr = smallMyr * std::pow(midMyr / smallMyr, t);
    } else if (diameter <= largeDiameter) {
        myr = midMyr;
    }
    return scale * myr;
}

void ValidateRayRetention(const RayRetention& retention) {
    CheckRanges(retention);
    if (!(retention.smallDiameter < retention.midDiameter &&
          retention.midDiameter <= retention.largeDiameter)) {
        throw std::invalid_argument("Ray retention diameters must be increasing");
    }
}

double ObliqueImpacts::MaxStretch() const {
    if (!enabled) {
        return 1.0;
    }
    const double ratio = downrangeSpeedRatio.Highest();
    const double a = (ratio - 1.0) / (ratio + 1.0);
    return (1.0 + a) * (1.0 + a) / (1.0 + 0.5 * a * a);
}

void ValidateObliqueImpacts(const ObliqueImpacts& oblique) {
    CheckRanges(oblique);
    ValidateValueDistribution(oblique.downrangeSpeedRatio);
    if (oblique.downrangeSpeedRatio.Lowest() < 1.0 ||
        !std::isfinite(oblique.downrangeSpeedRatio.Highest())) {
        throw std::invalid_argument("The downrange speed ratio must be at least 1 and bounded");
    }
    if (!(oblique.butterflyDegrees <= oblique.forbiddenDegrees &&
          oblique.forbiddenDegrees <= oblique.asymmetricDegrees)) {
        throw std::invalid_argument(
            "Oblique impact angles must satisfy butterfly <= forbidden <= asymmetric");
    }
}

double CraterRays::MedianLength(double diameter) const {
    return ElliottMedianRayLength(diameter, lengthCoefficient, lengthExponent);
}

void ValidateCraterRays(const CraterRays& rays) {
    CheckRanges(rays);
    ValidateValueDistribution(rays.count);
    ValidateValueDistribution(rays.lengthSpread);
    ValidateValueDistribution(rays.widthDegrees);
    if (rays.count.Lowest() < 0.0 || rays.count.Highest() > 64.0) {
        throw std::invalid_argument("Craters must have 0 to 64 rays");
    }
    if (!(rays.widthDegrees.Lowest() > 0.0 && rays.widthDegrees.Highest() < 180.0)) {
        throw std::invalid_argument("Ray widths must be in (0, 180) degrees");
    }
    ValidateObliqueImpacts(rays.oblique);
    if (rays.layout) {
        ValidateRayLayout(*rays.layout);
    }
}

void ValidateCraterAlbedoTexture(const CraterAlbedoTexture& texture) {
    CheckRanges(texture);
}

void ValidateReflectanceZone(const ReflectanceZone& zone) {
    CheckRanges(zone);
    ValidateRelativeChange(zone.change, "A reflectance zone's change");
    ValidateValueDistribution(zone.scale);
    if (!(zone.scale.Lowest() > 0.0 && std::isfinite(zone.scale.Highest()))) {
        throw std::invalid_argument("A reflectance zone needs a positive, bounded reach scale");
    }
}

double CraterReflectanceZones::MaxReach(double diameter) const {
    double reach = 0.0;
    for (const ReflectanceZone* zone : All()) {
        reach = std::fmax(reach, zone->coefficient * std::pow(diameter, zone->exponent) *
                                     zone->scale.Highest());
    }
    return reach;
}

void ValidateCraterReflectanceZones(const CraterReflectanceZones& zones) {
    CheckRanges(zones);
    for (const ReflectanceZone* zone : zones.All()) {
        ValidateReflectanceZone(*zone);
    }
}

void ValidateCraterAlbedoOptions(const CraterAlbedoOptions& options) {
    CheckRanges(options);
    ValidateCraterReflectanceZones(options.zones);
    ValidateCraterRays(options.rays);
    ValidateRayRetention(options.retention);
    ValidateResolutionFade(options.fade);
    ValidateCraterSelection(options.selection, "Crater albedo");
    if (!(options.maxMaturity >= options.backgroundMaturity)) {
        throw std::invalid_argument("The maturity cap must be at least the background maturity");
    }
}

}  // namespace terraingen
