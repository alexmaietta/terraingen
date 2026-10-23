// SPDX-License-Identifier: BSD-3-Clause

#include "bindings.h"
#include "common.h"
#include "options.h"
#include "trampolines.h"

#include "terraingen/distribution/equilibrium.h"
#include "terraingen/distribution/golombek_rapp.h"
#include "terraingen/distribution/ivanov.h"
#include "terraingen/distribution/log_polynomial.h"
#include "terraingen/distribution/minimum.h"
#include "terraingen/distribution/moore.h"
#include "terraingen/distribution/neukum.h"
#include "terraingen/distribution/piecewise_power_law.h"
#include "terraingen/distribution/piecewise_uniform.h"
#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/production_surface.h"
#include "terraingen/distribution/size_frequency.h"
#include "terraingen/distribution/sp8023.h"
#include "terraingen/distribution/spliced.h"
#include "terraingen/distribution/summed.h"
#include "terraingen/distribution/synthterrain.h"
#include "terraingen/distribution/tabulated.h"
#include "terraingen/distribution/viper.h"

#include <limits>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace terraingen::python {

namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

using Sfd = SizeFrequencyDistribution;
using SfdPointer = std::shared_ptr<Sfd>;
using StartAndSfd = std::pair<double, SfdPointer>;

/// @brief A distribution returned by value, held as Python holds distributions.
template <typename T> SfdPointer Shared(T value) {
    return std::make_shared<T>(std::move(value));
}

/// @brief A size-frequency distribution written in Python: `cumulative_at(diameter)` is required;
/// `incremental_at` and `invert_cumulative` refine the numerical defaults.
class PySizeFrequencyDistribution : public Sfd {
  public:
    NB_TRAMPOLINE(Sfd);

    PySizeFrequencyDistribution(SfdKind kind, double minDiameter, double maxDiameter)
        : Sfd(kind, DiameterRange{minDiameter, maxDiameter}) {}

    /// @brief The hooks of any distribution, for the base class's Python methods of the same
    /// names.
    using Sfd::UncheckedCumulative;
    using Sfd::UncheckedIncremental;
    using Sfd::UncheckedInverse;

  protected:
    double CumulativeAt(double diameter) const override {
        return CallRequiredAs<double>(nb_trampoline, "cumulative_at", diameter);
    }

    double IncrementalAt(double diameter) const override {
        NB_OVERRIDE_NAME("incremental_at", IncrementalAt, diameter);
    }

    double InvertCumulative(double target, double dMin, double dMax) const override {
        NB_OVERRIDE_NAME("invert_cumulative", InvertCumulative, target, dMin, dMax);
    }
};

/// @brief (start, distribution) pairs of `parts` as Python holds them.
template <typename Part> std::vector<StartAndSfd> Pairs(const std::vector<Part>& parts) {
    std::vector<StartAndSfd> pairs;
    for (const Part& part : parts) {
        if constexpr (std::is_same_v<Part, WeightedSfd>) {
            pairs.emplace_back(part.weight, Mutable(part.distribution));
        } else {
            pairs.emplace_back(part.start, Mutable(part.distribution));
        }
    }
    return pairs;
}

void BindBase(nb::module_& m) {
    nb::enum_<SfdKind>(m, "SfdKind", "What a size-frequency distribution counts.")
        .value("DENSITY", SfdKind::Density, "Craters or boulders per square meter.")
        .value("PRODUCTION_RATE", SfdKind::ProductionRate, "Per square meter per Gyr.");
    using Py = PySizeFrequencyDistribution;
    nb::class_<Sfd, Py>(
        m, "SizeFrequencyDistribution",
        "A cumulative size-frequency distribution N(>= D) per square meter (or per square meter "
        "per Gyr for a production rate), diameters in meters, valid between `min_diameter` and "
        "`max_diameter`. Subclass it and implement `cumulative_at(diameter)` (and optionally "
        "`incremental_at(diameter)`, -dN/dD, and `invert_cumulative(target, d_min, d_max)`) for "
        "a distribution of your own.")
        .def(nb::init<SfdKind, double, double>(), "kind"_a = SfdKind::Density,
             "min_diameter"_a = 0.0, "max_diameter"_a = kInfinity)
        .def("cumulative_at", &Py::UncheckedCumulative, "diameter"_a,
             "N(>= diameter) without range checks; override it in a subclass.")
        .def("incremental_at", &Py::UncheckedIncremental, "diameter"_a,
             "-dN/dD without range checks; numerical unless overridden.")
        .def("invert_cumulative", &Py::UncheckedInverse, "target"_a, "d_min"_a, "d_max"_a,
             "The diameter between the two where N(>= D) is `target`; numerical unless "
             "overridden.")
        .def_prop_ro("kind", &Sfd::Kind)
        .def_prop_ro(
            "valid_range",
            [](const Sfd& sfd) { return std::pair{sfd.ValidRange().min, sfd.ValidRange().max}; })
        .def_prop_rw("allows_extrapolation", &Sfd::AllowsExtrapolation, &Sfd::SetAllowExtrapolation)
        .def("cumulative", &Sfd::Cumulative, "diameter"_a, "N(>= diameter).")
        .def("incremental", &Sfd::Incremental, "diameter"_a, "-dN/dD at `diameter`.")
        .def("expected_count", &Sfd::ExpectedCount, "area"_a, "d_min"_a, "d_max"_a,
             "Expected number over `area` square meters between the diameters.")
        .def("sample_diameter", &Sfd::SampleDiameter, "random"_a, "d_min"_a, "d_max"_a,
             "A diameter drawn from the distribution between the two.");
}

void BindPowerLaws(nb::module_& m) {
    nb::class_<PowerLawSfd, Sfd>(m, "PowerLawSfd",
                                 "N(>= D) = coefficient * D^-exponent, D in meters.")
        .def(
            "__init__",
            [](PowerLawSfd* self, double coefficient, double exponent, SfdKind kind,
               double minDiameter, double maxDiameter) {
                new (self) PowerLawSfd(coefficient, exponent, kind, {minDiameter, maxDiameter});
            },
            "coefficient"_a, "exponent"_a, "kind"_a = SfdKind::Density, "min_diameter"_a = 0.0,
            "max_diameter"_a = kInfinity)
        .def_prop_ro("coefficient", &PowerLawSfd::Coefficient)
        .def_prop_ro("exponent", &PowerLawSfd::Exponent);
    nb::class_<PowerLawSegment>(m, "PowerLawSegment",
                                "One piece of a piecewise power law, from `start` meters.")
        .def_ro("start", &PowerLawSegment::start)
        .def_ro("coefficient", &PowerLawSegment::coefficient)
        .def_ro("exponent", &PowerLawSegment::exponent);
    m.def("power_law_coefficient_from_kilometers", &PowerLawCoefficientFromKilometers,
          "coefficient"_a, "exponent"_a,
          "The coefficient per square meter, D in meters, of a power law given per square "
          "kilometer with D in kilometers.");
    nb::class_<PiecewisePowerLawSfd, Sfd>(
        m, "PiecewisePowerLawSfd",
        "Power laws over consecutive diameter ranges: `segments` are (start diameter, "
        "coefficient, exponent), each holding from its start to the next.")
        .def(
            "__init__",
            [](PiecewisePowerLawSfd* self,
               const std::vector<std::tuple<double, double, double>>& segments, SfdKind kind,
               double minDiameter, double maxDiameter, bool allowDiscontinuities) {
                std::vector<PowerLawSegment> pieces;
                for (const auto& [start, coefficient, exponent] : segments) {
                    pieces.push_back({start, coefficient, exponent});
                }
                new (self) PiecewisePowerLawSfd(std::move(pieces), kind, {minDiameter, maxDiameter},
                                                allowDiscontinuities);
            },
            "segments"_a, "kind"_a = SfdKind::Density, "min_diameter"_a = 0.0,
            "max_diameter"_a = kInfinity, "allow_discontinuities"_a = false)
        .def_prop_ro("segments", &PiecewisePowerLawSfd::Segments);
}

void BindTables(nb::module_& m) {
    nb::class_<TabulatedSfd, Sfd>(
        m, "TabulatedSfd",
        "Cumulative counts at given diameters, interpolated log-log. `diameter_unit` and "
        "`area_unit` convert the table to meters and square meters (1000 and 1e6 for km and "
        "km^2).")
        .def(nb::init<std::vector<double>, std::vector<double>, SfdKind, double, double>(),
             "diameters"_a, "cumulative"_a, "kind"_a = SfdKind::Density, "diameter_unit"_a = 1.0,
             "area_unit"_a = 1.0)
        .def_prop_ro("diameters", &TabulatedSfd::Diameters, "Diameters in meters.")
        .def_prop_ro("cumulative_values", &TabulatedSfd::CumulativeValues,
                     "N(>= D) per square meter at each diameter.");
    nb::class_<UniformBand>(m, "UniformBand",
                            "A band of `density` features per square meter with diameters spread "
                            "uniformly from `min` to `max` meters.")
        .def_ro("min", &UniformBand::min)
        .def_ro("max", &UniformBand::max)
        .def_ro("density", &UniformBand::density);
    nb::class_<PiecewiseUniformSfd, Sfd>(
        m, "PiecewiseUniformSfd",
        "Counts in diameter bands, each with diameters spread uniformly: `bands` are (min, max, "
        "density) with diameters in meters and densities per square meter. A positive `total` "
        "takes the densities as relative weights and shares `total` per square meter among them.")
        .def(
            "__init__",
            [](PiecewiseUniformSfd* self,
               const std::vector<std::tuple<double, double, double>>& bands, SfdKind kind,
               double total) {
                std::vector<UniformBand> pieces;
                for (const auto& [min, max, density] : bands) {
                    pieces.push_back({min, max, density});
                }
                new (self) PiecewiseUniformSfd(std::move(pieces), kind, total);
            },
            "bands"_a, "kind"_a = SfdKind::Density, "total"_a = 0.0)
        .def_prop_ro("bands", &PiecewiseUniformSfd::Bands);
    nb::class_<LogPolynomialSfd, Sfd>(
        m, "LogPolynomialSfd",
        "log10 N(>= D) as a polynomial in log10 D, with `coefficients` from the constant term "
        "up, as for the Neukum production function.")
        .def(
            "__init__",
            [](LogPolynomialSfd* self, std::vector<double> coefficients, double minDiameter,
               double maxDiameter, SfdKind kind, double diameterUnit, double areaUnit) {
                new (self) LogPolynomialSfd(std::move(coefficients), {minDiameter, maxDiameter},
                                            kind, diameterUnit, areaUnit);
            },
            "coefficients"_a, "min_diameter"_a, "max_diameter"_a, "kind"_a = SfdKind::Density,
            "diameter_unit"_a = 1.0, "area_unit"_a = 1.0)
        .def_prop_ro("coefficients", &LogPolynomialSfd::Coefficients);
}

void BindSumAndMinimum(nb::module_& m) {
    nb::class_<SummedSfd, Sfd>(
        m, "SummedSfd", "A weighted sum of distributions: `terms` are (weight, distribution).")
        .def(
            "__init__",
            [](SummedSfd* self, const std::vector<StartAndSfd>& terms) {
                std::vector<WeightedSfd> weighted;
                for (const auto& [weight, distribution] : terms) {
                    weighted.push_back({weight, distribution});
                }
                new (self) SummedSfd(std::move(weighted));
            },
            "terms"_a)
        .def_prop_ro("terms", [](const SummedSfd& sfd) { return Pairs(sfd.Terms()); });
    nb::class_<MinimumSfd, Sfd>(
        m, "MinimumSfd",
        "The smallest of several distributions at each diameter, such as production capped by "
        "equilibrium.")
        .def(
            "__init__",
            [](MinimumSfd* self, const std::vector<SfdPointer>& distributions) {
                new (self) MinimumSfd({distributions.begin(), distributions.end()});
            },
            "distributions"_a)
        .def_prop_ro("distributions", [](const MinimumSfd& sfd) {
            std::vector<SfdPointer> parts;
            for (const auto& part : sfd.Distributions()) {
                parts.push_back(Mutable(part));
            }
            return parts;
        });
}

void BindSpliced(nb::module_& m) {
    nb::class_<SplicedSfd, Sfd>(
        m, "SplicedSfd",
        "Distributions joined end to end: `pieces` are (start diameter, distribution), each "
        "holding from its start to the next.")
        .def(
            "__init__",
            [](SplicedSfd* self, const std::vector<StartAndSfd>& pieces, double minDiameter,
               double maxDiameter) {
                std::vector<SfdPiece> spliced;
                for (const auto& [start, distribution] : pieces) {
                    spliced.push_back({start, distribution});
                }
                new (self) SplicedSfd(std::move(spliced), {minDiameter, maxDiameter});
            },
            "pieces"_a, "min_diameter"_a, "max_diameter"_a)
        .def_prop_ro("pieces", [](const SplicedSfd& sfd) { return Pairs(sfd.Pieces()); });
}

void BindGolombekRapp(nb::module_& m) {
    nb::class_<GolombekRappSfd, Sfd>(
        m, "GolombekRappSfd",
        "Golombek and Rapp's (1997) boulders: the fractional area covered by boulders at least D "
        "across is coverage * exp(-q D), q = qa + qb / coverage.")
        .def(
            "__init__",
            [](GolombekRappSfd* self, double coverage, double qa, double qb, double minDiameter,
               double maxDiameter) {
                new (self) GolombekRappSfd(coverage, qa, qb, {minDiameter, maxDiameter});
            },
            "coverage"_a, "qa"_a = 1.79, "qb"_a = 0.152, "min_diameter"_a = 0.0,
            "max_diameter"_a = kInfinity)
        .def("fractional_area", &GolombekRappSfd::FractionalArea, "diameter"_a);
}

void BindProductionSurface(nb::module_& m) {
    nb::class_<ProductionSurface, Sfd>(
        m, "ProductionSurface",
        "The craters a production function leaves on a surface `age_gyr` old: its shape scaled "
        "by `chronology`, such as `neukum_lunar_chronology`.")
        .def(nb::init<SfdPointer, double, const Chronology&>(), "production"_a, "age_gyr"_a,
             "chronology"_a)
        .def_prop_ro("age_gyr", &ProductionSurface::AgeGyr)
        .def_prop_ro("production", &ProductionSurface::Production,
                     nb::rv_policy::reference_internal,
                     "The production function scaled to the surface.");
}

void BindLunarCraterDistributions(nb::module_& m) {
    m.def(
        "trask_equilibrium", [] { return Shared(TraskEquilibrium()); },
        "Trask's (1966) lunar crater equilibrium, N(>= D) = 10^-1.1 D^-2 per square meter.");
    m.def(
        "hartmann_equilibrium", [] { return Shared(HartmannEquilibrium()); },
        "Hartmann's lunar crater equilibrium.");
    BindEnum<MooreTerrain>(m, "MooreTerrain", "Moore's (1971) lunar terrain classes.");
    m.def(
        "moore_distribution",
        [](MooreTerrain terrain) { return Shared(MooreDistribution(terrain)); }, "terrain"_a,
        "Moore's (1971) crater distribution for a lunar terrain class.");
    m.def(
        "viper_specification", [] { return Shared(ViperSpecification()); },
        "The VIPER mission's crater specification.");
    m.def(
        "neukum_production_function", [] { return Shared(NeukumProductionFunction()); },
        "Neukum's lunar production function, per square meter per Gyr at today's rate.");
    m.def(
        "extended_neukum_production_function",
        [] { return Shared(ExtendedNeukumProductionFunction()); },
        "The Neukum production function extended to small craters, as DSNE uses it.");
    m.def("neukum_lunar_chronology", &NeukumLunarChronology, "age_gyr"_a,
          "Neukum's lunar chronology: craters at least 1 km across per square meter on a "
          "surface `age_gyr` old.");
    m.def("neukum_lunar_chronology_rate", &NeukumLunarChronologyRate, "age_gyr"_a,
          "dN1/dT of Neukum's lunar chronology, per square meter per Gyr.");
}

void BindMartianCraterDistributions(nb::module_& m) {
    m.def(
        "ivanov_mars_production_function", [] { return Shared(IvanovMarsProductionFunction()); },
        "Ivanov's (2001) Martian production function.");
    m.def("ivanov_mars_chronology", &IvanovMarsChronology, "age_gyr"_a,
          "Ivanov's (2001) Martian chronology, N(>= 1 km) per square meter.");
    m.def("ivanov_mars_chronology_rate", &IvanovMarsChronologyRate, "age_gyr"_a,
          "dN1/dT of Ivanov's Martian chronology, per square meter per Gyr.");
}

void BindBoulderDistributions(nb::module_& m) {
    BindEnum<SpecificationCurve>(m, "SpecificationCurve", "Which SP-8023 boulder curve.");
    m.def(
        "sp8023_intercrater_blocks",
        [](SpecificationCurve curve) { return Shared(Sp8023IntercraterBlocks(curve)); }, "curve"_a,
        "NASA SP-8023 blocks between craters.");
    m.def(
        "sp8023_crater_blocks",
        [](SpecificationCurve curve) { return Shared(Sp8023CraterBlocks(curve)); }, "curve"_a,
        "NASA SP-8023 blocks near craters.");
    nb::enum_<SynthterrainBoulders>(m, "SynthterrainBoulders",
                                    "synthterrain's boulder distributions.")
        .value("INTER_CRATER", SynthterrainBoulders::InterCrater)
        .value("HAWORTH", SynthterrainBoulders::Haworth)
        .value("VIPER_SPECIFICATION", SynthterrainBoulders::ViperSpecification);
    m.def(
        "synthterrain_boulder_distribution",
        [](SynthterrainBoulders boulders) {
            return Shared(SynthterrainBoulderDistribution(boulders));
        },
        "boulders"_a, "One of synthterrain's power-law boulder distributions.");
}

}  // namespace

void BindDistribution(nb::module_& m) {
    BindBase(m);
    BindPowerLaws(m);
    BindTables(m);
    BindSumAndMinimum(m);
    BindSpliced(m);
    BindGolombekRapp(m);
    BindProductionSurface(m);
    BindLunarCraterDistributions(m);
    BindMartianCraterDistributions(m);
    BindBoulderDistributions(m);
}

}  // namespace terraingen::python
