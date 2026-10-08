/// Crater size-frequency distributions: the built-in lunar and Martian models, and one of your own.
///
/// Prints N(>= D) per square kilometre for the production on surfaces of several ages, the
/// equilibria of Trask and Hartmann, the DSNE lunar surface (production capped at equilibrium),
/// Moore's rough mare and Mars. Then defines a distribution by deriving from
/// `SizeFrequencyDistribution` (a power law that rolls over below a knee), draws craters from it
/// and compares the counts generated with those expected.
///
///     example_distributions_csfd [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

/// N(>= D) = coefficient * D^-slope above `knee` metres, flattening to D^-shallow below it.
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

double PerSquareKilometre(const terraingen::SizeFrequencyDistribution& sfd, double diameter) {
    const auto& range = sfd.ValidRange();
    return range.Contains(diameter) ? 1e6 * sfd.Cumulative(diameter) : std::nan("");
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);

    const auto production =
        std::make_shared<terraingen::SplicedSfd>(terraingen::ExtendedNeukumProductionFunction());
    const std::vector<
        std::pair<std::string, std::shared_ptr<const terraingen::SizeFrequencyDistribution>>>
        distributions = {
            {"production 0.1 Gyr",
             std::make_shared<terraingen::ProductionSurface>(production, 0.1)},
            {"production 3.8 Gyr",
             std::make_shared<terraingen::ProductionSurface>(production, 3.8)},
            {"Trask equilibrium",
             std::make_shared<terraingen::PowerLawSfd>(terraingen::TraskEquilibrium())},
            {"Hartmann equilibrium",
             std::make_shared<terraingen::PowerLawSfd>(terraingen::HartmannEquilibrium())},
            {"lunar surface 3.8 Gyr", terraingen::LunarSurface(3.8).density},
            {"Moore rough mare",
             std::make_shared<terraingen::PiecewisePowerLawSfd>(
                 terraingen::MooreDistribution(terraingen::MooreTerrain::RoughMare))},
            {"Mars surface 3 Gyr", terraingen::MarsSurface(3.0).density},
        };
    std::printf("N(>= D) per km^2  %12s %12s %12s\n", "10 m", "100 m", "1000 m");
    for (const auto& [name, sfd] : distributions) {
        std::printf("%-22s %12.4g %12.4g %12.4g\n", name.c_str(), PerSquareKilometre(*sfd, 10.0),
                    PerSquareKilometre(*sfd, 100.0), PerSquareKilometre(*sfd, 1000.0));
    }

    const auto custom = std::make_shared<RolledOverPowerLaw>(0.05, 2.5, 1.2, 20.0);
    const auto ages = std::make_shared<terraingen::UniformAges>(3.0);
    terraingen::CraterLayerOptions options;
    options.seed = 5;
    options.generation.dMin = 6.0;
    options.generation.dMax = 300.0;
    const terraingen::CraterLayer craters({custom, ages,
                                           std::make_shared<terraingen::FreshnessDegradation>(
                                               ages, terraingen::LunarFreshCraterMorphometry),
                                           std::make_shared<terraingen::FassettThomsonProfile>()},
                                          options);
    const terraingen::CraterList list = craters.Craters({-513.0, -513.0, 513.0, 513.0});
    const double area = 1026.0 * 1026.0;
    std::printf("\ncustom distribution  D (m)  expected  generated\n");
    for (const double d : {6.0, 12.0, 20.0, 40.0, 80.0}) {
        const auto generated = std::count_if(list.Diameters().begin(), list.Diameters().end(),
                                             [d](double diameter) { return diameter >= d; });
        std::printf("%26.0f %9.1f %10ld\n", d, custom->ExpectedCount(area, d, 300.0),
                    static_cast<long>(generated));
    }
    terraingen::WriteCraterList(list, output / "example_distributions_csfd.csv",
                                terraingen::CraterListFormat::Csv);
    return 0;
}
