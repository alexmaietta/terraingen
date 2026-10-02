#include "terraingen/models/size_frequency.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace terraingen {

namespace {

bool PositiveFinite(double value) {
    return std::isfinite(value) && value > 0.0;
}

void CheckRange(const DiameterRange& range) {
    if (!(std::isfinite(range.min) && range.min >= 0.0 && range.max > range.min) ||
        std::isnan(range.max)) {
        throw std::invalid_argument("Diameter range must satisfy 0 <= min < max");
    }
}

void CheckInterval(double dMin, double dMax) {
    if (!(PositiveFinite(dMin) && dMax > dMin) || std::isnan(dMax)) {
        throw std::invalid_argument("Diameter interval must satisfy 0 < dMin < dMax");
    }
}

}  // namespace

SizeFrequencyDistribution::SizeFrequencyDistribution(SfdKind kind, DiameterRange range)
    : kind_(kind), range_(range) {
    CheckRange(range_);
}

void SizeFrequencyDistribution::CheckDiameter(double diameter) const {
    if (!PositiveFinite(diameter)) {
        throw std::domain_error("Diameter must be positive and finite");
    }
    if (!extrapolate_ && !range_.Contains(diameter)) {
        throw std::domain_error("Diameter " + std::to_string(diameter) +
                                " m is outside the distribution's valid range");
    }
}

double SizeFrequencyDistribution::Cumulative(double diameter) const {
    CheckDiameter(diameter);
    return CumulativeAt(diameter);
}

double SizeFrequencyDistribution::Incremental(double diameter) const {
    CheckDiameter(diameter);
    return IncrementalAt(diameter);
}

double SizeFrequencyDistribution::IncrementalAt(double diameter) const {
    constexpr double kStep = 1e-5;
    const double low = diameter * std::exp(-kStep);
    const double high = diameter * std::exp(kStep);
    return (CumulativeAt(low) - CumulativeAt(high)) / (high - low);
}

double SizeFrequencyDistribution::InvertCumulative(double target, double dMin, double dMax) const {
    double low = std::log(dMin);
    double high = std::isinf(dMax) ? low + 1.0 : std::log(dMax);
    while (std::isinf(dMax) && CumulativeAt(std::exp(high)) > target) {
        high += 1.0;
    }
    for (int i = 0; i < 200 && high - low > 1e-15 * std::fmax(1.0, std::fabs(high)); ++i) {
        const double middle = 0.5 * (low + high);
        if (CumulativeAt(std::exp(middle)) > target) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return std::exp(0.5 * (low + high));
}

double SizeFrequencyDistribution::ExpectedCount(double area, double dMin, double dMax) const {
    CheckInterval(dMin, dMax);
    if (!(std::isfinite(area) && area >= 0.0)) {
        throw std::invalid_argument("Area must be finite and not negative");
    }
    double upper = 0.0;
    if (std::isinf(dMax)) {
        if (!extrapolate_ && !std::isinf(range_.max)) {
            throw std::domain_error("An infinite dMax needs an unbounded valid range");
        }
    } else {
        upper = Cumulative(dMax);
    }
    return area * (Cumulative(dMin) - upper);
}

double SizeFrequencyDistribution::SampleDiameter(RandomStream& random, double dMin,
                                                 double dMax) const {
    const double count = ExpectedCount(1.0, dMin, dMax);
    if (!(count > 0.0)) {
        throw std::invalid_argument("The distribution has no mass in the diameter interval");
    }
    const double top = Cumulative(dMin);
    const double target = top - random.NextDouble() * count;
    return std::clamp(InvertCumulative(target, dMin, dMax), dMin, dMax);
}

PowerLaw::PowerLaw(double coefficient, double exponent, SfdKind kind, DiameterRange range)
    : SizeFrequencyDistribution(kind, range), coefficient_(coefficient), exponent_(exponent) {
    if (!PositiveFinite(coefficient_) || !PositiveFinite(exponent_)) {
        throw std::invalid_argument(
            "Power law coefficient and exponent must be positive and finite");
    }
}

double PowerLaw::CumulativeAt(double diameter) const {
    return coefficient_ * std::pow(diameter, -exponent_);
}

double PowerLaw::IncrementalAt(double diameter) const {
    return coefficient_ * exponent_ * std::pow(diameter, -exponent_ - 1.0);
}

double PowerLaw::InvertCumulative(double target, double /*dMin*/, double /*dMax*/) const {
    return std::pow(target / coefficient_, -1.0 / exponent_);
}

PiecewisePowerLaw::PiecewisePowerLaw(std::vector<PowerLawSegment> segments, SfdKind kind,
                                     DiameterRange range, bool allowDiscontinuities,
                                     double tolerance)
    : SizeFrequencyDistribution(kind, range), segments_(std::move(segments)) {
    if (segments_.empty()) {
        throw std::invalid_argument("A piecewise power law needs at least one segment");
    }
    if (segments_.front().start != range.min) {
        throw std::invalid_argument("The first segment must start at the range minimum");
    }
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        const PowerLawSegment& segment = segments_[i];
        if (!PositiveFinite(segment.coefficient) || !PositiveFinite(segment.exponent)) {
            throw std::invalid_argument("Segment coefficients and exponents must be positive");
        }
        if (i == 0) {
            continue;
        }
        if (!(std::isfinite(segment.start) && segment.start > segments_[i - 1].start &&
              segment.start < range.max)) {
            throw std::invalid_argument("Segment starts must increase within the range");
        }
        const PowerLawSegment& previous = segments_[i - 1];
        const double below = previous.coefficient * std::pow(segment.start, -previous.exponent);
        const double above = segment.coefficient * std::pow(segment.start, -segment.exponent);
        if (above > below * (1.0 + tolerance)) {
            throw std::invalid_argument("A cumulative distribution cannot rise at a segment join");
        }
        if (!allowDiscontinuities && above < below * (1.0 - tolerance)) {
            throw std::invalid_argument("Segments are discontinuous at " +
                                        std::to_string(segment.start) + " m");
        }
    }
}

const PowerLawSegment& PiecewisePowerLaw::SegmentAt(double diameter) const {
    std::size_t index = 0;
    while (index + 1 < segments_.size() && segments_[index + 1].start < diameter) {
        ++index;
    }
    return segments_[index];
}

double PiecewisePowerLaw::CumulativeAt(double diameter) const {
    const PowerLawSegment& segment = SegmentAt(diameter);
    return segment.coefficient * std::pow(diameter, -segment.exponent);
}

double PiecewisePowerLaw::IncrementalAt(double diameter) const {
    const PowerLawSegment& segment = SegmentAt(diameter);
    return segment.coefficient * segment.exponent * std::pow(diameter, -segment.exponent - 1.0);
}

double PiecewisePowerLaw::InvertCumulative(double target, double dMin, double dMax) const {
    std::size_t index = 0;
    while (index + 1 < segments_.size()) {
        const PowerLawSegment& segment = segments_[index];
        const double high = std::fmin(dMax, segments_[index + 1].start);
        if (std::fmax(dMin, segment.start) < high &&
            target >= segment.coefficient * std::pow(high, -segment.exponent)) {
            break;
        }
        ++index;
    }
    const PowerLawSegment& segment = segments_[index];
    const double low = std::fmax(dMin, segment.start);
    if (target > segment.coefficient * std::pow(low, -segment.exponent)) {
        return low;
    }
    return std::pow(target / segment.coefficient, -1.0 / segment.exponent);
}

LogPolynomial::LogPolynomial(std::vector<double> coefficients, DiameterRange range, SfdKind kind,
                             double diameterUnit, double areaUnit)
    : SizeFrequencyDistribution(kind, range), coefficients_(std::move(coefficients)),
      diameterUnit_(diameterUnit), areaUnit_(areaUnit) {
    if (coefficients_.empty() || !std::all_of(coefficients_.begin(), coefficients_.end(),
                                              [](double value) { return std::isfinite(value); })) {
        throw std::invalid_argument("A log polynomial needs finite coefficients");
    }
    if (!PositiveFinite(diameterUnit_) || !PositiveFinite(areaUnit_)) {
        throw std::invalid_argument("Diameter and area units must be positive and finite");
    }
    if (!(range.min > 0.0 && std::isfinite(range.max))) {
        throw std::invalid_argument(
            "A log polynomial needs a finite range with a positive minimum");
    }
    constexpr int kSamples = 4096;
    const double low = std::log10(range.min / diameterUnit_);
    const double high = std::log10(range.max / diameterUnit_);
    for (int i = 0; i <= kSamples; ++i) {
        if (Derivative(low + (high - low) * i / kSamples) > 0.0) {
            throw std::invalid_argument("A cumulative distribution cannot increase with diameter");
        }
    }
}

double LogPolynomial::Polynomial(double x) const {
    double sum = 0.0;
    for (auto it = coefficients_.rbegin(); it != coefficients_.rend(); ++it) {
        sum = sum * x + *it;
    }
    return sum;
}

double LogPolynomial::Derivative(double x) const {
    double sum = 0.0;
    for (std::size_t i = coefficients_.size() - 1; i > 0; --i) {
        sum = sum * x + static_cast<double>(i) * coefficients_[i];
    }
    return sum;
}

double LogPolynomial::CumulativeAt(double diameter) const {
    return std::pow(10.0, Polynomial(std::log10(diameter / diameterUnit_))) / areaUnit_;
}

double LogPolynomial::IncrementalAt(double diameter) const {
    return -CumulativeAt(diameter) * Derivative(std::log10(diameter / diameterUnit_)) / diameter;
}

ProductionSurface::ProductionSurface(std::shared_ptr<const SizeFrequencyDistribution> production,
                                     double ageGyr)
    : SizeFrequencyDistribution(SfdKind::Density,
                                production ? production->ValidRange() : DiameterRange{}),
      production_(std::move(production)), ageGyr_(ageGyr) {
    if (!production_ || production_->Kind() != SfdKind::ProductionRate) {
        throw std::invalid_argument("A production surface needs a production rate distribution");
    }
    SetAllowExtrapolation(production_->AllowsExtrapolation());
    scale_ = NeukumLunarChronology(ageGyr_) / production_->Cumulative(1000.0);
}

double ProductionSurface::CumulativeAt(double diameter) const {
    return scale_ * CumulativeOf(*production_, diameter);
}

double ProductionSurface::IncrementalAt(double diameter) const {
    return scale_ * IncrementalOf(*production_, diameter);
}

double ProductionSurface::InvertCumulative(double target, double dMin, double dMax) const {
    return InvertCumulativeOf(*production_, target / scale_, dMin, dMax);
}

double PowerLawCoefficientFromKilometres(double coefficient, double exponent) {
    return coefficient * std::pow(1000.0, exponent) / 1e6;
}

PowerLaw TraskEquilibrium() {
    return PowerLaw(std::pow(10.0, -1.1), 2.0);
}

PowerLaw HartmannEquilibrium() {
    return PowerLaw(PowerLawCoefficientFromKilometres(std::pow(10.0, -1.14), 1.83), 1.83);
}

PiecewisePowerLaw MooreDistribution(MooreTerrain terrain) {
    constexpr double kRoundingTolerance = 5e-3;
    switch (terrain) {
    case MooreTerrain::RoughMare:
        return PiecewisePowerLaw({{0.0, 0.1, 2.0}, {100.0, 10.0, 3.0}}, SfdKind::Density,
                                 DiameterRange{}, false, kRoundingTolerance);
    case MooreTerrain::SmoothMare:
        return PiecewisePowerLaw({{0.0, 0.1, 2.0},
                                  {40.0, std::pow(10.0, 0.602), 3.0},
                                  {100.0, std::pow(10.0, -2.038), 1.68},
                                  {200.0, 10.0, 3.0}},
                                 SfdKind::Density, DiameterRange{}, false, kRoundingTolerance);
    case MooreTerrain::Uplands:
        return PiecewisePowerLaw({{0.0, 0.1, 2.0}, {1000.0, 100.0, 3.0}}, SfdKind::Density,
                                 DiameterRange{}, false, kRoundingTolerance);
    default:
        return PiecewisePowerLaw({{0.0, 0.1, 2.0}}, SfdKind::Density, DiameterRange{0.0, 10000.0});
    }
}

PiecewisePowerLaw ViperSpecification() {
    return PiecewisePowerLaw({{0.0, 29174.0 / 1e6, 1.92}, {80.0, 156228.0 / 1e6, 2.389}},
                             SfdKind::Density, DiameterRange{}, true);
}

double NeukumLunarChronology(double ageGyr) {
    if (!(ageGyr >= 0.0 && ageGyr <= kMaxLunarAgeGyr)) {
        throw std::domain_error("Lunar surface age must be between 0 and 4.6 Gyr");
    }
    return (5.44e-14 * std::expm1(6.93 * ageGyr) + 8.38e-4 * ageGyr) / 1e6;
}

LogPolynomial NeukumProductionFunction() {
    return LogPolynomial({-3.076756, -3.557528, 0.781027, 1.021521, -0.156012, -0.444058, 0.019977,
                          0.086850, -0.005874, -0.006809, 8.25e-4, 5.54e-5},
                         DiameterRange{10.0, 300000.0}, SfdKind::ProductionRate, 1000.0, 1e6);
}

}  // namespace terraingen
