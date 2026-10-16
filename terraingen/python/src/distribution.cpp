/// @file
/// @brief Bindings of the distribution module: size-frequency distributions, the base class
/// Python can extend, and their combinations.
#include "bindings.h"
#include "common.h"

#include "terraingen/distribution/equilibrium.h"
#include "terraingen/distribution/golombek_rapp.h"
#include "terraingen/distribution/ivanov.h"
#include "terraingen/distribution/log_polynomial.h"
#include "terraingen/distribution/minimum.h"
#include "terraingen/distribution/moore.h"
#include "terraingen/distribution/neukum.h"
#include "terraingen/distribution/piecewise_power_law.h"
#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/production_surface.h"
#include "terraingen/distribution/size_frequency.h"
#include "terraingen/distribution/sp8023.h"
#include "terraingen/distribution/spliced.h"
#include "terraingen/distribution/summed.h"
#include "terraingen/distribution/synthterrain.h"
#include "terraingen/distribution/tabulated.h"
#include "terraingen/distribution/viper.h"

#include <nanobind/trampoline.h>

#include <limits>
#include <tuple>
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
    static double CumulativeAtOf(const Sfd& sfd, double diameter) {
        return CumulativeOf(sfd, diameter);
    }
    static double IncrementalAtOf(const Sfd& sfd, double diameter) {
        return IncrementalOf(sfd, diameter);
    }
    static double InvertCumulativeAtOf(const Sfd& sfd, double target, double dMin, double dMax) {
        return InvertCumulativeOf(sfd, target, dMin, dMax);
    }

  protected:
    double CumulativeAt(double diameter) const override {
        NB_OVERRIDE_PURE_NAME("cumulative_at", CumulativeAt, diameter);
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
        if constexpr (requires { part.weight; }) {
            pairs.emplace_back(part.weight, Mutable(part.distribution));
        } else {
            pairs.emplace_back(part.start, Mutable(part.distribution));
        }
    }
    return pairs;
}

void BindBase(nb::module_& m) {
    nb::enum_<SfdKind>(m, "SfdKind", "What a size-frequency distribution counts.")
        .value("DENSITY", SfdKind::Density, "Craters or boulders per square metre.")
        .value("PRODUCTION_RATE", SfdKind::ProductionRate, "Per square metre per Gyr.");
    using Py = PySizeFrequencyDistribution;
    nb::class_<Sfd, Py>(
        m, "SizeFrequencyDistribution",
        "A cumulative size-frequency distribution N(>= D) per square metre (or per square metre "
        "per Gyr for a production rate), diameters in metres, valid between `min_diameter` and "
        "`max_diameter`. Subclass it and implement `cumulative_at(diameter)` (and optionally "
        "`incremental_at(diameter)`, -dN/dD, and `invert_cumulative(target, d_min, d_max)`) for "
        "a distribution of your own.")
        .def(nb::init<SfdKind, double, double>(), "kind"_a = SfdKind::Density,
             "min_diameter"_a = 0.0, "max_diameter"_a = kInfinity)
        .def("cumulative_at", &Py::CumulativeAtOf, "diameter"_a,
             "N(>= diameter) without range checks; override it in a subclass.")
        .def("incremental_at", &Py::IncrementalAtOf, "diameter"_a,
             "-dN/dD without range checks; numerical unless overridden.")
        .def("invert_cumulative", &Py::InvertCumulativeAtOf, "target"_a, "min_diameter"_a,
             "max_diameter"_a,
             "The diameter between the two where N(>= D) is `target`; numerical unless "
             "overridden.")
        .def_prop_ro("kind", &Sfd::Kind)
        .def_prop_ro(
            "valid_range",
            [](const Sfd& sfd) { return std::pair{sfd.ValidRange().min, sfd.ValidRange().max}; })
        .def_prop_rw("allows_extrapolation", &Sfd::AllowsExtrapolation, &Sfd::SetAllowExtrapolation)
        .def("cumulative", &Sfd::Cumulative, "diameter"_a, "N(>= diameter).")
        .def("incremental", &Sfd::Incremental, "diameter"_a, "-dN/dD at `diameter`.")
        .def("expected_count", &Sfd::ExpectedCount, "area"_a, "min_diameter"_a, "max_diameter"_a,
             "Expected number over `area` square metres between the diameters.")
        .def("sample_diameter", &Sfd::SampleDiameter, "random"_a, "min_diameter"_a,
             "max_diameter"_a, "A diameter drawn from the distribution between the two.");
}

void BindPowerLaws(nb::module_& m) {
    nb::class_<PowerLawSfd, Sfd>(m, "PowerLawSfd",
                                 "N(>= D) = coefficient * D^-exponent, D in metres.")
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
                                "One piece of a piecewise power law, from `start` metres.")
        .def_ro("start", &PowerLawSegment::start)
        .def_ro("coefficient", &PowerLawSegment::coefficient)
        .def_ro("exponent", &PowerLawSegment::exponent);
    m.def("power_law_coefficient_from_kilometres", &PowerLawCoefficientFromKilometres,
          "coefficient"_a, "exponent"_a,
          "The coefficient per square metre, D in metres, of a power law given per square "
          "kilometre with D in kilometres.");
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
        "`area_unit` convert the table to metres and square metres (1000 and 1e6 for km and "
        "km^2).")
        .def(nb::init<std::vector<double>, std::vector<double>, SfdKind, double, double>(),
             "diameters"_a, "cumulative"_a, "kind"_a = SfdKind::Density, "diameter_unit"_a = 1.0,
             "area_unit"_a = 1.0)
        .def_prop_ro("diameters", &TabulatedSfd::Diameters, "Diameters in metres.")
        .def_prop_ro("cumulative_values", &TabulatedSfd::CumulativeValues,
                     "N(>= D) per square metre at each diameter.");
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
        .def_prop_ro("coverage", &GolombekRappSfd::Coverage)
        .def_prop_ro("q", &GolombekRappSfd::Q)
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
        "Trask's (1966) lunar crater equilibrium, N(>= D) = 10^-1.1 D^-2 per square metre.");
    m.def(
        "hartmann_equilibrium", [] { return Shared(HartmannEquilibrium()); },
        "Hartmann's lunar crater equilibrium.");
    nb::enum_<MooreTerrain>(m, "MooreTerrain", "Moore's (1971) lunar terrain classes.")
        .value("ROUGH_MARE", MooreTerrain::RoughMare)
        .value("SMOOTH_MARE", MooreTerrain::SmoothMare)
        .value("UPLANDS", MooreTerrain::Uplands)
        .value("HEAVILY_CRATERED", MooreTerrain::HeavilyCratered);
    m.def(
        "moore_distribution",
        [](MooreTerrain terrain) { return Shared(MooreDistribution(terrain)); }, "terrain"_a,
        "Moore's (1971) crater distribution for a lunar terrain class.");
    m.def(
        "viper_specification", [] { return Shared(ViperSpecification()); },
        "The VIPER mission's crater specification.");
    m.def(
        "neukum_production_function", [] { return Shared(NeukumProductionFunction()); },
        "Neukum's lunar production function, per square metre per Gyr at today's rate.");
    m.def(
        "extended_neukum_production_function",
        [] { return Shared(ExtendedNeukumProductionFunction()); },
        "The Neukum production function extended to small craters, as DSNE uses it.");
    m.def("neukum_lunar_chronology", &NeukumLunarChronology, "age_gyr"_a,
          "Neukum's lunar chronology: craters at least 1 km across per square metre on a "
          "surface `age_gyr` old.");
    m.def("neukum_lunar_chronology_rate", &NeukumLunarChronologyRate, "age_gyr"_a,
          "dN1/dT of Neukum's lunar chronology, per square metre per Gyr.");
}

void BindMartianCraterDistributions(nb::module_& m) {
    m.def(
        "ivanov_mars_production_function", [] { return Shared(IvanovMarsProductionFunction()); },
        "Ivanov's (2001) Martian production function.");
    m.def("ivanov_mars_chronology", &IvanovMarsChronology, "age_gyr"_a,
          "Ivanov's (2001) Martian chronology, N(>= 1 km) per square metre.");
    m.def("ivanov_mars_chronology_rate", &IvanovMarsChronologyRate, "age_gyr"_a,
          "dN1/dT of Ivanov's Martian chronology, per square metre per Gyr.");
}

void BindBoulderDistributions(nb::module_& m) {
    nb::enum_<SpecificationCurve>(m, "SpecificationCurve", "Which SP-8023 boulder curve.")
        .value("MINIMUM", SpecificationCurve::Minimum)
        .value("NOMINAL", SpecificationCurve::Nominal)
        .value("MAXIMUM", SpecificationCurve::Maximum);
    m.def(
        "sp8023_intercrater_blocks",
        [](SpecificationCurve curve) { return SfdPointer(Sp8023IntercraterBlocks(curve)); },
        "curve"_a, "NASA SP-8023 blocks between craters.");
    m.def(
        "sp8023_crater_blocks",
        [](SpecificationCurve curve) { return SfdPointer(Sp8023CraterBlocks(curve)); }, "curve"_a,
        "NASA SP-8023 blocks near craters.");
    nb::enum_<SynthterrainBoulders>(m, "SynthterrainBoulders",
                                    "synthterrain's boulder distributions.")
        .value("INTER_CRATER", SynthterrainBoulders::InterCrater)
        .value("HAWORTH", SynthterrainBoulders::Haworth)
        .value("VIPER_SPECIFICATION", SynthterrainBoulders::ViperSpecification);
    m.def(
        "synthterrain_boulder_distribution",
        [](SynthterrainBoulders boulders) {
            return SfdPointer(SynthterrainBoulderDistribution(boulders));
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
