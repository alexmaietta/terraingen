#include "terraingen/bodies/moon/rocks.h"

#include <array>
#include <cmath>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// A point of a digitized curve: log10 of the diameter in metres and of N per m^2.
struct LogPoint {
    double logDiameter;
    double logCumulative;
};

constexpr std::array<LogPoint, 4> kIntercraterFine{
    {{-4.5, 7.885}, {-3.839, 6.154}, {-3.194, 4.416}, {-2.749, 3.304}}};
constexpr std::array<LogPoint, 11> kIntercraterMaximum{{{-2.293, 2.489},
                                                        {-1.844, 1.713},
                                                        {-1.409, 0.951},
                                                        {-0.973, 0.164},
                                                        {-0.744, -0.230},
                                                        {-0.515, -0.819},
                                                        {-0.286, -1.489},
                                                        {-0.057, -2.153},
                                                        {0.358, -3.469},
                                                        {0.768, -4.777},
                                                        {0.973, -5.426}}};
constexpr std::array<LogPoint, 12> kIntercraterNominal{{{-2.293, 2.136},
                                                        {-1.844, 1.220},
                                                        {-1.409, 0.287},
                                                        {-0.973, -0.615},
                                                        {-0.744, -1.063},
                                                        {-0.515, -1.515},
                                                        {-0.286, -1.933},
                                                        {-0.057, -2.455},
                                                        {0.154, -3.184},
                                                        {0.358, -3.769},
                                                        {0.563, -4.481},
                                                        {0.768, -5.196}}};
constexpr std::array<LogPoint, 10> kIntercraterMinimum{{{-2.293, 2.136},
                                                        {-1.844, 1.078},
                                                        {-1.409, -0.044},
                                                        {-0.973, -1.174},
                                                        {-0.286, -2.825},
                                                        {-0.057, -3.397},
                                                        {0.154, -3.874},
                                                        {0.358, -4.447},
                                                        {0.563, -5.052},
                                                        {0.768, -5.522}}};
constexpr std::array<LogPoint, 11> kCraterMaximum{{{0.152, -2.022},
                                                   {0.286, -2.299},
                                                   {0.419, -2.608},
                                                   {0.552, -2.927},
                                                   {0.686, -3.271},
                                                   {0.819, -3.651},
                                                   {0.952, -4.093},
                                                   {1.082, -4.608},
                                                   {1.211, -5.170},
                                                   {1.339, -5.748},
                                                   {1.382, -5.940}}};
constexpr std::array<LogPoint, 11> kCraterNominal{{{-0.071, -2.282},
                                                   {0.063, -2.574},
                                                   {0.197, -2.887},
                                                   {0.330, -3.239},
                                                   {0.463, -3.603},
                                                   {0.597, -4.028},
                                                   {0.730, -4.436},
                                                   {0.863, -4.841},
                                                   {0.997, -5.190},
                                                   {1.125, -5.572},
                                                   {1.254, -5.935}}};
constexpr std::array<LogPoint, 8> kCraterMinimum{{{-0.071, -2.606},
                                                  {0.063, -3.050},
                                                  {0.197, -3.547},
                                                  {0.330, -3.998},
                                                  {0.463, -4.497},
                                                  {0.597, -4.968},
                                                  {0.730, -5.422},
                                                  {0.863, -5.889}}};

std::shared_ptr<TabulatedSfd> Table(std::span<const LogPoint> first,
                                    std::span<const LogPoint> second = {}) {
    std::vector<double> diameters;
    std::vector<double> cumulative;
    for (const auto points : {first, second}) {
        for (const LogPoint& point : points) {
            diameters.push_back(std::pow(10.0, point.logDiameter));
            cumulative.push_back(std::pow(10.0, point.logCumulative));
        }
    }
    return std::make_shared<TabulatedSfd>(std::move(diameters), std::move(cumulative));
}

}  // namespace

std::shared_ptr<TabulatedSfd> Sp8023IntercraterBlocks(RockModelCurve curve) {
    switch (curve) {
    case RockModelCurve::Minimum:
        return Table(kIntercraterFine, kIntercraterMinimum);
    case RockModelCurve::Nominal:
        return Table(kIntercraterFine, kIntercraterNominal);
    case RockModelCurve::Maximum:
        return Table(kIntercraterFine, kIntercraterMaximum);
    }
    throw std::invalid_argument("Unknown rock model curve");
}

std::shared_ptr<TabulatedSfd> Sp8023CraterBlocks(RockModelCurve curve) {
    switch (curve) {
    case RockModelCurve::Minimum:
        return Table(kCraterMinimum);
    case RockModelCurve::Nominal:
        return Table(kCraterNominal);
    case RockModelCurve::Maximum:
        return Table(kCraterMaximum);
    }
    throw std::invalid_argument("Unknown rock model curve");
}

std::shared_ptr<PowerLawSfd> SynthterrainRockDistribution(SynthterrainRocks rocks) {
    switch (rocks) {
    case SynthterrainRocks::InterCrater:
        return std::make_shared<PowerLawSfd>(0.0001, 1.75457);
    case SynthterrainRocks::Haworth:
        return std::make_shared<PowerLawSfd>(0.002, 2.6607);
    case SynthterrainRocks::ViperSpecification:
        return std::make_shared<PowerLawSfd>(0.0003, 2.482);
    }
    throw std::invalid_argument("Unknown synthterrain rock distribution");
}

}  // namespace terraingen
