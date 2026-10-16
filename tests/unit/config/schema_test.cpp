/// @file
/// @brief Tests of `ConfigSchema` and `ConfigCatalog` (pipeline/config_schema.cpp).
#include "terraingen/config/json.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using terraingen::AgesSpec;
using terraingen::DensitySpec;
using terraingen::LayerSpec;
using terraingen::ParseConfig;

namespace {

using SchemaJson = nlohmann::json;

/// The schema `node` stands for, following a reference into `schema`'s definitions.
const SchemaJson& Resolve(const SchemaJson& schema, const SchemaJson& node) {
    return node["kind"] == "ref" ? schema["definitions"].at(node["ref"].get<std::string>()) : node;
}

/// Checks that `value` fits `node` (as a field's default must), recursing into objects, choices
/// and arrays; counts the kinds seen.
void CheckFits(const SchemaJson& schema, const SchemaJson& node, const SchemaJson& value,
               const std::string& where, std::map<std::string, int>& kinds) {
    const SchemaJson& resolved = Resolve(schema, node);
    const std::string kind = resolved["kind"];
    ++kinds[kind];
    if (kind == "number") {
        EXPECT_TRUE(value.is_number()) << where;
    } else if (kind == "integer") {
        EXPECT_TRUE(value.is_number_integer()) << where;
    } else if (kind == "boolean") {
        EXPECT_TRUE(value.is_boolean()) << where;
    } else if (kind == "string") {
        EXPECT_TRUE(value.is_string()) << where;
    } else if (kind == "enum") {
        ASSERT_TRUE(value.is_string()) << where;
        const SchemaJson& choices = resolved["choices"];
        EXPECT_NE(std::find(choices.begin(), choices.end(), value), choices.end()) << where;
    } else if (kind == "optional") {
        if (!value.is_null()) {
            CheckFits(schema, resolved["of"], value, where, kinds);
        }
        CheckFits(schema, resolved["of"], resolved["default"], where + "?", kinds);
    } else if (kind == "array") {
        ASSERT_TRUE(value.is_array()) << where;
        for (const SchemaJson& item : value) {
            CheckFits(schema, resolved["of"], item, where + "[]", kinds);
        }
    } else if (kind == "object") {
        ASSERT_TRUE(value.is_object()) << where;
        for (const SchemaJson& field : resolved["fields"]) {
            const std::string name = field["name"];
            ASSERT_TRUE(value.contains(name)) << where << "." << name;
            CheckFits(schema, field["schema"], value[name], where + "." + name, kinds);
        }
    } else if (kind == "choice") {
        ASSERT_TRUE(value.is_object() && value.contains("type")) << where;
        bool found = false;
        for (const SchemaJson& alternative : resolved["alternatives"]) {
            if (alternative["type"] != value["type"]) {
                continue;
            }
            found = true;
            for (const SchemaJson& field : alternative["fields"]) {
                const std::string name = field["name"];
                ASSERT_TRUE(value.contains(name)) << where << "." << name;
                CheckFits(schema, field["schema"], value[name], where + "." + name, kinds);
            }
        }
        EXPECT_TRUE(found) << where << " has type " << value["type"];
    } else {
        ADD_FAILURE() << where << ": unknown kind " << kind;
    }
}

/// The field `name` of the object schema `object`.
SchemaJson FieldOf(const SchemaJson& object, const std::string& name) {
    for (const SchemaJson& f : object["fields"]) {
        if (f["name"] == name) {
            return f;
        }
    }
    ADD_FAILURE() << "no field " << name;
    return SchemaJson::object();
}

/// The alternative `type` of the choice schema `choice`.
SchemaJson AlternativeOf(const SchemaJson& choice, const std::string& type) {
    for (const SchemaJson& a : choice["alternatives"]) {
        if (a["type"] == type) {
            return a;
        }
    }
    ADD_FAILURE() << "no alternative " << type;
    return SchemaJson::object();
}

}  // namespace

TEST(ConfigSchema, DescribesEveryFieldAndEveryDefaultFitsIt) {
    const SchemaJson schema = SchemaJson::parse(terraingen::ConfigSchema());
    EXPECT_EQ(schema["schema_version"], terraingen::kConfigSchemaVersion);
    const SchemaJson& root = schema["root"];
    std::vector<std::string> names;
    for (const SchemaJson& field : root["fields"]) {
        names.push_back(field["name"]);
    }
    EXPECT_EQ(names, (std::vector<std::string>{"schema_version", "surface", "source", "layers",
                                               "filters", "outputs", "albedo"}));

    // Every alternative's default, and every field's, fits its schema.
    std::map<std::string, int> kinds;
    for (const auto& [id, definition] : schema["definitions"].items()) {
        if (definition["kind"] == "choice") {
            for (const SchemaJson& alternative : definition["alternatives"]) {
                CheckFits(schema, definition, alternative["default"],
                          id + ":" + alternative["type"].get<std::string>(), kinds);
            }
        } else {
            for (const SchemaJson& field : definition["fields"]) {
                CheckFits(schema, field["schema"], field["default"],
                          id + "." + field["name"].get<std::string>(), kinds);
            }
        }
    }
    for (const char* kind : {"number", "integer", "boolean", "string", "enum", "optional", "array",
                             "object", "choice"}) {
        EXPECT_GT(kinds[kind], 0) << kind;
    }

    // A default configuration built from the schema parses.
    SchemaJson config = SchemaJson::object();
    for (const SchemaJson& field : root["fields"]) {
        config[field["name"].get<std::string>()] = field["default"];
    }
    EXPECT_NO_THROW(ParseConfig(config.dump())) << config.dump();
}

TEST(ConfigSchema, DescribesEveryFieldTypeAndChoice) {
    const SchemaJson schema = SchemaJson::parse(terraingen::ConfigSchema());
    std::vector<std::string> missing;
    std::set<std::string> enums;
    const auto describedAs = [&](const SchemaJson& item, const std::string& where) {
        const std::string text = item.value("description", "");
        if (text.empty()) {
            missing.push_back(where);
            return;
        }
        EXPECT_TRUE(std::all_of(text.begin(), text.end(),
                                [](unsigned char c) { return c >= 32 && c < 127; }))
            << where << " is not plain ASCII: " << text;
        EXPECT_EQ(text.back(), '.') << where << " does not end with a full stop: " << text;
        EXPECT_LE(text.size(), 400u) << where << " is long for a tooltip";
    };
    const auto checkEnums = [&](const SchemaJson& node, const std::string& where) {
        const SchemaJson* resolved = &node;
        while ((*resolved)["kind"] == "optional" || (*resolved)["kind"] == "array") {
            resolved = &(*resolved)["of"];
        }
        if ((*resolved)["kind"] != "enum" || !enums.insert(resolved->dump()).second) {
            return;
        }
        const SchemaJson& choices = (*resolved)["choices"];
        const SchemaJson& descriptions = (*resolved)["descriptions"];
        ASSERT_EQ(descriptions.size(), choices.size()) << where;
        for (std::size_t i = 0; i < choices.size(); ++i) {
            describedAs(SchemaJson{{"description", descriptions[i]}},
                        where + "=" + choices[i].get<std::string>());
        }
    };
    const auto checkFields = [&](const SchemaJson& fields, const std::string& where) {
        for (const SchemaJson& field : fields) {
            const std::string at = where + "." + field["name"].get<std::string>();
            describedAs(field, at);
            checkEnums(field["schema"], at);
        }
    };
    checkFields(schema["root"]["fields"], "root");
    for (const auto& [id, definition] : schema["definitions"].items()) {
        if (definition["kind"] == "choice") {
            for (const SchemaJson& alternative : definition["alternatives"]) {
                const std::string type = alternative["type"];
                describedAs(alternative, type);
                checkFields(alternative["fields"], type);
            }
        } else {
            checkFields(definition["fields"], id);
        }
    }
    std::string list;
    for (const std::string& where : missing) {
        list += "\n  " + where;
    }
    EXPECT_TRUE(missing.empty()) << missing.size() << " without a description:" << list;
}

TEST(ConfigSchema, GivesUnitsOnlyToNumbersAndFromAFixedSet) {
    const SchemaJson schema = SchemaJson::parse(terraingen::ConfigSchema());
    const std::set<std::string> units = {"",    "m",   "m^2",   "km",     "deg", "rad",    "Gyr",
                                         "Myr", "1/m", "1/m^2", "1/km^2", "px",  "m^2/Myr"};
    int withUnits = 0;
    const auto numeric = [&](const SchemaJson& node) {
        const SchemaJson* resolved = &node;
        while ((*resolved)["kind"] == "optional" || (*resolved)["kind"] == "array") {
            resolved = &(*resolved)["of"];
        }
        return (*resolved)["kind"] == "number" || (*resolved)["kind"] == "integer";
    };
    const auto check = [&](const SchemaJson& fields, const std::string& where) {
        for (const SchemaJson& field : fields) {
            const std::string unit = field.value("unit", "");
            const std::string at = where + "." + field["name"].get<std::string>();
            EXPECT_TRUE(units.contains(unit)) << at << " has unit \"" << unit << "\"";
            if (!unit.empty()) {
                ++withUnits;
                EXPECT_TRUE(numeric(field["schema"])) << at << " is not a number";
            }
        }
    };
    check(schema["root"]["fields"], "root");
    for (const auto& [id, definition] : schema["definitions"].items()) {
        if (definition["kind"] == "choice") {
            for (const SchemaJson& alternative : definition["alternatives"]) {
                check(alternative["fields"], alternative["type"].get<std::string>());
            }
        } else {
            check(definition["fields"], id);
        }
    }
    EXPECT_GT(withUnits, 50) << "lengths, angles and ages carry units";
}

TEST(ConfigSchema, ListsTheParsersChoicesAndRefersToRecursiveTypes) {
    const SchemaJson schema = SchemaJson::parse(terraingen::ConfigSchema());
    const SchemaJson source = FieldOf(schema["root"], "source")["schema"];
    ASSERT_EQ(source["kind"], "ref");
    const SchemaJson& sources = Resolve(schema, source);
    ASSERT_EQ(sources["kind"], "choice");
    const SchemaJson chain = FieldOf(AlternativeOf(sources, "chain"), "sources")["schema"];
    EXPECT_EQ(chain["kind"], "array");
    EXPECT_EQ(chain["of"], source) << "a chain holds sources of the same choice";

    const SchemaJson layers = FieldOf(schema["root"], "layers")["schema"];
    ASSERT_EQ(layers["kind"], "array");
    const SchemaJson& layerChoice = Resolve(schema, layers["of"]);
    EXPECT_EQ(layerChoice["alternatives"].size(), std::variant_size_v<LayerSpec>);
    const SchemaJson stack = FieldOf(AlternativeOf(layerChoice, "noise"), "stack")["schema"];
    const SchemaJson& noise = Resolve(schema, stack["of"]);
    const SchemaJson basis = FieldOf(noise, "basis")["schema"];
    ASSERT_EQ(basis["kind"], "enum");
    EXPECT_EQ(basis["choices"].size(), 6u);
    for (const SchemaJson& choice : basis["choices"]) {
        const std::string config =
            R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"basis": )" +
            choice.dump() + "}]}]}";
        EXPECT_NO_THROW(ParseConfig(config)) << choice;
    }
    const SchemaJson octaves = FieldOf(noise, "octaves");
    EXPECT_EQ(octaves["schema"]["kind"], "integer");
    EXPECT_EQ(octaves["default"], 8);
    const SchemaJson hurst = FieldOf(noise, "hurst");
    EXPECT_EQ(hurst["schema"]["kind"], "optional");
    EXPECT_TRUE(hurst["default"].is_null());
    EXPECT_TRUE(hurst["schema"]["default"].is_number());
    const SchemaJson version = FieldOf(schema["root"], "schema_version");
    EXPECT_EQ(version["schema"], (SchemaJson{{"kind", "integer"}, {"fixed", true}}));
    EXPECT_EQ(version["default"], terraingen::kConfigSchemaVersion);
    EXPECT_TRUE(FieldOf(schema["root"], "albedo")["default"].is_null());
}

TEST(ConfigSchema, SharesTheOutputPathDescriptions) {
    const SchemaJson schema = SchemaJson::parse(terraingen::ConfigSchema());
    const SchemaJson outputs = FieldOf(schema["root"], "outputs")["schema"];
    const SchemaJson& choice = Resolve(schema, outputs["of"]);
    EXPECT_EQ(FieldOf(AlternativeOf(choice, "heightmap"), "path")["description"],
              "GeoTIFF file to write; relative paths resolve against the output folder (the CLI's "
              "--output-dir), else the configuration's folder.");
    EXPECT_EQ(FieldOf(AlternativeOf(choice, "albedo"), "path"),
              FieldOf(AlternativeOf(choice, "heightmap"), "path"));
    EXPECT_EQ(FieldOf(AlternativeOf(choice, "crater_list"), "format"),
              FieldOf(AlternativeOf(choice, "boulder_list"), "format"));
}

TEST(ConfigCatalog, ListsEveryTypeAndEachEntryParsesWhereItBelongs) {
    using Json = nlohmann::json;
    const Json catalog = Json::parse(terraingen::ConfigCatalog());
    EXPECT_EQ(catalog["schema_version"], terraingen::kConfigSchemaVersion);
    const auto ordered = nlohmann::ordered_json::parse(terraingen::ConfigCatalog());
    std::vector<std::string> keys;
    for (const auto& [key, value] : ordered.items()) {
        keys.push_back(key);
    }
    EXPECT_EQ(keys, (std::vector<std::string>{"schema_version", "surface", "source", "layers",
                                              "filters", "outputs", "albedo", "models"}));
    const auto types = [](const Json& list) {
        std::vector<std::string> names;
        for (const Json& entry : list) {
            names.push_back(entry["type"].get<std::string>());
        }
        return names;
    };
    EXPECT_EQ(types(catalog["surface"]), (std::vector<std::string>{"plane", "sphere"}));
    EXPECT_EQ(catalog["layers"].size(), std::variant_size_v<LayerSpec>);
    EXPECT_EQ(catalog["outputs"].size(), std::variant_size_v<terraingen::OutputSpec>);
    EXPECT_EQ(catalog["models"]["density"].size(), std::variant_size_v<DensitySpec>);
    EXPECT_EQ(catalog["models"]["ages"].size(), std::variant_size_v<AgesSpec>);

    const auto parses = [](const Json& config, const std::string& what) {
        Json document = config;
        document["schema_version"] = terraingen::kConfigSchemaVersion;
        EXPECT_NO_THROW(ParseConfig(document.dump())) << what << ": " << document.dump();
    };
    for (const Json& surface : catalog["surface"]) {
        parses({{"surface", surface}}, "surface");
    }
    for (const Json& source : catalog["source"]) {
        parses({{"source", source}}, "source");
    }
    for (const char* list : {"layers", "filters", "outputs"}) {
        for (const Json& entry : catalog[list]) {
            parses({{list, Json::array({entry})}}, list);
        }
    }
    parses({{"albedo", catalog["albedo"]}}, "albedo");

    const auto layerOf = [&](const std::string& type) {
        for (const Json& layer : catalog["layers"]) {
            if (layer["type"] == type) {
                return layer;
            }
        }
        ADD_FAILURE() << "no " << type << " layer";
        return Json::object();
    };
    const Json& models = catalog["models"];
    for (const auto& [model, key] : {std::pair<const char*, const char*>{"density", "density"},
                                     {"ages", "ages"},
                                     {"degradation", "degradation"},
                                     {"profile", "profile"}}) {
        for (const Json& entry : models[model]) {
            Json craters = layerOf("craters");
            craters[key] = entry;
            parses({{"layers", Json::array({craters})}}, model);
        }
    }
    for (const Json& entry : models["boulder_sizes"]) {
        Json boulders = layerOf("boulders");
        boulders["sizes"] = entry;
        parses({{"layers", Json::array({boulders})}}, "boulder_sizes");
    }
    for (const Json& entry : models["region_shape"]) {
        Json region = layerOf("region");
        region["shape"] = entry;
        parses({{"layers", Json::array({region})}}, "region_shape");
    }
    Json noise = layerOf("noise");
    noise["stack"] = Json::array({models["noise_layer"]});
    parses({{"layers", Json::array({noise})}}, "noise_layer");
    for (const Json& entry : models["albedo_source"]) {
        Json albedo = catalog["albedo"];
        albedo["source"] = entry;
        parses({{"albedo", albedo}}, "albedo_source");
    }
    for (const Json& entry : models["albedo_layer"]) {
        Json albedo = catalog["albedo"];
        albedo["layers"] = Json::array({entry});
        parses({{"albedo", albedo}}, "albedo_layer");
    }
}

TEST(ConfigSchema, ListsProductionSurfacesMinimumsAndTheirModels) {
    const SchemaJson schema = SchemaJson::parse(terraingen::ConfigSchema());
    const SchemaJson layers = FieldOf(schema["root"], "layers")["schema"];
    const SchemaJson& layerChoice = Resolve(schema, layers["of"]);
    const SchemaJson craters = AlternativeOf(layerChoice, "craters");
    const SchemaJson density = FieldOf(craters, "density")["schema"];
    const SchemaJson& densities = Resolve(schema, density);
    ASSERT_EQ(densities["kind"], "choice");
    EXPECT_EQ(densities["alternatives"].size(), std::variant_size_v<DensitySpec>);

    const SchemaJson surface = AlternativeOf(densities, "production_surface");
    EXPECT_EQ(FieldOf(surface, "production")["schema"]["choices"],
              (SchemaJson{"neukum", "extended_neukum", "ivanov"}));
    EXPECT_EQ(FieldOf(surface, "chronology")["schema"]["choices"],
              (SchemaJson{"neukum", "ivanov"}));
    EXPECT_EQ(FieldOf(surface, "age_gyr")["unit"], "Gyr");
    const SchemaJson distributions =
        FieldOf(AlternativeOf(densities, "minimum"), "distributions")["schema"];
    EXPECT_EQ(distributions["kind"], "array");
    EXPECT_EQ(distributions["of"], density) << "a minimum holds densities of the same choice";

    const SchemaJson ages = Resolve(schema, FieldOf(craters, "ages")["schema"]);
    std::vector<std::string> types;
    for (const SchemaJson& alternative : ages["alternatives"]) {
        types.push_back(alternative["type"]);
    }
    EXPECT_EQ(types, (std::vector<std::string>{"uniform", "fresh", "steady_state"}));
    EXPECT_EQ(FieldOf(craters, "ages")["default"],
              (SchemaJson{{"type", "uniform"}, {"lifetime_gyr", 1.0}}));
    EXPECT_EQ(FieldOf(craters, "density")["default"]["type"], "power_law");
    const SchemaJson steady = AlternativeOf(ages, "steady_state");
    EXPECT_EQ(FieldOf(steady, "production")["schema"]["choices"],
              (SchemaJson{"neukum", "extended_neukum", "ivanov"}));
    EXPECT_EQ(FieldOf(steady, "equilibrium")["schema"]["choices"],
              (SchemaJson{"trask", "hartmann"}));
    EXPECT_EQ(FieldOf(steady, "chronology")["schema"]["choices"], (SchemaJson{"neukum", "ivanov"}));

    const SchemaJson morphometry = FieldOf(craters, "morphometry");
    EXPECT_EQ(morphometry["schema"]["choices"], (SchemaJson{"lunar", "mars"}));
    EXPECT_EQ(morphometry["default"], "lunar");
    EXPECT_EQ(FieldOf(AlternativeOf(layerChoice, "secondaries"), "morphometry")["schema"],
              morphometry["schema"]);
}

TEST(ConfigSchema, GivesASphereOnlyARadiusInMetres) {
    const SchemaJson schema = SchemaJson::parse(terraingen::ConfigSchema());
    const SchemaJson surfaces = Resolve(schema, FieldOf(schema["root"], "surface")["schema"]);
    const SchemaJson sphere = AlternativeOf(surfaces, "sphere");
    ASSERT_EQ(sphere["fields"].size(), 1u);
    EXPECT_EQ(sphere["fields"][0]["name"], "radius");
    EXPECT_EQ(sphere["fields"][0]["unit"], "m");
}

TEST(ConfigCatalog, ListsTheNewDensitiesAndAges) {
    using Json = nlohmann::json;
    const Json catalog = Json::parse(terraingen::ConfigCatalog());
    std::vector<std::string> densities;
    for (const Json& entry : catalog["models"]["density"]) {
        densities.push_back(entry["type"]);
    }
    EXPECT_NE(std::find(densities.begin(), densities.end(), "production_surface"), densities.end());
    EXPECT_NE(std::find(densities.begin(), densities.end(), "minimum"), densities.end());
    EXPECT_EQ(std::find(densities.begin(), densities.end(), "lunar_surface"), densities.end());
    std::vector<std::string> ages;
    for (const Json& entry : catalog["models"]["ages"]) {
        ages.push_back(entry["type"]);
    }
    EXPECT_EQ(ages, (std::vector<std::string>{"uniform", "fresh", "steady_state"}));
}
