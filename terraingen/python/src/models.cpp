#include "bindings.h"
#include "options.h"

#include "terraingen/terraingen.h"

#include <nanobind/stl/function.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>
#include <nanobind/trampoline.h>

#include <limits>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

namespace terraingen::python {

namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

/// A size-frequency distribution written in Python: `cumulative_at(diameter)` is required;
/// `incremental_at` and `invert_cumulative` refine the numerical defaults.
class PySizeFrequencyDistribution : public SizeFrequencyDistribution {
  public:
    NB_TRAMPOLINE(SizeFrequencyDistribution);

    PySizeFrequencyDistribution(SfdKind kind, double minDiameter, double maxDiameter)
        : SizeFrequencyDistribution(kind, DiameterRange{minDiameter, maxDiameter}) {}

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

  public:
    /// The hooks of any distribution, for the base class's Python methods of the same names.
    static double CumulativeAtOf(const SizeFrequencyDistribution& sfd, double diameter) {
        return CumulativeOf(sfd, diameter);
    }
    static double IncrementalAtOf(const SizeFrequencyDistribution& sfd, double diameter) {
        return IncrementalOf(sfd, diameter);
    }
    static double InvertCumulativeAtOf(const SizeFrequencyDistribution& sfd, double target,
                                       double dMin, double dMax) {
        return InvertCumulativeOf(sfd, target, dMin, dMax);
    }
};

/// A crater age model written in Python: `lifetime_at(diameter)` is required; without
/// `sample_age_at(random, diameter, lifetime_gyr)`, ages are uniform over the lifetime.
class PyCraterAgeModel : public CraterAgeModel {
  public:
    NB_TRAMPOLINE(CraterAgeModel);

    PyCraterAgeModel() = default;

  protected:
    double LifetimeAt(double diameter) const override {
        NB_OVERRIDE_PURE_NAME("lifetime_at", LifetimeAt, diameter);
    }

    double SampleAgeAt(RandomStream& random, double diameter, double lifetimeGyr) const override {
        constexpr std::uint64_t hash = nb::detail::str_hash("sample_age_at");
        const nb::detail::ticket ticket(nb_trampoline, "sample_age_at", hash, false);
        if (ticket.key.is_valid()) {
            return nb::cast<double>(nb_trampoline.base().attr(ticket.key)(
                nb::cast(&random, nb::rv_policy::reference), diameter, lifetimeGyr));
        }
        return lifetimeGyr * random.NextDouble();
    }
};

/// A degradation model written in Python: `state(crater)` returns a `CraterState`.
class PyDegradationModel : public DegradationModel {
  public:
    NB_TRAMPOLINE(DegradationModel);

    CraterState State(const Crater& crater) const override {
        NB_OVERRIDE_PURE_NAME("state", State, crater);
    }
};

/// A crater profile written in Python: `height(distance, diameter, state)` and `reach_radii()`
/// are required; `is_complex(diameter)` is False unless overridden.
class PyCraterProfile : public CraterProfile {
  public:
    NB_TRAMPOLINE(CraterProfile);

    double Height(double distance, double diameter, const CraterState& state) const override {
        NB_OVERRIDE_PURE_NAME("height", Height, distance, diameter, state);
    }

    double ReachRadii() const override {
        NB_OVERRIDE_PURE_NAME("reach_radii", ReachRadii);
    }

    bool IsComplex(double diameter) const override {
        NB_OVERRIDE_NAME("is_complex", IsComplex, diameter);
    }
};

template <typename T> std::shared_ptr<SizeFrequencyDistribution> Shared(T value) {
    return std::make_shared<T>(std::move(value));
}

FreshMorphometry FreshOf(std::optional<FreshMorphometry> fresh) {
    return fresh ? *fresh : FreshMorphometry(LunarFreshCraterMorphometry);
}

void BindDistributions(nb::module_& m) {
    nb::enum_<SfdKind>(m, "SfdKind", "What a size-frequency distribution counts.")
        .value("DENSITY", SfdKind::Density, "Craters or rocks per square metre.")
        .value("PRODUCTION_RATE", SfdKind::ProductionRate, "Per square metre per Gyr.");

    nb::class_<SizeFrequencyDistribution, PySizeFrequencyDistribution>(
        m, "SizeFrequencyDistribution",
        "A cumulative size-frequency distribution N(>= D) per square metre (or per square metre "
        "per Gyr for a production rate), diameters in metres, valid between `min_diameter` and "
        "`max_diameter`. Subclass it and implement `cumulative_at(diameter)` (and optionally "
        "`incremental_at(diameter)`, -dN/dD, and `invert_cumulative(target, d_min, d_max)`) for "
        "a distribution of your own.")
        .def(nb::init<SfdKind, double, double>(), "kind"_a = SfdKind::Density,
             "min_diameter"_a = 0.0, "max_diameter"_a = kInfinity)
        .def("cumulative_at", &PySizeFrequencyDistribution::CumulativeAtOf, "diameter"_a,
             "N(>= diameter) without range checks; override it in a subclass.")
        .def("incremental_at", &PySizeFrequencyDistribution::IncrementalAtOf, "diameter"_a,
             "-dN/dD without range checks; numerical unless overridden.")
        .def("invert_cumulative", &PySizeFrequencyDistribution::InvertCumulativeAtOf, "target"_a,
             "min_diameter"_a, "max_diameter"_a,
             "The diameter between the two where N(>= D) is `target`; numerical unless "
             "overridden.")
        .def_prop_ro("kind", &SizeFrequencyDistribution::Kind)
        .def_prop_ro("valid_range",
                     [](const SizeFrequencyDistribution& sfd) {
                         return std::pair{sfd.ValidRange().min, sfd.ValidRange().max};
                     })
        .def_prop_rw("allows_extrapolation", &SizeFrequencyDistribution::AllowsExtrapolation,
                     &SizeFrequencyDistribution::SetAllowExtrapolation)
        .def("cumulative", &SizeFrequencyDistribution::Cumulative, "diameter"_a, "N(>= diameter).")
        .def("incremental", &SizeFrequencyDistribution::Incremental, "diameter"_a,
             "-dN/dD at `diameter`.")
        .def("expected_count", &SizeFrequencyDistribution::ExpectedCount, "area"_a,
             "min_diameter"_a, "max_diameter"_a,
             "Expected number over `area` square metres between the diameters.")
        .def("sample_diameter", &SizeFrequencyDistribution::SampleDiameter, "random"_a,
             "min_diameter"_a, "max_diameter"_a,
             "A diameter drawn from the distribution between the two.");

    nb::class_<PowerLawSfd, SizeFrequencyDistribution>(
        m, "PowerLawSfd", "N(>= D) = coefficient * D^-exponent, D in metres.")
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

    nb::class_<PiecewisePowerLawSfd, SizeFrequencyDistribution>(
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

    nb::class_<TabulatedSfd, SizeFrequencyDistribution>(
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

    nb::class_<LogPolynomialSfd, SizeFrequencyDistribution>(
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

    nb::class_<SummedSfd, SizeFrequencyDistribution>(
        m, "SummedSfd", "A weighted sum of distributions: `terms` are (weight, distribution).")
        .def(
            "__init__",
            [](SummedSfd* self,
               const std::vector<std::pair<double, std::shared_ptr<SizeFrequencyDistribution>>>&
                   terms) {
                std::vector<WeightedSfd> weighted;
                for (const auto& [weight, distribution] : terms) {
                    weighted.push_back({weight, distribution});
                }
                new (self) SummedSfd(std::move(weighted));
            },
            "terms"_a)
        .def_prop_ro("terms", [](const SummedSfd& sfd) {
            std::vector<std::pair<double, std::shared_ptr<SizeFrequencyDistribution>>> terms;
            for (const WeightedSfd& term : sfd.Terms()) {
                terms.emplace_back(term.weight, std::const_pointer_cast<SizeFrequencyDistribution>(
                                                    term.distribution));
            }
            return terms;
        });

    nb::class_<MinimumSfd, SizeFrequencyDistribution>(
        m, "MinimumSfd",
        "The smallest of several distributions at each diameter, such as production capped by "
        "equilibrium.")
        .def(
            "__init__",
            [](MinimumSfd* self,
               const std::vector<std::shared_ptr<SizeFrequencyDistribution>>& distributions) {
                new (self) MinimumSfd({distributions.begin(), distributions.end()});
            },
            "distributions"_a)
        .def_prop_ro("distributions", [](const MinimumSfd& sfd) {
            std::vector<std::shared_ptr<SizeFrequencyDistribution>> parts;
            for (const auto& part : sfd.Distributions()) {
                parts.push_back(std::const_pointer_cast<SizeFrequencyDistribution>(part));
            }
            return parts;
        });

    nb::class_<SplicedSfd, SizeFrequencyDistribution>(
        m, "SplicedSfd",
        "Distributions joined end to end: `pieces` are (start diameter, distribution), each "
        "holding from its start to the next.")
        .def(
            "__init__",
            [](SplicedSfd* self,
               const std::vector<std::pair<double, std::shared_ptr<SizeFrequencyDistribution>>>&
                   pieces,
               double minDiameter, double maxDiameter) {
                std::vector<SfdPiece> spliced;
                for (const auto& [start, distribution] : pieces) {
                    spliced.push_back({start, distribution});
                }
                new (self) SplicedSfd(std::move(spliced), {minDiameter, maxDiameter});
            },
            "pieces"_a, "min_diameter"_a, "max_diameter"_a)
        .def_prop_ro("pieces", [](const SplicedSfd& sfd) {
            std::vector<std::pair<double, std::shared_ptr<SizeFrequencyDistribution>>> pieces;
            for (const SfdPiece& piece : sfd.Pieces()) {
                pieces.emplace_back(piece.start, std::const_pointer_cast<SizeFrequencyDistribution>(
                                                     piece.distribution));
            }
            return pieces;
        });

    nb::class_<GolombekRappSfd, SizeFrequencyDistribution>(
        m, "GolombekRappSfd",
        "Golombek and Rapp's (1997) rocks: the fractional area covered by rocks at least D "
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
    m.def(
        "ivanov_mars_production_function", [] { return Shared(IvanovMarsProductionFunction()); },
        "Ivanov's (2001) Martian production function.");
    m.def("ivanov_mars_chronology", &IvanovMarsChronology, "age_gyr"_a,
          "Ivanov's (2001) Martian chronology, N(>= 1 km) per square metre.");

    nb::class_<ProductionSurface, SizeFrequencyDistribution>(
        m, "ProductionSurface",
        "The craters a production function leaves on a surface `age_gyr` old: its shape scaled "
        "by `chronology` (by default Neukum's lunar chronology).")
        .def(
            "__init__",
            [](ProductionSurface* self, std::shared_ptr<SizeFrequencyDistribution> production,
               double ageGyr, std::optional<Chronology> chronology) {
                new (self)
                    ProductionSurface(std::move(production), ageGyr,
                                      chronology.value_or(Chronology(NeukumLunarChronology)));
            },
            "production"_a, "age_gyr"_a, "chronology"_a = nb::none())
        .def_prop_ro("age_gyr", &ProductionSurface::AgeGyr)
        .def_prop_ro("production", &ProductionSurface::Production,
                     nb::rv_policy::reference_internal,
                     "The production function scaled to the surface.");
    m.def("neukum_lunar_chronology_rate", &NeukumLunarChronologyRate, "age_gyr"_a,
          "dN1/dT of Neukum's lunar chronology, per square metre per Gyr.");
    m.def("ivanov_mars_chronology_rate", &IvanovMarsChronologyRate, "age_gyr"_a,
          "dN1/dT of Ivanov's Martian chronology, per square metre per Gyr.");
    m.def("exponential_integral", &ExponentialIntegral, "n"_a, "x"_a,
          "The generalized exponential integral E_n(x), as Golombek and Rapp's model uses.");

    nb::enum_<RockModelCurve>(m, "RockModelCurve", "Which SP-8023 rock curve.")
        .value("MINIMUM", RockModelCurve::Minimum)
        .value("NOMINAL", RockModelCurve::Nominal)
        .value("MAXIMUM", RockModelCurve::Maximum);
    m.def(
        "sp8023_intercrater_blocks",
        [](RockModelCurve curve) {
            return std::shared_ptr<SizeFrequencyDistribution>(Sp8023IntercraterBlocks(curve));
        },
        "curve"_a, "NASA SP-8023 blocks between craters.");
    m.def(
        "sp8023_crater_blocks",
        [](RockModelCurve curve) {
            return std::shared_ptr<SizeFrequencyDistribution>(Sp8023CraterBlocks(curve));
        },
        "curve"_a, "NASA SP-8023 blocks near craters.");
    nb::enum_<SynthterrainRocks>(m, "SynthterrainRocks", "synthterrain's rock distributions.")
        .value("INTER_CRATER", SynthterrainRocks::InterCrater)
        .value("HAWORTH", SynthterrainRocks::Haworth)
        .value("VIPER_SPECIFICATION", SynthterrainRocks::ViperSpecification);
    m.def(
        "synthterrain_rock_distribution",
        [](SynthterrainRocks rocks) {
            return std::shared_ptr<SizeFrequencyDistribution>(SynthterrainRockDistribution(rocks));
        },
        "rocks"_a, "One of synthterrain's power-law rock distributions.");
}

void BindAges(nb::module_& m) {
    nb::class_<CraterAgeModel, PyCraterAgeModel>(
        m, "CraterAgeModel",
        "How old craters are: each has a lifetime by size and an age drawn within it. Subclass "
        "it and implement `lifetime_at(diameter)`, and optionally `sample_age_at(random, "
        "diameter, lifetime_gyr)` (uniform over the lifetime otherwise).")
        .def(nb::init<>())
        .def(
            "lifetime_at",
            [](const CraterAgeModel& model, double diameter) {
                return model.LifetimeGyr(diameter);
            },
            "diameter"_a, "The lifetime (Gyr) of a crater `diameter` across; override it.")
        .def(
            "sample_age_at",
            [](const CraterAgeModel& /*model*/, RandomStream& random, double /*diameter*/,
               double lifetimeGyr) { return lifetimeGyr * random.NextDouble(); },
            "random"_a, "diameter"_a, "lifetime_gyr"_a,
            "An age (Gyr) within the lifetime; uniform unless overridden.")
        .def("lifetime_gyr", &CraterAgeModel::LifetimeGyr, "diameter"_a)
        .def("sample_age_gyr", &CraterAgeModel::SampleAgeGyr, "random"_a, "diameter"_a)
        .def("freshness", &CraterAgeModel::Freshness, "diameter"_a, "age_gyr"_a,
             "1 for a new crater, falling to 0 at the end of its lifetime.");
    nb::class_<FreshAges, CraterAgeModel>(m, "FreshAges", "Every crater is new (age 0).")
        .def(nb::init<>());
    nb::class_<UniformAges, CraterAgeModel>(
        m, "UniformAges", "Ages uniform over a lifetime of `lifetime_gyr`, whatever the size.")
        .def(nb::init<double>(), "lifetime_gyr"_a);
    nb::class_<SteadyStateAges, CraterAgeModel>(
        m, "SteadyStateAges",
        "Ages of a surface `age_gyr` old in steady state: a crater lasts until the "
        "`equilibrium` density is replaced by `production_rate`, or the surface's age.")
        .def(
            "__init__",
            [](SteadyStateAges* self, std::shared_ptr<SizeFrequencyDistribution> production,
               std::shared_ptr<SizeFrequencyDistribution> equilibrium, double ageGyr) {
                new (self) SteadyStateAges(std::move(production), std::move(equilibrium), ageGyr);
            },
            "production_rate"_a, "equilibrium"_a, "age_gyr"_a)
        .def_prop_ro("surface_age_gyr", &SteadyStateAges::SurfaceAgeGyr)
        .def("equilibrium_lifetime_gyr", &SteadyStateAges::EquilibriumLifetimeGyr, "diameter"_a);

    nb::class_<SurfaceModel>(m, "SurfaceModel", "A surface's crater density and age model.")
        .def_prop_ro("density",
                     [](const SurfaceModel& model) {
                         return std::const_pointer_cast<SizeFrequencyDistribution>(model.density);
                     })
        .def_prop_ro("ages", [](const SurfaceModel& model) {
            return std::const_pointer_cast<CraterAgeModel>(model.ages);
        });
    m.def("lunar_surface", &LunarSurface, "age_gyr"_a,
          "The DSNE lunar surface `age_gyr` old: the extended Neukum production capped at "
          "Trask's equilibrium, valid from 2.5 m to 300 km, with steady-state ages.");
    m.def("mars_surface", &MarsSurface, "age_gyr"_a,
          "A Martian surface `age_gyr` old: Ivanov's chronology and production, valid from 15 m "
          "to 362 km.");
}

void BindDegradation(nb::module_& m) {
    nb::class_<CraterMorphometry>(m, "CraterMorphometry",
                                  "A crater's depth and rim height, each over its diameter.")
        .def(
            "__init__",
            [](CraterMorphometry* self, double depthRatio, double rimHeightRatio) {
                new (self) CraterMorphometry{depthRatio, rimHeightRatio};
            },
            "depth_ratio"_a = 0.0, "rim_height_ratio"_a = 0.0)
        .def_rw("depth_ratio", &CraterMorphometry::depthRatio)
        .def_rw("rim_height_ratio", &CraterMorphometry::rimHeightRatio);
    nb::class_<CraterState>(m, "CraterState",
                            "A crater's shape when fresh and now, and how far diffusion has "
                            "smoothed it.")
        .def(
            "__init__",
            [](CraterState* self, CraterMorphometry fresh, CraterMorphometry current,
               double diffusion) { new (self) CraterState{fresh, current, diffusion}; },
            "fresh"_a = CraterMorphometry{}, "current"_a = CraterMorphometry{}, "diffusion"_a = 0.0)
        .def_rw("fresh", &CraterState::fresh)
        .def_rw("current", &CraterState::current)
        .def_rw("diffusion", &CraterState::diffusion);
    m.def("lunar_fresh_crater_morphometry", &LunarFreshCraterMorphometry, "diameter"_a,
          "A fresh lunar crater's depth and rim height ratios.");
    m.def("lunar_fresh_crater_depth_ratio", &LunarFreshCraterDepthRatio, "diameter"_a,
          "A fresh lunar crater's depth over diameter.");
    m.def("fassett_thomson_2014_diffusion_age", &FassettThomson2014DiffusionAge, "age_gyr"_a,
          "Fassett and Thomson's (2014) effective diffusion age for a crater `age_gyr` old.");
    m.def("mars_fresh_crater_morphometry", &MarsFreshCraterMorphometry, "diameter"_a,
          "A fresh Martian crater's depth and rim height ratios.");

    nb::class_<DegradationModel, PyDegradationModel>(
        m, "DegradationModel",
        "How a crater's shape changes with age. Subclass it and implement `state(crater)`, "
        "returning a CraterState, for a model of your own.")
        .def(nb::init<>())
        .def("state", &DegradationModel::State, "crater"_a);
    nb::class_<NoDegradation, DegradationModel>(
        m, "NoDegradation",
        "Craters keep their fresh shape (`fresh`, a function of diameter giving a "
        "CraterMorphometry, lunar by default), or a fixed `depth_ratio`.")
        .def(
            "__init__",
            [](NoDegradation* self, std::optional<double> depthRatio,
               std::optional<FreshMorphometry> fresh) {
                new (self) NoDegradation(FreshOf(std::move(fresh)), depthRatio);
            },
            "depth_ratio"_a = nb::none(), "fresh"_a = nb::none());
    nb::class_<FreshnessDegradation, DegradationModel>(
        m, "FreshnessDegradation",
        "Craters shallow from their fresh shape with their freshness under `ages`, down to "
        "`cutoff_depth_ratio`.")
        .def(
            "__init__",
            [](FreshnessDegradation* self, std::shared_ptr<CraterAgeModel> ages,
               double cutoffDepthRatio, std::optional<FreshMorphometry> fresh) {
                new (self) FreshnessDegradation(std::move(ages), FreshOf(std::move(fresh)),
                                                cutoffDepthRatio);
            },
            "ages"_a, "cutoff_depth_ratio"_a = 0.04, "fresh"_a = nb::none());

    auto diffusivity = nb::class_<PowerLawDiffusivity>(
        m, "PowerLawDiffusivity",
        "Topographic diffusivity coefficient * D^exponent (m^2 per Myr, D in km), no less than "
        "at `min_diameter` metres; called with (diameter, age_gyr) it gives the diffusion "
        "length squared, in m^2.");
    BindKwargsInit<PowerLawDiffusivity>(diffusivity);
    diffusivity.def_rw("coefficient", &PowerLawDiffusivity::coefficient)
        .def_rw("exponent", &PowerLawDiffusivity::exponent)
        .def_rw("min_diameter", &PowerLawDiffusivity::minDiameter)
        .def("diffusivity", &PowerLawDiffusivity::Diffusivity, "diameter"_a)
        .def("__call__", &PowerLawDiffusivity::operator(), "diameter"_a, "age_gyr"_a);
    m.def("fassett_thomson_2014_diffusivity", &FassettThomson2014Diffusivity,
          "Fassett and Thomson's (2014) constant diffusivity, 5.5 m^2/Myr.");
    m.def("fassett_2022_hartmann_diffusivity", &Fassett2022HartmannDiffusivity,
          "Fassett et al.'s (2022) diffusivity fit with Hartmann's equilibrium.");
    m.def("fassett_2022_trask_diffusivity", &Fassett2022TraskDiffusivity,
          "Fassett et al.'s (2022) diffusivity fit with Trask's equilibrium.");
}

void BindProfiles(nb::module_& m) {
    nb::class_<CraterProfile, PyCraterProfile>(
        m, "CraterProfile",
        "A crater's height relative to the surrounding surface at `distance` metres from its "
        "centre. Subclass it and implement `height(distance, diameter, state)` and "
        "`reach_radii()` (how far, in radii, it changes the terrain), and optionally "
        "`is_complex(diameter)`.")
        .def(nb::init<>())
        .def("height", &CraterProfile::Height, "distance"_a, "diameter"_a, "state"_a)
        .def("reach_radii", &CraterProfile::ReachRadii)
        .def("is_complex", &CraterProfile::IsComplex, "diameter"_a);

    nb::enum_<FassettThomsonRim>(m, "FassettThomsonRim", "How the Fassett and Thomson rim is fit.")
        .value("ORIGINAL", FassettThomsonRim::Original)
        .value("AT_ONE_RADIUS", FassettThomsonRim::AtOneRadius)
        .value("PLATEAU", FassettThomsonRim::Plateau);
    nb::class_<FassettThomsonProfile, CraterProfile>(
        m, "FassettThomsonProfile",
        "The fresh simple-crater profile of Fassett and Thomson (2014).")
        .def(nb::init<FassettThomsonRim>(), "rim"_a = FassettThomsonRim::Original);
    nb::class_<MartinParkesDunstanProfile, CraterProfile>(
        m, "MartinParkesDunstanProfile",
        "Martin, Parkes and Dunstan's profile, whose rounded rim lowers as the crater degrades.")
        .def(nb::init<>());

    nb::class_<KilometrePowerLaw>(m, "KilometrePowerLaw",
                                  "coefficient * D^exponent with D in kilometres.")
        .def(
            "__init__",
            [](KilometrePowerLaw* self, double coefficient, double exponent) {
                new (self) KilometrePowerLaw{coefficient, exponent};
            },
            "coefficient"_a = 0.0, "exponent"_a = 0.0)
        .def_rw("coefficient", &KilometrePowerLaw::coefficient)
        .def_rw("exponent", &KilometrePowerLaw::exponent);
    auto shape = nb::class_<ComplexCraterShape>(
        m, "ComplexCraterShape",
        "Where craters turn complex and how their floors and central peaks scale.");
    BindKwargsInit<ComplexCraterShape>(shape);
    shape.def_rw("transition_diameter", &ComplexCraterShape::transitionDiameter)
        .def_rw("floor_diameter", &ComplexCraterShape::floorDiameter)
        .def_rw("peak_height", &ComplexCraterShape::peakHeight)
        .def_rw("peak_volume", &ComplexCraterShape::peakVolume)
        .def_rw("peak_diameter", &ComplexCraterShape::peakDiameter)
        .def_rw("peak_fit_max_diameter", &ComplexCraterShape::peakFitMaxDiameter)
        .def_rw("ejecta_exponent", &ComplexCraterShape::ejectaExponent)
        .def_rw("reach_radii", &ComplexCraterShape::reachRadii);
    m.def("lunar_complex_crater_shape", &LunarComplexCraterShape,
          "Lunar complex craters, from 15 km.");
    m.def("mars_complex_crater_shape", &MarsComplexCraterShape,
          "Martian complex craters, from 7 km.");
    nb::class_<ComplexCraterProfile, CraterProfile>(
        m, "ComplexCraterProfile",
        "`simple` below `shape.transition_diameter`; above it a flat floor and a central peak.")
        .def(
            "__init__",
            [](ComplexCraterProfile* self, std::shared_ptr<CraterProfile> simple,
               ComplexCraterShape shape) {
                new (self) ComplexCraterProfile(std::move(simple), std::move(shape));
            },
            "simple"_a, "shape"_a)
        .def_prop_ro("shape", &ComplexCraterProfile::Shape);

    nb::class_<DiffusedProfile, CraterProfile>(
        m, "DiffusedProfile",
        "`fresh` smoothed by topographic diffusion, tabulated by the diffusion time over the "
        "diameter squared; used with DiffusionDegradation.")
        .def(
            "__init__",
            [](DiffusedProfile* self, std::shared_ptr<CraterProfile> fresh, double reachRadii,
               std::size_t cells) {
                new (self) DiffusedProfile(std::move(fresh), reachRadii, cells);
            },
            "fresh"_a, "reach_radii"_a = 3.0, "cells"_a = 600);
    nb::class_<DiffusionDegradation, DegradationModel>(
        m, "DiffusionDegradation",
        "Craters smoothed by topographic diffusion over their age (Fassett and Thomson 2014): "
        "`diffusivity` gives each crater's diffusion length squared (m^2) from its diameter and "
        "age, as PowerLawDiffusivity does, and `profile` the diffused shape.")
        .def(
            "__init__",
            [](DiffusionDegradation* self, DiffusionAgeModel diffusivity,
               std::shared_ptr<DiffusedProfile> profile, std::optional<FreshMorphometry> fresh) {
                new (self) DiffusionDegradation(FreshOf(std::move(fresh)), std::move(diffusivity),
                                                std::move(profile));
            },
            "diffusivity"_a, "profile"_a, "fresh"_a = nb::none())
        .def(
            "__init__",
            [](DiffusionDegradation* self, const PowerLawDiffusivity& diffusivity,
               std::shared_ptr<DiffusedProfile> profile, std::optional<FreshMorphometry> fresh) {
                new (self) DiffusionDegradation(FreshOf(std::move(fresh)), diffusivity,
                                                std::move(profile));
            },
            "diffusivity"_a, "profile"_a, "fresh"_a = nb::none());

    nb::class_<CraterModels>(m, "CraterModels",
                             "The models a crater layer draws and shapes craters with.")
        .def(
            "__init__",
            [](CraterModels* self, std::shared_ptr<SizeFrequencyDistribution> density,
               std::shared_ptr<CraterAgeModel> ages, std::shared_ptr<DegradationModel> degradation,
               std::shared_ptr<CraterProfile> profile) {
                new (self) CraterModels{std::move(density), std::move(ages), std::move(degradation),
                                        std::move(profile)};
            },
            "density"_a, "ages"_a, "degradation"_a, "profile"_a)
        .def_prop_ro("density",
                     [](const CraterModels& models) {
                         return std::const_pointer_cast<SizeFrequencyDistribution>(models.density);
                     })
        .def_prop_ro("ages",
                     [](const CraterModels& models) {
                         return std::const_pointer_cast<CraterAgeModel>(models.ages);
                     })
        .def_prop_ro("degradation",
                     [](const CraterModels& models) {
                         return std::const_pointer_cast<DegradationModel>(models.degradation);
                     })
        .def_prop_ro("profile", [](const CraterModels& models) {
            return std::const_pointer_cast<CraterProfile>(models.profile);
        });
}

}  // namespace

void BindModels(nb::module_& m) {
    BindDistributions(m);
    BindAges(m);
    BindDegradation(m);
    BindProfiles(m);
}

}  // namespace terraingen::python
