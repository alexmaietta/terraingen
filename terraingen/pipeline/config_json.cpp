#include "terraingen/pipeline/config_json.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace terraingen {

namespace {

using Json = nlohmann::ordered_json;

std::string Child(const std::string& path, std::string_view key) {
    return path.empty() ? std::string(key) : path + "." + std::string(key);
}

std::string Element(const std::string& path, std::size_t index) {
    return path + "[" + std::to_string(index) + "]";
}

std::string TypeName(const Json& json) {
    return json.type_name();
}

[[noreturn]] void Fail(const std::string& path, const std::string& message) {
    throw ConfigError(path, message);
}

template <typename E> struct EnumNames;

template <> struct EnumNames<NoiseBasis> {
    static constexpr std::array<std::pair<std::string_view, NoiseBasis>, 6> kValues{{
        {"perlin", NoiseBasis::Perlin},
        {"simplex", NoiseBasis::Simplex},
        {"value", NoiseBasis::Value},
        {"worley_f1", NoiseBasis::WorleyF1},
        {"worley_f2", NoiseBasis::WorleyF2},
        {"worley_f2_minus_f1", NoiseBasis::WorleyF2MinusF1},
    }};
};

template <> struct EnumNames<FractalType> {
    static constexpr std::array<std::pair<std::string_view, FractalType>, 4> kValues{{
        {"fbm", FractalType::Fbm},
        {"ridged", FractalType::Ridged},
        {"billow", FractalType::Billow},
        {"hybrid_multifractal", FractalType::HybridMultifractal},
    }};
};

template <> struct EnumNames<LayerBlend> {
    static constexpr std::array<std::pair<std::string_view, LayerBlend>, 4> kValues{{
        {"add", LayerBlend::Add},
        {"max", LayerBlend::Max},
        {"min", LayerBlend::Min},
        {"mask", LayerBlend::Mask},
    }};
};

template <> struct EnumNames<MooreTerrain> {
    static constexpr std::array<std::pair<std::string_view, MooreTerrain>, 4> kValues{{
        {"rough_mare", MooreTerrain::RoughMare},
        {"smooth_mare", MooreTerrain::SmoothMare},
        {"uplands", MooreTerrain::Uplands},
        {"heavily_cratered", MooreTerrain::HeavilyCratered},
    }};
};

template <> struct EnumNames<EquilibriumModel> {
    static constexpr std::array<std::pair<std::string_view, EquilibriumModel>, 2> kValues{{
        {"trask", EquilibriumModel::Trask},
        {"hartmann", EquilibriumModel::Hartmann},
    }};
};

template <> struct EnumNames<FassettThomsonRim> {
    static constexpr std::array<std::pair<std::string_view, FassettThomsonRim>, 3> kValues{{
        {"original", FassettThomsonRim::Original},
        {"at_one_radius", FassettThomsonRim::AtOneRadius},
        {"plateau", FassettThomsonRim::Plateau},
    }};
};

template <> struct EnumNames<CraterOrder> {
    static constexpr std::array<std::pair<std::string_view, CraterOrder>, 2> kValues{{
        {"oldest_first", CraterOrder::OldestFirst},
        {"largest_first", CraterOrder::LargestFirst},
    }};
};

template <> struct EnumNames<CraterDatum> {
    static constexpr std::array<std::pair<std::string_view, CraterDatum>, 4> kValues{{
        {"none", CraterDatum::None},
        {"plane", CraterDatum::Plane},
        {"mean_height", CraterDatum::MeanHeight},
        {"centre_height", CraterDatum::CentreHeight},
    }};
};

template <> struct EnumNames<BlendShape> {
    static constexpr std::array<std::pair<std::string_view, BlendShape>, 4> kValues{{
        {"linear", BlendShape::Linear},
        {"smoothstep", BlendShape::Smoothstep},
        {"smootherstep", BlendShape::Smootherstep},
        {"gaussian", BlendShape::Gaussian},
    }};
};

template <> struct EnumNames<EjectaProfile> {
    static constexpr std::array<std::pair<std::string_view, EjectaProfile>, 2> kValues{{
        {"smoothstep", EjectaProfile::Smoothstep},
        {"exponential", EjectaProfile::Exponential},
    }};
};

template <> struct EnumNames<EjectaAging> {
    static constexpr std::array<std::pair<std::string_view, EjectaAging>, 2> kValues{{
        {"freshness", EjectaAging::Freshness},
        {"age", EjectaAging::Age},
    }};
};

template <> struct EnumNames<RockModelCurve> {
    static constexpr std::array<std::pair<std::string_view, RockModelCurve>, 3> kValues{{
        {"minimum", RockModelCurve::Minimum},
        {"nominal", RockModelCurve::Nominal},
        {"maximum", RockModelCurve::Maximum},
    }};
};

template <> struct EnumNames<SynthterrainRocks> {
    static constexpr std::array<std::pair<std::string_view, SynthterrainRocks>, 3> kValues{{
        {"intercrater", SynthterrainRocks::InterCrater},
        {"haworth", SynthterrainRocks::Haworth},
        {"viper_specification", SynthterrainRocks::ViperSpecification},
    }};
};

template <> struct EnumNames<BaseSolid> {
    static constexpr std::array<std::pair<std::string_view, BaseSolid>, 5> kValues{{
        {"icosahedron", BaseSolid::Icosahedron},
        {"cube", BaseSolid::Cube},
        {"pyramid", BaseSolid::Pyramid},
        {"prism", BaseSolid::Prism},
        {"tetrahedron", BaseSolid::Tetrahedron},
    }};
};

template <> struct EnumNames<ValueDistribution::Kind> {
    static constexpr std::array<std::pair<std::string_view, ValueDistribution::Kind>, 4> kValues{{
        {"constant", ValueDistribution::Kind::Constant},
        {"uniform", ValueDistribution::Kind::Uniform},
        {"normal", ValueDistribution::Kind::Normal},
        {"log_normal", ValueDistribution::Kind::LogNormal},
    }};
};

template <> struct EnumNames<RimRoughnessModel> {
    static constexpr std::array<std::pair<std::string_view, RimRoughnessModel>, 2> kValues{{
        {"power_law", RimRoughnessModel::PowerLaw},
        {"du_2024", RimRoughnessModel::Du2024},
    }};
};

template <> struct EnumNames<CraterBody> {
    static constexpr std::array<std::pair<std::string_view, CraterBody>, 2> kValues{{
        {"moon", CraterBody::Moon},
        {"mars", CraterBody::Mars},
    }};
};

template <> struct EnumNames<RockRelief> {
    static constexpr std::array<std::pair<std::string_view, RockRelief>, 3> kValues{{
        {"dome", RockRelief::Dome},
        {"shape", RockRelief::Shape},
        {"none", RockRelief::None},
    }};
};

template <> struct EnumNames<CraterListFormat> {
    static constexpr std::array<std::pair<std::string_view, CraterListFormat>, 2> kValues{{
        {"csv", CraterListFormat::Csv},
        {"json", CraterListFormat::Json},
    }};
};

template <> struct EnumNames<ResampleMethod> {
    static constexpr std::array<std::pair<std::string_view, ResampleMethod>, 3> kValues{{
        {"nearest", ResampleMethod::Nearest},
        {"bilinear", ResampleMethod::Bilinear},
        {"cubic", ResampleMethod::Cubic},
    }};
};

template <> struct EnumNames<TiffCompression> {
    static constexpr std::array<std::pair<std::string_view, TiffCompression>, 4> kValues{{
        {"none", TiffCompression::None},
        {"lzw", TiffCompression::Lzw},
        {"deflate", TiffCompression::Deflate},
        {"packbits", TiffCompression::PackBits},
    }};
};

template <typename E>
concept NamedEnum = std::is_enum_v<E> && requires { EnumNames<E>::kValues; };

template <typename Spec> struct SpecName;
template <> struct SpecName<FlatSourceSpec> {
    static constexpr std::string_view kName = "flat";
};
template <> struct SpecName<RasterSourceSpec> {
    static constexpr std::string_view kName = "raster";
};
template <> struct SpecName<ChainSourceSpec> {
    static constexpr std::string_view kName = "chain";
};
template <> struct SpecName<NoiseLayerSpec> {
    static constexpr std::string_view kName = "noise";
};
template <> struct SpecName<CraterLayerSpec> {
    static constexpr std::string_view kName = "craters";
};
template <> struct SpecName<RockLayerSpec> {
    static constexpr std::string_view kName = "rocks";
};
template <> struct SpecName<RoughnessLayerSpec> {
    static constexpr std::string_view kName = "roughness";
};
template <> struct SpecName<GolombekRappRocksSpec> {
    static constexpr std::string_view kName = "golombek_rapp";
};
template <> struct SpecName<Sp8023IntercraterRocksSpec> {
    static constexpr std::string_view kName = "sp8023_intercrater";
};
template <> struct SpecName<Sp8023CraterRocksSpec> {
    static constexpr std::string_view kName = "sp8023_crater";
};
template <> struct SpecName<SynthterrainRocksSpec> {
    static constexpr std::string_view kName = "synthterrain";
};
template <> struct SpecName<BedformLayerSpec> {
    static constexpr std::string_view kName = "bedforms";
};
template <> struct SpecName<ScaleLayerSpec> {
    static constexpr std::string_view kName = "scale";
};
template <> struct SpecName<ClampLayerSpec> {
    static constexpr std::string_view kName = "clamp";
};
template <> struct SpecName<RegionLayerSpec> {
    static constexpr std::string_view kName = "region";
};
template <> struct SpecName<RectangleRegionSpec> {
    static constexpr std::string_view kName = "rectangle";
};
template <> struct SpecName<CircleRegionSpec> {
    static constexpr std::string_view kName = "circle";
};
template <> struct SpecName<RimSmoothing> {
    static constexpr std::string_view kName = "rim_smoothing";
};
template <> struct SpecName<HeightmapOutput> {
    static constexpr std::string_view kName = "heightmap";
};
template <> struct SpecName<LunarSurfaceSpec> {
    static constexpr std::string_view kName = "lunar_surface";
};
template <> struct SpecName<MarsSurfaceSpec> {
    static constexpr std::string_view kName = "mars_surface";
};
template <> struct SpecName<PowerLawSpec> {
    static constexpr std::string_view kName = "power_law";
};
template <> struct SpecName<PiecewisePowerLawSpec> {
    static constexpr std::string_view kName = "piecewise_power_law";
};
template <> struct SpecName<TabulatedSpec> {
    static constexpr std::string_view kName = "tabulated";
};
template <> struct SpecName<TraskSpec> {
    static constexpr std::string_view kName = "trask";
};
template <> struct SpecName<HartmannSpec> {
    static constexpr std::string_view kName = "hartmann";
};
template <> struct SpecName<MooreSpec> {
    static constexpr std::string_view kName = "moore";
};
template <> struct SpecName<ViperSpec> {
    static constexpr std::string_view kName = "viper";
};
template <> struct SpecName<AutoAgesSpec> {
    static constexpr std::string_view kName = "auto";
};
template <> struct SpecName<FreshAgesSpec> {
    static constexpr std::string_view kName = "fresh";
};
template <> struct SpecName<UniformAgesSpec> {
    static constexpr std::string_view kName = "uniform";
};
template <> struct SpecName<SteadyStateAgesSpec> {
    static constexpr std::string_view kName = "steady_state";
};
template <> struct SpecName<FreshnessDegradationSpec> {
    static constexpr std::string_view kName = "freshness";
};
template <> struct SpecName<NoDegradationSpec> {
    static constexpr std::string_view kName = "none";
};
template <> struct SpecName<FassettThomsonSpec> {
    static constexpr std::string_view kName = "fassett_thomson";
};
template <> struct SpecName<ComplexProfileSpec> {
    static constexpr std::string_view kName = "complex";
};
template <> struct SpecName<MartinParkesDunstanSpec> {
    static constexpr std::string_view kName = "martin_parkes_dunstan";
};
template <> struct SpecName<RockListOutput> {
    static constexpr std::string_view kName = "rock_list";
};
template <> struct SpecName<BoulderModelsOutput> {
    static constexpr std::string_view kName = "boulder_models";
};
template <> struct SpecName<CraterListOutput> {
    static constexpr std::string_view kName = "crater_list";
};

/// Reads the keys of one JSON object into fields, remembering which keys were asked for so that
/// any other key can be reported.
class Reader {
  public:
    Reader(const Json& json, std::string path) : json_(json), path_(std::move(path)) {
        if (!json_.is_object()) {
            Fail(path_, "expected an object, not " + TypeName(json_));
        }
    }

    template <typename T> void operator()(std::string_view key, T& value);

    /// Marks `key` as known without reading it.
    void Accept(std::string_view key) {
        known_.emplace(key);
    }

    bool Has(std::string_view key) const {
        return json_.contains(key);
    }

    /// @throws ConfigError for a key that no field asked for.
    void Finish() const {
        for (const auto& [key, value] : json_.items()) {
            if (known_.count(key) == 0) {
                std::string expected;
                for (const std::string& name : known_) {
                    expected += (expected.empty() ? "" : ", ") + name;
                }
                Fail(Child(path_, key), "unknown key; expected one of: " + expected);
            }
        }
    }

  private:
    const Json& json_;
    std::string path_;
    std::set<std::string, std::less<>> known_;
};

/// Writes fields as the keys of a JSON object, in order.
class Writer {
  public:
    explicit Writer(Json& json) : json_(json) {}

    template <typename T> void operator()(std::string_view key, T& value);

  private:
    Json& json_;
};

template <typename IO> void Fields(IO& io, FlatSourceSpec& source);
template <typename IO> void Fields(IO& io, RasterSourceSpec& source);
template <typename IO> void Fields(IO& io, ChainSourceSpec& source);
template <typename IO> void Fields(IO& io, NoiseLayerSpec& layer);
template <typename IO> void Fields(IO& io, HeightmapOutput& output);
template <typename IO> void Fields(IO& io, NoiseLayerConfig& layer);
template <typename IO> void Fields(IO& io, PowerLawSegment& segment);
template <typename IO> void Fields(IO& io, CraterSpacing& spacing);
template <typename IO> void Fields(IO& io, Crater& crater);
template <typename IO> void Fields(IO& io, CraterBlend& blend);
template <typename IO> void Fields(IO& io, ResolutionFade& fade);
template <typename IO> void Fields(IO& io, CraterApplyOptions& apply);
template <typename IO> void Fields(IO& io, ValueDistribution& distribution);
template <typename IO> void Fields(IO& io, CraterIrregularity& irregularity);
template <typename IO> void Fields(IO& io, RimSmoothing& smoothing);
template <typename IO> void Fields(IO& io, CraterLayerSpec& craters);
template <typename IO> void Fields(IO& io, RockPatches& patches);
template <typename IO> void Fields(IO& io, RockEjecta& ejecta);
template <typename IO> void Fields(IO& io, ExposureParams& exposure);
template <typename IO> void Fields(IO& io, RockLayerSpec& spec);
template <typename IO> void Fields(IO& io, RoughnessLayerSpec& spec);
template <typename IO> void Fields(IO& io, GolombekRappRocksSpec& spec);
template <typename IO> void Fields(IO& io, Sp8023IntercraterRocksSpec& spec);
template <typename IO> void Fields(IO& io, Sp8023CraterRocksSpec& spec);
template <typename IO> void Fields(IO& io, SynthterrainRocksSpec& spec);
template <typename IO> void Fields(IO& io, ScaleLayerSpec& spec);
template <typename IO> void Fields(IO& io, BedformLayerSpec& spec);
template <typename IO> void Fields(IO& io, ClampLayerSpec& spec);
template <typename IO> void Fields(IO& io, RectangleRegionSpec& spec);
template <typename IO> void Fields(IO& io, CircleRegionSpec& spec);
template <typename IO> void Fields(IO& io, RegionLayerSpec& spec);
template <typename IO> void Fields(IO& io, LunarSurfaceSpec& spec);
template <typename IO> void Fields(IO& io, PowerLawSpec& spec);
template <typename IO> void Fields(IO& io, MarsSurfaceSpec& spec);
template <typename IO> void Fields(IO& io, PiecewisePowerLawSpec& spec);
template <typename IO> void Fields(IO& io, TabulatedSpec& spec);
template <typename IO> void Fields(IO& io, TraskSpec& spec);
template <typename IO> void Fields(IO& io, HartmannSpec& spec);
template <typename IO> void Fields(IO& io, MooreSpec& spec);
template <typename IO> void Fields(IO& io, ViperSpec& spec);
template <typename IO> void Fields(IO& io, AutoAgesSpec& spec);
template <typename IO> void Fields(IO& io, FreshAgesSpec& spec);
template <typename IO> void Fields(IO& io, UniformAgesSpec& spec);
template <typename IO> void Fields(IO& io, SteadyStateAgesSpec& spec);
template <typename IO> void Fields(IO& io, FreshnessDegradationSpec& spec);
template <typename IO> void Fields(IO& io, NoDegradationSpec& spec);
template <typename IO> void Fields(IO& io, FassettThomsonSpec& spec);
template <typename IO> void Fields(IO& io, MartinParkesDunstanSpec& spec);
template <typename IO> void Fields(IO& io, ComplexProfileSpec& spec);
template <typename IO> void Fields(IO& io, CraterListOutput& output);
template <typename IO> void Fields(IO& io, RockListOutput& output);
template <typename IO> void Fields(IO& io, BoulderModelsOutput& output);
template <typename IO> void Fields(IO& io, RockModels& models);
template <typename IO> void Fields(IO& io, ValueRange& range);
template <typename IO> void Fields(IO& io, BoulderStyle& style);
template <typename IO> void Fields(IO& io, BoulderShapeOptions& shapes);

template <typename T>
concept HasFields = requires(Reader& reader, T& value) { Fields(reader, value); };

void Read(const Json& json, const std::string& path, double& value) {
    if (!json.is_number()) {
        Fail(path, "expected a number, not " + TypeName(json));
    }
    value = json.get<double>();
    if (!std::isfinite(value)) {
        Fail(path, "expected a finite number");
    }
}

void Read(const Json& json, const std::string& path, float& value) {
    double number = 0.0;
    Read(json, path, number);
    if (std::fabs(number) > static_cast<double>(std::numeric_limits<float>::max())) {
        Fail(path, "number is too large");
    }
    value = static_cast<float>(number);
}

void Read(const Json& json, const std::string& path, bool& value) {
    if (!json.is_boolean()) {
        Fail(path, "expected true or false, not " + TypeName(json));
    }
    value = json.get<bool>();
}

void Read(const Json& json, const std::string& path, std::string& value) {
    if (!json.is_string()) {
        Fail(path, "expected a string, not " + TypeName(json));
    }
    value = json.get<std::string>();
}

void Read(const Json& json, const std::string& path, int& value) {
    if (!json.is_number_integer()) {
        Fail(path, "expected an integer, not " + TypeName(json));
    }
    if (json.is_number_unsigned() &&
        json.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        Fail(path, "integer is too large");
    }
    const auto number = json.get<std::int64_t>();
    if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) {
        Fail(path, "integer is out of range");
    }
    value = static_cast<int>(number);
}

template <std::unsigned_integral T>
    requires(!std::same_as<T, bool>)
void Read(const Json& json, const std::string& path, T& value) {
    if (!json.is_number_integer()) {
        Fail(path, "expected a non-negative integer, not " + TypeName(json));
    }
    if (!json.is_number_unsigned()) {
        Fail(path, "expected a non-negative integer");
    }
    const auto number = json.get<std::uint64_t>();
    if (number > std::numeric_limits<T>::max()) {
        Fail(path, "integer is too large");
    }
    value = static_cast<T>(number);
}

template <NamedEnum E> void Read(const Json& json, const std::string& path, E& value) {
    std::string name;
    Read(json, path, name);
    std::string expected;
    for (const auto& [candidate, enumerator] : EnumNames<E>::kValues) {
        if (candidate == name) {
            value = enumerator;
            return;
        }
        expected += (expected.empty() ? "" : ", ") + std::string(candidate);
    }
    Fail(path, "unknown value \"" + name + "\"; expected one of: " + expected);
}

template <typename T> void Read(const Json& json, const std::string& path, std::optional<T>& value);
template <typename T> void Read(const Json& json, const std::string& path, Boxed<T>& value);
template <typename T> void Read(const Json& json, const std::string& path, std::vector<T>& value);
template <typename... Ts>
void Read(const Json& json, const std::string& path, std::variant<Ts...>& value);

template <typename T> void AfterRead(const Json& /*json*/, T& /*value*/) {}

void AfterRead(const Json& /*json*/, Crater& crater) {
    crater.manual = true;
}

/// The list format a path's extension names when the configuration gives none.
template <typename Output> void InferListFormat(const Json& json, Output& output) {
    if (!json.contains("format")) {
        const std::string& path = output.path;
        const bool isJson = path.size() >= 5 && path.compare(path.size() - 5, 5, ".json") == 0;
        output.format = isJson ? CraterListFormat::Json : CraterListFormat::Csv;
    }
}

void AfterRead(const Json& json, CraterListOutput& output) {
    InferListFormat(json, output);
}

void AfterRead(const Json& json, RockListOutput& output) {
    InferListFormat(json, output);
}

template <HasFields T> void Read(const Json& json, const std::string& path, T& value) {
    Reader reader(json, path);
    Fields(reader, value);
    reader.Finish();
    AfterRead(json, value);
}

template <typename T>
void Read(const Json& json, const std::string& path, std::optional<T>& value) {
    if (json.is_null()) {
        value.reset();
        return;
    }
    T inner = value.value_or(T{});
    Read(json, path, inner);
    value = std::move(inner);
}

template <typename T> void Read(const Json& json, const std::string& path, Boxed<T>& value) {
    Read(json, path, *value);
}

template <typename T> void Read(const Json& json, const std::string& path, std::vector<T>& value) {
    if (!json.is_array()) {
        Fail(path, "expected an array, not " + TypeName(json));
    }
    std::vector<T> items(json.size());
    for (std::size_t i = 0; i < json.size(); ++i) {
        Read(json[i], Element(path, i), items[i]);
    }
    value = std::move(items);
}

template <typename... Ts>
void Read(const Json& json, const std::string& path, std::variant<Ts...>& value) {
    if (!json.is_object()) {
        Fail(path, "expected an object with a \"type\", not " + TypeName(json));
    }
    const auto type = json.find("type");
    std::string expected;
    ((expected += (expected.empty() ? "" : ", ") + std::string(SpecName<Ts>::kName)), ...);
    if (type == json.end() || !type->is_string()) {
        Fail(Child(path, "type"), "expected a string, one of: " + expected);
    }
    const std::string name = type->template get<std::string>();
    bool found = false;
    const auto tryAlternative = [&]<typename T>(T* /*tag*/) {
        if (found || name != SpecName<T>::kName) {
            return;
        }
        T spec{};
        Reader reader(json, path);
        reader.Accept("type");
        Fields(reader, spec);
        reader.Finish();
        AfterRead(json, spec);
        value = std::move(spec);
        found = true;
    };
    (tryAlternative(static_cast<Ts*>(nullptr)), ...);
    if (!found) {
        Fail(Child(path, "type"), "unknown type \"" + name + "\"; expected one of: " + expected);
    }
}

template <typename T> void Reader::operator()(std::string_view key, T& value) {
    known_.emplace(key);
    const auto found = json_.find(key);
    if (found != json_.end()) {
        Read(*found, Child(path_, key), value);
    }
}

Json ToJson(double value) {
    return value;
}

Json ToJson(float value) {
    return static_cast<double>(value);
}

Json ToJson(bool value) {
    return value;
}

Json ToJson(int value) {
    return value;
}

Json ToJson(const std::string& value) {
    return value;
}

template <std::unsigned_integral T>
    requires(!std::same_as<T, bool>)
Json ToJson(T value) {
    return static_cast<std::uint64_t>(value);
}

template <NamedEnum E> Json ToJson(E value) {
    for (const auto& [name, enumerator] : EnumNames<E>::kValues) {
        if (enumerator == value) {
            return std::string(name);
        }
    }
    return nullptr;
}

template <typename T> Json ToJson(const std::optional<T>& value);
template <typename T> Json ToJson(const Boxed<T>& value);
template <typename T> Json ToJson(const std::vector<T>& value);
template <typename... Ts> Json ToJson(const std::variant<Ts...>& value);

template <HasFields T> Json ToJson(const T& value) {
    Json json = Json::object();
    T copy = value;
    Writer writer(json);
    Fields(writer, copy);
    return json;
}

template <typename T> Json ToJson(const std::optional<T>& value) {
    return value ? ToJson(*value) : Json(nullptr);
}

template <typename T> Json ToJson(const Boxed<T>& value) {
    return ToJson(*value);
}

template <typename T> Json ToJson(const std::vector<T>& value) {
    Json json = Json::array();
    for (const T& item : value) {
        json.push_back(ToJson(item));
    }
    return json;
}

template <typename... Ts> Json ToJson(const std::variant<Ts...>& value) {
    return std::visit(
        [](const auto& spec) {
            using Spec = std::decay_t<decltype(spec)>;
            Json json = Json::object();
            json["type"] = std::string(SpecName<Spec>::kName);
            const Json fields = ToJson(spec);
            for (const auto& item : fields.items()) {
                json[item.key()] = item.value();
            }
            return json;
        },
        value);
}

template <typename T> void Writer::operator()(std::string_view key, T& value) {
    json_[std::string(key)] = ToJson(value);
}

template <typename IO> void Fields(IO& io, FlatSourceSpec& source) {
    io("width", source.width);
    io("height", source.height);
    io("pixel_size", source.pixelSize);
    io("elevation", source.elevation);
}

template <typename IO> void Fields(IO& io, RasterSourceSpec& source) {
    io("path", source.path);
    io("band", source.band);
    io("pixel_size", source.pixelSize);
    io("resampling", source.resampling);
    io("scale", source.scale);
    io("offset", source.offset);
}

template <typename IO> void Fields(IO& io, ChainSourceSpec& source) {
    io("sources", source.sources);
}

template <typename IO> void Fields(IO& io, NoiseLayerSpec& layer) {
    io("stack", layer.stack);
}

template <typename IO> void Fields(IO& io, HeightmapOutput& output) {
    io("path", output.path);
    io("compression", output.compression);
    io("tile_size", output.tileSize);
}

template <typename IO> void Fields(IO& io, NoiseLayerConfig& config) {
    NoiseLayer& layer = config.layer;
    FractalParams& fractal = layer.fractal;
    io("enabled", layer.enabled);
    io("basis", fractal.basis);
    io("fractal", fractal.type);
    io("blend", layer.blend);
    io("seed", fractal.seed);
    io("wavelength", fractal.wavelength);
    io("amplitude", fractal.amplitude);
    io("octaves", fractal.octaves);
    io("lacunarity", fractal.lacunarity);
    io("gain", fractal.gain);
    io("hurst", config.hurst);
    io("offset", fractal.offset);
    io("warp_amplitude", layer.warpAmplitude);
    io("warp_wavelength", layer.warpWavelength);
}

template <typename IO> void Fields(IO& io, PowerLawSegment& segment) {
    io("start", segment.start);
    io("coefficient", segment.coefficient);
    io("exponent", segment.exponent);
}

template <typename IO> void Fields(IO& io, CraterSpacing& spacing) {
    io("diameter_ratio", spacing.diameterRatio);
    io("separation", spacing.separation);
}

template <typename IO> void Fields(IO& io, Crater& crater) {
    io("x", crater.x);
    io("y", crater.y);
    io("diameter", crater.diameter);
    io("age_gyr", crater.ageGyr);
    io("id", crater.id);
}

template <typename IO> void Fields(IO& io, ResolutionFade& fade) {
    io("start_samples", fade.startSamples);
    io("end_samples", fade.endSamples);
    io("shape", fade.shape);
}

template <typename IO> void Fields(IO& io, CraterBlend& blend) {
    io("start_radii", blend.startRadii);
    io("end_radii", blend.endRadii);
    io("shape", blend.shape);
}

template <typename IO> void Fields(IO& io, CraterApplyOptions& apply) {
    io("order", apply.order);
    io("fade", apply.fade);
    io("datum", apply.datum);
    io("datum_fit_radii", apply.datumFitRadii);
    io("interior_replacement", apply.interiorReplacement);
    io("blend", apply.blend);
    io("irregularity", apply.irregularity);
}

template <typename IO> void Fields(IO& io, ValueDistribution& distribution) {
    io("kind", distribution.kind);
    io("a", distribution.a);
    io("b", distribution.b);
    std::optional<double> min;
    std::optional<double> max;
    if (std::isfinite(distribution.min)) {
        min = distribution.min;
    }
    if (std::isfinite(distribution.max)) {
        max = distribution.max;
    }
    io("min", min);
    io("max", max);
    distribution.min = min.value_or(-std::numeric_limits<double>::infinity());
    distribution.max = max.value_or(std::numeric_limits<double>::infinity());
}

template <typename IO> void Fields(IO& io, CraterIrregularity& irregularity) {
    io("enabled", irregularity.enabled);
    io("seed", irregularity.seed);
    io("min_diameter", irregularity.minDiameter);
    std::optional<double> maxDiameter;
    if (std::isfinite(irregularity.maxDiameter)) {
        maxDiameter = irregularity.maxDiameter;
    }
    io("max_diameter", maxDiameter);
    irregularity.maxDiameter = maxDiameter.value_or(std::numeric_limits<double>::infinity());
    io("min_age_gyr", irregularity.minAgeGyr);
    std::optional<double> maxAge;
    if (std::isfinite(irregularity.maxAgeGyr)) {
        maxAge = irregularity.maxAgeGyr;
    }
    io("max_age_gyr", maxAge);
    irregularity.maxAgeGyr = maxAge.value_or(std::numeric_limits<double>::infinity());
    io("manual", irregularity.manual);
    io("rough_share", irregularity.roughShare);
    io("model", irregularity.model);
    io("scatter", irregularity.scatter);
    io("max_deviation", irregularity.maxDeviation);
    io("roughness", irregularity.roughness);
    io("spectral_slope", irregularity.spectralSlope);
    io("min_harmonic", irregularity.minHarmonic);
    io("max_harmonic", irregularity.maxHarmonic);
    io("elongated_share", irregularity.elongatedShare);
    io("ellipticity", irregularity.ellipticity);
    io("heading", irregularity.heading);
    io("heading_spread", irregularity.headingSpread);
}

template <typename IO> void Fields(IO& io, RimSmoothing& smoothing) {
    io("sigma_radii", smoothing.sigmaRadii);
    io("inner_radii", smoothing.innerRadii);
    io("outer_radii", smoothing.outerRadii);
    io("shape", smoothing.shape);
    io("strength", smoothing.strength);
    io("fade", smoothing.fade);
}

template <typename IO> void Fields(IO& io, CraterLayerSpec& craters) {
    io("body", craters.body);
    io("generate", craters.generate);
    io("seed", craters.seed);
    io("density", craters.density);
    io("ages", craters.ages);
    io("min_diameter", craters.minDiameter);
    io("max_diameter", craters.maxDiameter);
    io("spacing", craters.spacing);
    io("max_craters", craters.maxCraters);
    io("manual", craters.manual);
    io("profile", craters.profile);
    io("degradation", craters.degradation);
    io("apply", craters.apply);
}

template <typename IO> void Fields(IO& io, RockPatches& patches) {
    io("spread", patches.spread);
    io("size", patches.size);
}

template <typename IO> void Fields(IO& io, RockEjecta& ejecta) {
    io("min_crater_diameter", ejecta.minCraterDiameter);
    io("regolith_thickness", ejecta.regolithThickness);
    io("excavation_depth_ratio", ejecta.excavationDepthRatio);
    io("coverage", ejecta.coverage);
    io("profile", ejecta.profile);
    io("reach", ejecta.reach);
    io("decay_radii", ejecta.decayRadii);
    io("floor_share", ejecta.floorShare);
    io("aging", ejecta.aging);
    io("min_freshness", ejecta.minFreshness);
    io("lifetime_gyr", ejecta.lifetimeGyr);
    io("aging_exponent", ejecta.agingExponent);
    io("gravity", ejecta.gravity);
}

template <typename IO> void Fields(IO& /*io*/, GolombekRappRocksSpec& /*spec*/) {}

template <typename IO> void Fields(IO& io, Sp8023IntercraterRocksSpec& spec) {
    io("curve", spec.curve);
}

template <typename IO> void Fields(IO& io, Sp8023CraterRocksSpec& spec) {
    io("curve", spec.curve);
}

template <typename IO> void Fields(IO& io, SynthterrainRocksSpec& spec) {
    io("model", spec.model);
}

template <typename IO> void Fields(IO& io, ExposureParams& exposure) {
    io("reach", exposure.reach);
    io("rise_soil", exposure.riseSoil);
    io("rise_rock", exposure.riseRock);
    io("slope_soil", exposure.slopeSoil);
    io("slope_rock", exposure.slopeRock);
    io("floor_cover", exposure.floorCover);
    io("floor_patch", exposure.floorPatch);
    io("seed", exposure.seed);
}

template <typename IO> void Fields(IO& io, RockLayerSpec& spec) {
    RockLayerOptions& rocks = spec.rocks;
    io("coverage", rocks.coverage);
    io("qa", rocks.qa);
    io("qb", rocks.qb);
    io("min_diameter", rocks.minDiameter);
    io("max_diameter", rocks.maxDiameter);
    io("sizes", spec.sizes);
    io("seed", rocks.seed);
    io("patches", rocks.patches);
    io("ejecta", rocks.ejecta);
    io("ejecta_sizes", spec.ejectaSizes);
    io("extrapolate", spec.extrapolate);
    io("exposure", rocks.exposure);
    io("bedrock_abundance", rocks.bedrockAbundance);
    io("exposure_spacing", rocks.exposureSpacing);
    io("min_height_ratio", rocks.minHeightRatio);
    io("max_height_ratio", rocks.maxHeightRatio);
    io("min_burial", rocks.minBurial);
    io("max_burial", rocks.maxBurial);
    io("max_lean", rocks.maxLean);
    io("relief", rocks.relief);
    io("models", rocks.models);
    io("shapes", rocks.shapes);
    io("shape_prototypes", rocks.shapePrototypes);
    io("fade", rocks.fade);
}

template <typename IO> void Fields(IO& io, RoughnessLayerSpec& spec) {
    RoughnessOptions& roughness = spec.roughness;
    io("rms_slope", roughness.rmsSlope);
    io("baseline", roughness.baseline);
    io("hurst", roughness.hurst);
    io("max_wavelength", roughness.maxWavelength);
    io("min_wavelength", roughness.minWavelength);
    io("seed", roughness.seed);
    io("fade", roughness.fade);
}

template <typename IO> void Fields(IO& io, BedformLayerSpec& spec) {
    BedformOptions& bedforms = spec.bedforms;
    io("wavelength", bedforms.wavelength);
    io("height", bedforms.height);
    io("wind_azimuth", bedforms.windAzimuth);
    io("azimuth_spread", bedforms.azimuthSpread);
    io("lee_fraction", bedforms.leeFraction);
    io("sinuosity", bedforms.sinuosity);
    io("cover", bedforms.cover);
    io("field_size", bedforms.fieldSize);
    io("seed", bedforms.seed);
    io("exposure", bedforms.exposure);
    io("exposure_spacing", bedforms.exposureSpacing);
    io("fade", bedforms.fade);
}

template <typename IO> void Fields(IO& io, ScaleLayerSpec& spec) {
    io("scale", spec.scale);
    io("offset", spec.offset);
}

template <typename IO> void Fields(IO& io, ClampLayerSpec& spec) {
    io("min", spec.min);
    io("max", spec.max);
}

template <typename IO> void Fields(IO& io, RectangleRegionSpec& spec) {
    io("min_x", spec.minX);
    io("min_y", spec.minY);
    io("max_x", spec.maxX);
    io("max_y", spec.maxY);
}

template <typename IO> void Fields(IO& io, CircleRegionSpec& spec) {
    io("x", spec.x);
    io("y", spec.y);
    io("radius", spec.radius);
}

template <typename IO> void Fields(IO& io, RegionLayerSpec& spec) {
    io("shape", spec.shape);
    io("feather", spec.feather);
    io("blend", spec.blend);
    io("layer", spec.layer);
}

template <typename IO> void Fields(IO& io, LunarSurfaceSpec& spec) {
    io("age_gyr", spec.ageGyr);
}

template <typename IO> void Fields(IO& io, MarsSurfaceSpec& spec) {
    io("age_gyr", spec.ageGyr);
}

template <typename IO> void Fields(IO& io, PowerLawSpec& spec) {
    io("coefficient", spec.coefficient);
    io("exponent", spec.exponent);
}

template <typename IO> void Fields(IO& io, PiecewisePowerLawSpec& spec) {
    io("segments", spec.segments);
}

template <typename IO> void Fields(IO& io, TabulatedSpec& spec) {
    io("diameters", spec.diameters);
    io("cumulative", spec.cumulative);
    io("diameter_unit", spec.diameterUnit);
    io("area_unit", spec.areaUnit);
}

template <typename IO> void Fields(IO& /*io*/, TraskSpec& /*spec*/) {}

template <typename IO> void Fields(IO& /*io*/, HartmannSpec& /*spec*/) {}

template <typename IO> void Fields(IO& io, MooreSpec& spec) {
    io("terrain", spec.terrain);
}

template <typename IO> void Fields(IO& /*io*/, ViperSpec& /*spec*/) {}

template <typename IO> void Fields(IO& /*io*/, AutoAgesSpec& /*spec*/) {}

template <typename IO> void Fields(IO& /*io*/, FreshAgesSpec& /*spec*/) {}

template <typename IO> void Fields(IO& io, UniformAgesSpec& spec) {
    io("lifetime_gyr", spec.lifetimeGyr);
}

template <typename IO> void Fields(IO& io, SteadyStateAgesSpec& spec) {
    io("age_gyr", spec.ageGyr);
    io("equilibrium", spec.equilibrium);
    io("extended_production", spec.extendedProduction);
}

template <typename IO> void Fields(IO& io, FreshnessDegradationSpec& spec) {
    io("cutoff_depth_ratio", spec.cutoffDepthRatio);
}

template <typename IO> void Fields(IO& io, NoDegradationSpec& spec) {
    io("depth_ratio", spec.depthRatio);
}

template <typename IO> void Fields(IO& io, FassettThomsonSpec& spec) {
    io("rim", spec.rim);
}

template <typename IO> void Fields(IO& /*io*/, MartinParkesDunstanSpec& /*spec*/) {}

template <typename IO> void Fields(IO& io, ComplexProfileSpec& spec) {
    io("simple", spec.simple);
}

template <typename IO> void Fields(IO& io, RockListOutput& output) {
    io("path", output.path);
    io("format", output.format);
}

template <typename IO> void Fields(IO& io, BoulderModelsOutput& output) {
    io("path", output.path);
    io("crease_angle", output.creaseAngle);
}

template <typename IO> void Fields(IO& io, RockModels& models) {
    io("min_diameter", models.minDiameter);
    io("min_samples", models.minSamples);
}

template <typename IO> void Fields(IO& io, ValueRange& range) {
    io("min", range.min);
    io("max", range.max);
}

template <typename IO> void Fields(IO& io, BoulderStyle& style) {
    io("name", style.name);
    io("weight", style.weight);
    io("solid", style.solid);
    io("intermediate", style.intermediate);
    io("short_axis", style.shortAxis);
    io("rounding", style.rounding);
    io("min_fractures", style.minFractures);
    io("max_fractures", style.maxFractures);
    io("fracture_depth", style.fractureDepth);
    io("max_fracture_tilt", style.maxFractureTilt);
    io("noise_amplitude", style.noiseAmplitude);
    io("noise_wavelength", style.noiseWavelength);
    io("noise_octaves", style.noiseOctaves);
}

template <typename IO> void Fields(IO& io, BoulderShapeOptions& shapes) {
    io("styles", shapes.styles);
    io("subdivisions", shapes.subdivisions);
    io("seed", shapes.seed);
}

template <typename IO> void Fields(IO& io, CraterListOutput& output) {
    io("path", output.path);
    io("format", output.format);
}

TerrainConfig ReadConfig(const Json& json) {
    Reader reader(json, "");
    if (!reader.Has("schema_version")) {
        Fail("schema_version",
             "required; this library reads version " + std::to_string(kConfigSchemaVersion));
    }
    TerrainConfig config;
    reader("schema_version", config.schemaVersion);
    if (config.schemaVersion != kConfigSchemaVersion) {
        Fail("schema_version", "version " + std::to_string(config.schemaVersion) +
                                   " is not supported; this library reads version " +
                                   std::to_string(kConfigSchemaVersion));
    }
    reader("source", config.source);
    reader("layers", config.layers);
    reader("filters", config.filters);
    reader("outputs", config.outputs);
    reader.Finish();
    return config;
}

}  // namespace

ConfigError::ConfigError(std::string path, const std::string& message)
    : std::runtime_error(path.empty() ? message : path + ": " + message), path_(std::move(path)) {}

TerrainConfig ParseConfig(std::string_view json) {
    Json document;
    try {
        document = Json::parse(json);
    } catch (const Json::exception& error) {
        throw ConfigError("", std::string("invalid JSON: ") + error.what());
    }
    return ReadConfig(document);
}

TerrainConfig LoadConfig(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw ConfigError("", "cannot read " + path.string());
    }
    std::ostringstream text;
    text << file.rdbuf();
    return ParseConfig(text.str());
}

std::string ConfigToJson(const TerrainConfig& config, int indent) {
    Json json = Json::object();
    json["schema_version"] = config.schemaVersion;
    json["source"] = ToJson(config.source);
    json["layers"] = ToJson(config.layers);
    json["filters"] = ToJson(config.filters);
    json["outputs"] = ToJson(config.outputs);
    return json.dump(indent) + "\n";
}

}  // namespace terraingen
