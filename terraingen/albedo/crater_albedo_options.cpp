#include "terraingen/albedo/crater_albedo_options.h"

#include "terraingen/albedo/bands.h"
#include "terraingen/layers/craters/secondary_model.h"
#include "terraingen/util/checks.h"

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
    if (!(IsPositive(retention.smallDiameter) && IsPositive(retention.midDiameter) &&
          IsPositive(retention.largeDiameter) && retention.smallDiameter < retention.midDiameter &&
          retention.midDiameter <= retention.largeDiameter)) {
        throw std::invalid_argument("Ray retention diameters must be positive and increasing");
    }
    if (!(IsPositive(retention.smallMyr) && IsPositive(retention.midMyr) &&
          IsPositive(retention.largeMyr) && IsPositive(retention.scale))) {
        throw std::invalid_argument("Ray retention times and their scale must be positive");
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
    ValidateValueDistribution(oblique.downrangeSpeedRatio);
    if (oblique.downrangeSpeedRatio.Lowest() < 1.0 ||
        !std::isfinite(oblique.downrangeSpeedRatio.Highest())) {
        throw std::invalid_argument("The downrange speed ratio must be at least 1 and bounded");
    }
    if (!(oblique.butterflyDegrees >= 0.0 && oblique.butterflyDegrees <= oblique.forbiddenDegrees &&
          oblique.forbiddenDegrees <= oblique.asymmetricDegrees &&
          oblique.asymmetricDegrees <= 90.0)) {
        throw std::invalid_argument(
            "Oblique impact angles must satisfy 0 <= butterfly <= forbidden <= asymmetric <= 90");
    }
    if (!(oblique.zoneHalfWidthDegrees >= 0.0 && oblique.zoneHalfWidthDegrees <= 180.0)) {
        throw std::invalid_argument("A forbidden zone's half width must be in [0, 180] degrees");
    }
    if (oblique.heading && !std::isfinite(*oblique.heading)) {
        throw std::invalid_argument("An impact heading must be finite");
    }
}

double CraterRays::MedianLength(double diameter) const {
    return ElliottMedianRayLength(diameter, lengthCoefficient, lengthExponent);
}

void ValidateCraterRays(const CraterRays& rays) {
    ValidateValueDistribution(rays.count);
    ValidateValueDistribution(rays.lengthSpread);
    ValidateValueDistribution(rays.widthDegrees);
    if (rays.count.Lowest() < 0.0 || rays.count.Highest() > 64.0) {
        throw std::invalid_argument("Craters must have 0 to 64 rays");
    }
    if (!(rays.widthDegrees.Lowest() > 0.0 && rays.widthDegrees.Highest() < 180.0)) {
        throw std::invalid_argument("Ray widths must be in (0, 180) degrees");
    }
    if (!(IsPositive(rays.lengthCoefficient) && IsPositive(rays.lengthExponent))) {
        throw std::invalid_argument("The ray length law needs a positive coefficient and exponent");
    }
    if (!IsNonNegative(rays.excess)) {
        throw std::invalid_argument("The ray maturity excess must not be negative");
    }
    if (!(IsInUnitInterval(rays.patchiness) && IsPositive(rays.patchRadii))) {
        throw std::invalid_argument(
            "Ray patchiness must be in [0, 1] with a positive patch wavelength");
    }
    ValidateObliqueImpacts(rays.oblique);
    if (rays.layout) {
        ValidateRayLayout(*rays.layout);
    }
}

void ValidateCraterAlbedoTexture(const CraterAlbedoTexture& texture) {
    if (!(IsInUnitInterval(texture.streaks) && IsInUnitInterval(texture.floorShare))) {
        throw std::invalid_argument("Crater albedo streaks and floor share must be in [0, 1]");
    }
    if (!(texture.lobes >= 0.0 && texture.lobes <= 0.9 && texture.lobeCount >= 1 &&
          texture.lobeCount <= 64)) {
        throw std::invalid_argument(
            "Crater albedo lobes must be in [0, 0.9] with a lobe count of 1 to 64");
    }
    RequirePositive(texture.streakWidthDegrees, "Crater albedo streak width");
    RequirePositive(texture.streakLengthRadii, "Crater albedo streak length");
    RequireNonNegative(texture.continuousRadii, "Crater albedo continuous ejecta reach");
}

void ValidateReflectanceZone(const ReflectanceZone& zone) {
    RequireUnitInterval(zone.share, "The share of craters with a reflectance zone");
    ValidateRelativeChange(zone.change, "A reflectance zone's change");
    ValidateValueDistribution(zone.scale);
    if (!(IsPositive(zone.coefficient) && IsPositive(zone.exponent) && zone.scale.Lowest() > 0.0 &&
          std::isfinite(zone.scale.Highest()))) {
        throw std::invalid_argument(
            "A reflectance zone needs a positive reach law and a positive, bounded reach scale");
    }
    if (!(zone.raggedness >= 0.0 && zone.raggedness < 1.0 && zone.feather > 0.0 &&
          zone.feather <= 1.0)) {
        throw std::invalid_argument(
            "A reflectance zone's raggedness must be in [0, 1) and its feather in (0, 1]");
    }
    RequirePositive(zone.fingerWidthDegrees, "A reflectance zone's finger width");
    if (zone.retentionMyr) {
        RequirePositive(*zone.retentionMyr, "A reflectance zone's retention time");
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
    for (const ReflectanceZone* zone : zones.All()) {
        ValidateReflectanceZone(*zone);
    }
    RequirePositive(zones.maxDiameter, "The largest crater with reflectance zones");
    RequirePositive(zones.excavationRatio, "The excavation depth ratio");
    RequireNonNegative(zones.matureDepth, "The mature regolith depth");
    const ZoneShadows& s = zones.shadows;
    if (!(s.jetAngleDegrees > 0.0 && s.jetAngleDegrees < 90.0 && IsPositive(s.depth) &&
          s.azimuths >= 8 && s.azimuths <= 8192 && s.radialSamples >= 8 &&
          s.radialSamples <= 8192)) {
        throw std::invalid_argument("Zone shadows need a jet angle in (0, 90) degrees, a positive "
                                    "depth and 8 to 8192 azimuths and samples");
    }
}

void ValidateCraterAlbedoOptions(const CraterAlbedoOptions& options) {
    ValidateCraterReflectanceZones(options.zones);
    ValidateCraterAlbedoTexture(options.texture);
    ValidateCraterRays(options.rays);
    ValidateRayRetention(options.retention);
    ValidateResolutionFade(options.fade);
    ValidateCraterSelection(options.selection, "Crater albedo");
    RequireUnitInterval(options.share, "The share of craters with bright material");
    if (!(IsPositive(options.backgroundMaturity) && std::isfinite(options.maxMaturity) &&
          options.maxMaturity >= options.backgroundMaturity)) {
        throw std::invalid_argument(
            "The background maturity must be positive and the cap at least as high");
    }
    if (!(IsNonNegative(options.interiorExcess) && IsNonNegative(options.ejectaExcess))) {
        throw std::invalid_argument("Crater maturity excesses must not be negative");
    }
    if (!(IsPositive(options.ejectaExponent) && std::isfinite(options.ejectaReachRadii) &&
          options.ejectaReachRadii > 1.0 && IsPositive(options.fadeExponent) &&
          std::isfinite(options.originReflectance))) {
        throw std::invalid_argument("Crater albedo needs a positive ejecta exponent and fade "
                                    "exponent and a reach over 1 radius");
    }
}

}  // namespace terraingen
