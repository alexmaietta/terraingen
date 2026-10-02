#include "terraingen/distributions/size_frequency.h"

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

SfdKind
CommonKind(const std::vector<std::shared_ptr<const SizeFrequencyDistribution>>& distributions) {
    if (distributions.empty() ||
        std::any_of(distributions.begin(), distributions.end(),
                    [](const auto& distribution) { return !distribution; })) {
        throw std::invalid_argument("A minimum needs at least one distribution and no nulls");
    }
    const SfdKind kind = distributions.front()->Kind();
    if (std::any_of(distributions.begin(), distributions.end(),
                    [kind](const auto& distribution) { return distribution->Kind() != kind; })) {
        throw std::invalid_argument("A minimum needs distributions of one kind");
    }
    return kind;
}

DiameterRange IntersectRanges(
    const std::vector<std::shared_ptr<const SizeFrequencyDistribution>>& distributions) {
    CommonKind(distributions);
    DiameterRange range;
    for (const auto& distribution : distributions) {
        range.min = std::fmax(range.min, distribution->ValidRange().min);
        range.max = std::fmin(range.max, distribution->ValidRange().max);
    }
    if (!(range.max > range.min)) {
        throw std::invalid_argument("The distributions' valid ranges do not overlap");
    }
    return range;
}

DiameterRange TableRange(const std::vector<double>& diameters,
                         const std::vector<double>& cumulative, double diameterUnit,
                         double areaUnit) {
    if (diameters.size() < 2 || diameters.size() != cumulative.size()) {
        throw std::invalid_argument("A table needs at least two diameters with one value each");
    }
    if (!PositiveFinite(diameterUnit) || !PositiveFinite(areaUnit)) {
        throw std::invalid_argument("Diameter and area units must be positive and finite");
    }
    for (std::size_t i = 0; i < diameters.size(); ++i) {
        if (!PositiveFinite(diameters[i]) || !PositiveFinite(cumulative[i])) {
            throw std::invalid_argument("Table diameters and values must be positive and finite");
        }
        if (i > 0 && !(diameters[i] > diameters[i - 1])) {
            throw std::invalid_argument("Table diameters must strictly increase");
        }
        if (i > 0 && cumulative[i] > cumulative[i - 1]) {
            throw std::invalid_argument("A cumulative distribution cannot increase with diameter");
        }
    }
    return DiameterRange{diameters.front() * diameterUnit, diameters.back() * diameterUnit};
}

bool Covers(const SizeFrequencyDistribution& distribution, double low, double high) {
    return distribution.AllowsExtrapolation() ||
           (distribution.ValidRange().min <= low && distribution.ValidRange().max >= high);
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

TabulatedSfd::TabulatedSfd(std::vector<double> diameters, std::vector<double> cumulative,
                           SfdKind kind, double diameterUnit, double areaUnit)
    : SizeFrequencyDistribution(kind, TableRange(diameters, cumulative, diameterUnit, areaUnit)),
      diameters_(std::move(diameters)), cumulative_(std::move(cumulative)) {
    for (std::size_t i = 0; i < diameters_.size(); ++i) {
        diameters_[i] *= diameterUnit;
        cumulative_[i] /= areaUnit;
        if (i > 0) {
            slopes_.push_back(std::log(cumulative_[i] / cumulative_[i - 1]) /
                              std::log(diameters_[i] / diameters_[i - 1]));
        }
    }
}

std::size_t TabulatedSfd::IntervalAt(double diameter) const {
    const auto upper = std::upper_bound(diameters_.begin(), diameters_.end(), diameter);
    const auto index = static_cast<std::size_t>(upper - diameters_.begin());
    return std::clamp<std::size_t>(index, 1, diameters_.size() - 1) - 1;
}

double TabulatedSfd::CumulativeAt(double diameter) const {
    const std::size_t i = IntervalAt(diameter);
    return cumulative_[i] * std::pow(diameter / diameters_[i], slopes_[i]);
}

double TabulatedSfd::IncrementalAt(double diameter) const {
    return -slopes_[IntervalAt(diameter)] * CumulativeAt(diameter) / diameter;
}

double TabulatedSfd::InvertCumulative(double target, double /*dMin*/, double /*dMax*/) const {
    const auto above = std::partition_point(cumulative_.begin(), cumulative_.end(),
                                            [target](double value) { return value > target; });
    const auto index = static_cast<std::size_t>(above - cumulative_.begin());
    const std::size_t i = std::clamp<std::size_t>(index, 1, cumulative_.size() - 1) - 1;
    return diameters_[i] * std::pow(target / cumulative_[i], 1.0 / slopes_[i]);
}

SplicedSfd::SplicedSfd(std::vector<SfdPiece> pieces, DiameterRange range, double tolerance)
    : SizeFrequencyDistribution(pieces.empty() || !pieces.front().distribution
                                    ? SfdKind::Density
                                    : pieces.front().distribution->Kind(),
                                range),
      pieces_(std::move(pieces)) {
    if (pieces_.empty()) {
        throw std::invalid_argument("A spliced distribution needs at least one piece");
    }
    if (pieces_.front().start != range.min) {
        throw std::invalid_argument("The first piece must start at the range minimum");
    }
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        const SfdPiece& piece = pieces_[i];
        if (!piece.distribution || piece.distribution->Kind() != Kind()) {
            throw std::invalid_argument("Spliced pieces must be distributions of one kind");
        }
        const double end = i + 1 < pieces_.size() ? pieces_[i + 1].start : range.max;
        if (i > 0 && !(std::isfinite(piece.start) && piece.start > pieces_[i - 1].start &&
                       piece.start < range.max)) {
            throw std::invalid_argument("Piece starts must increase within the range");
        }
        if (!Covers(*piece.distribution, piece.start, end)) {
            throw std::invalid_argument("Each piece must be valid over its diameter interval");
        }
        if (i == 0) {
            continue;
        }
        const double below = CumulativeOf(*pieces_[i - 1].distribution, piece.start);
        const double above = CumulativeOf(*piece.distribution, piece.start);
        if (above > below * (1.0 + tolerance)) {
            throw std::invalid_argument("A cumulative distribution cannot rise at a splice");
        }
        if (above < below * (1.0 - tolerance)) {
            throw std::invalid_argument("Pieces are discontinuous at " +
                                        std::to_string(piece.start) + " m");
        }
    }
}

std::size_t SplicedSfd::PieceAt(double diameter) const {
    std::size_t index = 0;
    while (index + 1 < pieces_.size() && pieces_[index + 1].start < diameter) {
        ++index;
    }
    return index;
}

double SplicedSfd::CumulativeAt(double diameter) const {
    return CumulativeOf(*pieces_[PieceAt(diameter)].distribution, diameter);
}

double SplicedSfd::IncrementalAt(double diameter) const {
    return IncrementalOf(*pieces_[PieceAt(diameter)].distribution, diameter);
}

double SplicedSfd::InvertCumulative(double target, double dMin, double dMax) const {
    std::size_t index = PieceAt(dMin);
    while (index + 1 < pieces_.size()) {
        const double high = std::fmin(dMax, pieces_[index + 1].start);
        if (target >= CumulativeOf(*pieces_[index].distribution, high)) {
            break;
        }
        ++index;
    }
    const SizeFrequencyDistribution& piece = *pieces_[index].distribution;
    const double low = std::fmax(dMin, pieces_[index].start);
    const double high =
        index + 1 < pieces_.size() ? std::fmin(dMax, pieces_[index + 1].start) : dMax;
    if (target >= CumulativeOf(piece, low)) {
        return low;
    }
    return InvertCumulativeOf(piece, target, low, high);
}

MinimumSfd::MinimumSfd(std::vector<std::shared_ptr<const SizeFrequencyDistribution>> distributions)
    : SizeFrequencyDistribution(CommonKind(distributions), IntersectRanges(distributions)),
      distributions_(std::move(distributions)) {}

const SizeFrequencyDistribution& MinimumSfd::LowestAt(double diameter) const {
    const SizeFrequencyDistribution* lowest = distributions_.front().get();
    double value = CumulativeOf(*lowest, diameter);
    for (const auto& distribution : distributions_) {
        const double candidate = CumulativeOf(*distribution, diameter);
        if (candidate < value) {
            value = candidate;
            lowest = distribution.get();
        }
    }
    return *lowest;
}

double MinimumSfd::CumulativeAt(double diameter) const {
    return CumulativeOf(LowestAt(diameter), diameter);
}

double MinimumSfd::IncrementalAt(double diameter) const {
    return IncrementalOf(LowestAt(diameter), diameter);
}

double PowerLawCoefficientFromKilometres(double coefficient, double exponent) {
    return coefficient * std::pow(1000.0, exponent) / 1e6;
}

}  // namespace terraingen
