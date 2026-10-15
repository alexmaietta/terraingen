/// @file
/// @brief `ConfigSchema` and `ConfigCatalog`: the configuration format described from its field
/// lists.
#include "terraingen/pipeline/detail/json_io.h"

#include <map>
#include <typeindex>

namespace terraingen {

namespace config {

namespace {

/// Every alternative of the variant `std::variant<Ts...>`, default-constructed, with its "type".
template <typename... Ts> Json Alternatives(const std::variant<Ts...>* /*tag*/) {
    Json json = Json::array();
    (json.push_back(ToJson(std::variant<Ts...>(Ts{}))), ...);
    return json;
}

template <typename Variant> Json Alternatives() {
    return Alternatives(static_cast<const Variant*>(nullptr));
}

/// What a catalog lists for a field: every alternative of a variant (or of a list of them), and
/// the default of anything else, an optional's value included.
template <typename T> Json CatalogEntry(const T& value) {
    return ToJson(value);
}
template <typename... Ts> Json CatalogEntry(const std::variant<Ts...>& /*value*/) {
    return Alternatives<std::variant<Ts...>>();
}
template <typename... Ts> Json CatalogEntry(const std::vector<std::variant<Ts...>>& /*value*/) {
    return Alternatives<std::variant<Ts...>>();
}
template <typename T> Json CatalogEntry(const std::optional<T>& /*value*/) {
    return ToJson(T{});
}

/// Writes each field a `Fields` function visits as its `CatalogEntry`.
class CatalogWriter {
  public:
    explicit CatalogWriter(Json& json) : json_(json) {}

    template <typename T>
    void operator()(std::string_view key, T& value, std::string_view /*description*/ = {},
                    std::string_view /*unit*/ = {}) {
        json_[std::string(key)] = CatalogEntry(value);
    }

  private:
    Json& json_;
};

/// What `ConfigSchema` collects: each struct and variant described once under an id, so
/// recursive types (a chain of sources) refer to themselves.
struct SchemaBuilder {
    Json definitions = Json::object();
    std::map<std::type_index, std::string> ids;

    /// A reference to `T`'s definition, made by `describe` the first time.
    template <typename T, typename Describe> Json Reference(Describe describe) {
        const auto found = ids.find(typeid(T));
        if (found != ids.end()) {
            return Json{{"kind", "ref"}, {"ref", found->second}};
        }
        const std::string id = "d" + std::to_string(ids.size());
        ids.emplace(typeid(T), id);
        definitions[id] = describe();
        return Json{{"kind", "ref"}, {"ref", id}};
    }
};

/// `Describe(builder)`: the schema of `T`.
template <typename T> struct Describer;

/// Describes each field a `Fields` function visits: its name, schema and default.
class SchemaWriter {
  public:
    SchemaWriter(SchemaBuilder& builder, Json& fields) : builder_(builder), fields_(fields) {}

    template <typename T>
    void operator()(std::string_view key, T& value, std::string_view description = {},
                    std::string_view unit = {}) {
        fields_.push_back(Json{{"name", std::string(key)},
                               {"description", std::string(description)},
                               {"unit", std::string(unit)},
                               {"schema", Describer<T>::Describe(builder_)},
                               {"default", ToJson(value)}});
    }

  private:
    SchemaBuilder& builder_;
    Json& fields_;
};

/// The described fields of a default `T`.
template <typename T> Json DescribeFields(SchemaBuilder& builder) {
    Json fields = Json::array();
    T value{};
    SchemaWriter writer(builder, fields);
    Fields(writer, value);
    return fields;
}

template <> struct Describer<double> {
    static Json Describe(SchemaBuilder& /*builder*/) {
        return Json{{"kind", "number"}};
    }
};

template <> struct Describer<float> : Describer<double> {};

template <> struct Describer<int> {
    static Json Describe(SchemaBuilder& /*builder*/) {
        return Json{{"kind", "integer"}};
    }
};

template <std::unsigned_integral T>
    requires(!std::same_as<T, bool>)
struct Describer<T> {
    static Json Describe(SchemaBuilder& /*builder*/) {
        return Json{{"kind", "integer"}, {"minimum", 0}};
    }
};

template <> struct Describer<bool> {
    static Json Describe(SchemaBuilder& /*builder*/) {
        return Json{{"kind", "boolean"}};
    }
};

template <> struct Describer<std::string> {
    static Json Describe(SchemaBuilder& /*builder*/) {
        return Json{{"kind", "string"}};
    }
};

template <NamedEnum E> struct Describer<E> {
    static Json Describe(SchemaBuilder& /*builder*/) {
        Json choices = Json::array();
        Json descriptions = Json::array();
        for (const auto& entry : EnumNames<E>::kValues) {
            choices.push_back(std::string(entry.name));
            descriptions.push_back(std::string(entry.description));
        }
        return Json{{"kind", "enum"}, {"choices", choices}, {"descriptions", descriptions}};
    }
};

template <typename T> struct Describer<std::optional<T>> {
    static Json Describe(SchemaBuilder& builder) {
        return Json{{"kind", "optional"},
                    {"of", Describer<T>::Describe(builder)},
                    {"default", ToJson(T{})}};
    }
};

template <typename T> struct Describer<Boxed<T>> : Describer<T> {};

template <typename T> struct Describer<std::vector<T>> {
    static Json Describe(SchemaBuilder& builder) {
        return Json{
            {"kind", "array"}, {"of", Describer<T>::Describe(builder)}, {"default", ToJson(T{})}};
    }
};

template <typename... Ts> struct Describer<std::variant<Ts...>> {
    template <typename T> static Json Alternative(SchemaBuilder& builder) {
        return Json{{"type", std::string(SpecName<T>::kName)},
                    {"description", std::string(SpecName<T>::kDescription)},
                    {"fields", DescribeFields<T>(builder)},
                    {"default", ToJson(std::variant<Ts...>(T{}))}};
    }

    static Json Describe(SchemaBuilder& builder) {
        return builder.Reference<std::variant<Ts...>>([&] {
            Json alternatives = Json::array();
            (alternatives.push_back(Alternative<Ts>(builder)), ...);
            return Json{{"kind", "choice"}, {"alternatives", alternatives}};
        });
    }
};

template <HasFields T> struct Describer<T> {
    static Json Describe(SchemaBuilder& builder) {
        return builder.Reference<T>(
            [&] { return Json{{"kind", "object"}, {"fields", DescribeFields<T>(builder)}}; });
    }
};

}  // namespace

}  // namespace config

std::string ConfigCatalog(int indent) {
    using config::Alternatives;
    using config::Json;
    Json json = Json::object();
    TerrainConfig defaults;
    config::CatalogWriter writer(json);
    config::Fields(writer, defaults);
    Json models = Json::object();
    models["noise_layer"] = config::ToJson(NoiseLayerConfig{});
    models["density"] = Alternatives<DensitySpec>();
    models["ages"] = Alternatives<AgesSpec>();
    models["degradation"] = Alternatives<DegradationSpec>();
    models["profile"] = Alternatives<ProfileSpec>();
    models["boulder_sizes"] = Alternatives<BoulderSizesSpec>();
    models["region_shape"] = Alternatives<RegionShapeSpec>();
    models["albedo_source"] = Alternatives<AlbedoSourceSpec>();
    models["albedo_layer"] = Alternatives<AlbedoLayerSpec>();
    json["models"] = models;
    return json.dump(indent) + "\n";
}

std::string ConfigSchema(int indent) {
    using config::Json;
    config::SchemaBuilder builder;
    Json fields = config::DescribeFields<TerrainConfig>(builder);
    for (Json& field : fields) {
        if (field["name"] == "schema_version") {
            field["schema"]["fixed"] = true;
        }
    }
    Json json = Json::object();
    json["schema_version"] = kConfigSchemaVersion;
    json["root"] = Json{{"kind", "object"}, {"fields", fields}};
    json["definitions"] = builder.definitions;
    return json.dump(indent) + "\n";
}

}  // namespace terraingen
