/// Crater size-frequency distributions: the built-in lunar and Martian models, and one of your own.
///
/// Prints N(>= D) per square kilometer for the production on surfaces of several ages, the
/// equilibria of Trask and Hartmann, the DSNE lunar surface (production capped at equilibrium),
/// Moore's rough mare and Mars. Then defines a distribution by deriving from
/// `SizeFrequencyDistribution` (a power law that rolls over below a knee), draws craters from it
/// and compares the counts generated with those expected.
///
///     example_distributions_csfd [--output-dir DIR] [--test]
///     terraingen-examples distributions_csfd

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

/// N(>= D) = coefficient * D^-slope above `knee` meters, flattening to D^-shallow below it.
class RolledOverPowerLaw final : public terraingen::SizeFrequencyDistribution {
  public:
    RolledOverPowerLaw(double coefficient, double slope, double shallow, double knee)
        : SizeFrequencyDistribution(terraingen::SfdKind::Density, {1.0, 10000.0}),
          coefficient_(coefficient), slope_(slope), shallow_(shallow), knee_(knee) {}

  protected:
    double CumulativeAt(double diameter) const override {
        if (diameter >= knee_) {
            return coefficient_ * std::pow(diameter, -slope_);
        }
        return coefficient_ * std::pow(knee_, -slope_) * std::pow(diameter / knee_, -shallow_);
    }

  private:
    double coefficient_;
    double slope_;
    double shallow_;
    double knee_;
};

double PerSquareKilometer(const terraingen::SizeFrequencyDistribution& sfd, double diameter) {
    const auto& range = sfd.ValidRange();
    return range.Contains(diameter) ? 1e6 * sfd.Cumulative(diameter) : std::nan("");
}

/// One line of the printed tables, formatted as by printf.
template <typename... Args> std::string Format(const char* format, Args... args) {
    std::array<char, 256> buffer{};
    std::snprintf(buffer.data(), buffer.size(), format, args...);
    return buffer.data();
}

constexpr double kDMin = 6.0;
constexpr double kDMax = 300.0;
constexpr double kArea = 1026.0 * 1026.0;

class DistributionsCsfd : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        const auto production = std::make_shared<terraingen::SplicedSfd>(
            terraingen::ExtendedNeukumProductionFunction());
        const auto equilibrium =
            std::make_shared<terraingen::PowerLawSfd>(terraingen::TraskEquilibrium());
        const auto lunarSurface = std::make_shared<terraingen::MinimumSfd>(
            std::vector<std::shared_ptr<const terraingen::SizeFrequencyDistribution>>{
                std::make_shared<terraingen::ProductionSurface>(production, 3.8,
                                                                terraingen::NeukumLunarChronology),
                equilibrium});
        const auto marsSurface = std::make_shared<terraingen::ProductionSurface>(
            std::make_shared<terraingen::LogPolynomialSfd>(
                terraingen::IvanovMarsProductionFunction()),
            3.0, terraingen::IvanovMarsChronology);
        distributions_ = {
            {"production 0.1 Gyr", std::make_shared<terraingen::ProductionSurface>(
                                       production, 0.1, terraingen::NeukumLunarChronology)},
            {"production 3.8 Gyr", std::make_shared<terraingen::ProductionSurface>(
                                       production, 3.8, terraingen::NeukumLunarChronology)},
            {"Trask equilibrium", equilibrium},
            {"Hartmann equilibrium",
             std::make_shared<terraingen::PowerLawSfd>(terraingen::HartmannEquilibrium())},
            {"lunar surface 3.8 Gyr", lunarSurface},
            {"Moore rough mare",
             std::make_shared<terraingen::PiecewisePowerLawSfd>(
                 terraingen::MooreDistribution(terraingen::MooreTerrain::RoughMare))},
            {"Mars surface 3 Gyr", marsSurface},
        };
        Out() << Format("N(>= D) per km^2  %12s %12s %12s\n", "10 m", "100 m", "1000 m");
        for (const auto& [name, sfd] : distributions_) {
            Out() << Format("%-22s %12.4g %12.4g %12.4g\n", name.c_str(),
                            PerSquareKilometer(*sfd, 10.0), PerSquareKilometer(*sfd, 100.0),
                            PerSquareKilometer(*sfd, 1000.0));
        }

        custom_ = std::make_shared<RolledOverPowerLaw>(0.05, 2.5, 1.2, 20.0);
        const auto ages = std::make_shared<terraingen::UniformAges>(3.0);
        terraingen::CraterLayerOptions options;
        options.seed = 5;
        options.generation.dMin = kDMin;
        options.generation.dMax = kDMax;
        const terraingen::CraterLayer craters(
            {custom_, ages,
             std::make_shared<terraingen::FreshnessDegradation>(
                 ages, terraingen::LunarFreshCraterMorphometry),
             std::make_shared<terraingen::FassettThomsonProfile>()},
            options);
        list_ = craters.Craters({-513.0, -513.0, 513.0, 513.0});
        Out() << "\ncustom distribution  D (m)  expected  generated\n";
        for (const double d : {6.0, 12.0, 20.0, 40.0, 80.0}) {
            Out() << Format("%26.0f %9.1f %10ld\n", d, custom_->ExpectedCount(kArea, d, kDMax),
                            static_cast<long>(CountAtLeast(d)));
        }
    }

    void Save(const std::filesystem::path& dir) override {
        const auto path = dir / "example_distributions_csfd.csv";
        terraingen::WriteCraterList(list_, path, terraingen::ListFormat::Csv);
        Out() << "Wrote " << path.string() << "\n";
    }

    void Test() override {
        Check(distributions_.size() == 7, "seven built-in distributions");
        const auto& young = *distributions_[0].second;
        const auto& old = *distributions_[1].second;
        Check(PerSquareKilometer(old, 100.0) > 10.0 * PerSquareKilometer(young, 100.0),
              "a 3.8 Gyr surface has over ten times the craters of a 0.1 Gyr one");
        bool decreasing = true;
        for (const auto& [name, sfd] : distributions_) {
            const double n100 = PerSquareKilometer(*sfd, 100.0);
            const double n1000 = PerSquareKilometer(*sfd, 1000.0);
            decreasing = decreasing && std::isfinite(n100) && std::isfinite(n1000) && n1000 < n100;
        }
        Check(decreasing, "every distribution has fewer craters at 1000 m than at 100 m");
        const auto& diameters = list_.Diameters();
        Check(std::all_of(diameters.begin(), diameters.end(),
                          [](double d) { return d >= kDMin && d <= kDMax; }),
              "every crater drawn is 6 to 300 m across");
        const double expected = custom_->ExpectedCount(kArea, kDMin, kDMax);
        const auto generated = static_cast<double>(CountAtLeast(kDMin));
        Check(generated > 0.7 * expected && generated < 1.3 * expected,
              "the craters drawn number within 30% of those the custom distribution expects");
        Check(CountAtLeast(12.0) < CountAtLeast(kDMin) && CountAtLeast(40.0) < CountAtLeast(12.0),
              "fewer craters are drawn at larger sizes");
    }

  private:
    std::ptrdiff_t CountAtLeast(double d) const {
        return std::count_if(list_.Diameters().begin(), list_.Diameters().end(),
                             [d](double diameter) { return diameter >= d; });
    }

    std::vector<
        std::pair<std::string, std::shared_ptr<const terraingen::SizeFrequencyDistribution>>>
        distributions_;
    std::shared_ptr<RolledOverPowerLaw> custom_;
    terraingen::CraterList list_;
};

}  // namespace

TERRAINGEN_EXAMPLE(DistributionsCsfd, "Crater size-frequency distributions, built-in and custom")
