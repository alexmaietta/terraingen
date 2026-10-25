// SPDX-License-Identifier: BSD-3-Clause

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
        if (value.is_string() && resolved.value("named", false)) {
            const SchemaJson& alternatives = resolved["alternatives"];
            EXPECT_TRUE(std::any_of(
                alternatives.begin(), alternatives.end(),
                [&](const SchemaJson& alternative) { return alternative["type"] == value; }))
                << where << " names " << value;
            return;
        }
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
    EXPECT_EQ(names, (std::vector<std::string>{"schema_version", "body", "source", "layers",
                                               "outputs", "albedo", "materials"}));

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
    const std::set<std::string> units = {"",       "m",   "m^2",     "km",    "deg",
                                         "rad",    "Gyr", "Myr",     "1/m",   "1/m^2",
                                         "1/km^2", "px",  "m^2/Myr", "m/unit"};
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
            EXPECT_TRUE(units.count(unit) != 0) << at << " has unit \"" << unit << "\"";
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

TEST(ConfigSchema, BoundsOnlyNumbersAndTheirDefaultsFitTheBounds) {
    const SchemaJson schema = SchemaJson::parse(terraingen::ConfigSchema());
    const std::set<std::string> placeholders = {"radius_m", "diameter_m"};
    int bounded = 0;
    const auto check = [&](const SchemaJson& fields, const std::string& where) {
        for (const SchemaJson& field : fields) {
            const bool low = field.contains("minimum");
            const bool high = field.contains("maximum");
            if (!low && !high) {
                continue;
            }
            ++bounded;
            const std::string name = field["name"];
            const std::string at = where + "." + name;
            const SchemaJson* inner = &field["schema"];
            while ((*inner)["kind"] == "optional" || (*inner)["kind"] == "array") {
                inner = &(*inner)["of"];
            }
            EXPECT_TRUE((*inner)["kind"] == "number" || (*inner)["kind"] == "integer") << at;
            EXPECT_EQ(low, field.contains("exclusive_minimum")) << at;
            EXPECT_EQ(high, field.contains("exclusive_maximum")) << at;
            if (low && high) {
                EXPECT_LT(field["minimum"].get<double>(), field["maximum"].get<double>()) << at;
            }
            const SchemaJson& value = field["default"];
            const SchemaJson values = value.is_array() ? value : SchemaJson::array({value});
            for (const SchemaJson& v : values) {
                if (!v.is_number() || placeholders.count(name) != 0) {
                    continue;
                }
                const double x = v.get<double>();
                if (low) {
                    const double min = field["minimum"];
                    EXPECT_TRUE(field["exclusive_minimum"] ? x > min : x >= min)
                        << at << " = " << x;
                }
                if (high) {
                    const double max = field["maximum"];
                    EXPECT_TRUE(field["exclusive_maximum"] ? x < max : x <= max)
                        << at << " = " << x;
                }
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
    EXPECT_GT(bounded, 300) << "shares, counts and sizes carry their ranges";
}

TEST(ConfigSchema, ListsTheParsersChoicesAndRefersToRecursiveTypes) {
    const SchemaJson schema = SchemaJson::parse(terraingen::ConfigSchema());
    const SchemaJson base = FieldOf(schema["root"], "source")["schema"];
    ASSERT_EQ(base["kind"], "optional");
    const SchemaJson source = base["of"];
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
    EXPECT_EQ(basis["choices"].size(), 7u);
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
              "GeoTIFF file to write; unset to only return the result, writing nothing; relative "
              "paths resolve against the output folder (the CLI's --output-dir), else the "
              "configuration's folder.");
    EXPECT_EQ(FieldOf(AlternativeOf(choice, "albedo"), "extent_m"),
              FieldOf(AlternativeOf(choice, "heightmap"), "extent_m"));
    EXPECT_EQ(FieldOf(AlternativeOf(choice, "crater_list"), "pixel_size_m"),
              FieldOf(AlternativeOf(choice, "boulder_models"), "pixel_size_m"));
    EXPECT_EQ(FieldOf(AlternativeOf(choice, "report"), "path")["schema"]["kind"], "string");
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
    EXPECT_EQ(keys, (std::vector<std::string>{"schema_version", "body", "source", "layers",
                                              "outputs", "albedo", "materials", "models"}));
    const auto types = [](const Json& list) {
        std::vector<std::string> names;
        for (const Json& entry : list) {
            names.push_back(entry["type"].get<std::string>());
        }
        return names;
    };
    EXPECT_EQ(types(catalog["body"]),
              (std::vector<std::string>{"plane", "sphere", "ellipsoid", "shape_model"}));
    EXPECT_EQ(catalog["layers"].size(), std::variant_size_v<LayerSpec>);
    EXPECT_EQ(catalog["outputs"].size(), std::variant_size_v<terraingen::OutputSpec>);
    EXPECT_EQ(catalog["models"]["density"].size(), std::variant_size_v<DensitySpec>);
    EXPECT_EQ(catalog["models"]["ages"].size(), std::variant_size_v<AgesSpec>);

    const auto parses = [](const Json& config, const std::string& what) {
        Json document = config;
        document["schema_version"] = terraingen::kConfigSchemaVersion;
        EXPECT_NO_THROW(ParseConfig(document.dump())) << what << ": " << document.dump();
    };
    for (const Json& body : catalog["body"]) {
        parses({{"body", body}}, "body");
    }
    EXPECT_EQ(types(catalog["source"]),
              (std::vector<std::string>{"raster", "chain", "synthetic", "shape_model"}));
    for (const Json& source : catalog["source"]) {
        parses({{"source", source}}, "source");
    }
    for (const char* list : {"layers", "outputs"}) {
        for (const Json& entry : catalog[list]) {
            parses({{list, Json::array({entry})}}, list);
        }
    }
    parses({{"albedo", catalog["albedo"]}}, "albedo");
    parses({{"materials", catalog["materials"]}}, "materials");

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
        Json masked = layerOf("masked");
        masked["shape"] = entry;
        parses({{"layers", Json::array({masked})}}, "region_shape");
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

    const SchemaJson unsetAges = FieldOf(craters, "ages")["schema"];
    ASSERT_EQ(unsetAges["kind"], "optional");
    const SchemaJson ages = Resolve(schema, unsetAges["of"]);
    std::vector<std::string> types;
    for (const SchemaJson& alternative : ages["alternatives"]) {
        types.push_back(alternative["type"]);
    }
    EXPECT_EQ(types, (std::vector<std::string>{"uniform", "fresh", "steady_state", "freshness"}));
    EXPECT_TRUE(FieldOf(craters, "ages")["default"].is_null()) << "unset ages follow the density";
    EXPECT_EQ(FieldOf(craters, "density")["default"]["type"], "minimum");
    const SchemaJson steady = AlternativeOf(ages, "steady_state");
    EXPECT_EQ(FieldOf(steady, "production")["schema"]["choices"],
              (SchemaJson{"neukum", "extended_neukum", "ivanov"}));
    EXPECT_EQ(FieldOf(steady, "equilibrium")["schema"]["choices"],
              (SchemaJson{"trask", "hartmann"}));
    EXPECT_EQ(FieldOf(steady, "chronology")["schema"]["choices"], (SchemaJson{"neukum", "ivanov"}));
    const SchemaJson freshness = AlternativeOf(ages, "freshness");
    EXPECT_EQ(FieldOf(freshness, "lifetime_gyr")["unit"], "Gyr");
    EXPECT_EQ(FieldOf(freshness, "lifetime_gyr")["default"], 1.0);
    const SchemaJson drawn = FieldOf(freshness, "freshness");
    EXPECT_EQ(drawn["default"]["kind"], "uniform");
    EXPECT_EQ(drawn["default"]["edges"], SchemaJson::array());
    const SchemaJson& distribution = Resolve(schema, drawn["schema"]);
    EXPECT_EQ(FieldOf(distribution, "kind")["schema"]["choices"],
              (SchemaJson{"constant", "uniform", "normal", "log_normal", "piecewise"}));
    EXPECT_EQ(FieldOf(distribution, "edges")["schema"]["kind"], "array");
    EXPECT_EQ(FieldOf(distribution, "weights")["schema"]["kind"], "array");

    const SchemaJson morphometry = FieldOf(craters, "morphometry");
    EXPECT_EQ(morphometry["schema"]["choices"], (SchemaJson{"lunar", "mars"}));
    EXPECT_EQ(morphometry["default"], "lunar");
    EXPECT_EQ(FieldOf(AlternativeOf(layerChoice, "secondaries"), "morphometry")["schema"],
              morphometry["schema"]);
}

TEST(ConfigSchema, GivesASphereOnlyARadiusInMeters) {
    const SchemaJson schema = SchemaJson::parse(terraingen::ConfigSchema());
    const SchemaJson surfaces = Resolve(schema, FieldOf(schema["root"], "body")["schema"]);
    const SchemaJson sphere = AlternativeOf(surfaces, "sphere");
    ASSERT_EQ(sphere["fields"].size(), 1u);
    EXPECT_EQ(sphere["fields"][0]["name"], "radius_m");
    EXPECT_EQ(sphere["fields"][0]["unit"], "m");
}

TEST(ConfigSchema, GivesGeoTiffReadsACropAndLayersASourceResolvedLimit) {
    const SchemaJson schema = SchemaJson::parse(terraingen::ConfigSchema());
    const SchemaJson base = FieldOf(schema["root"], "source")["schema"];
    ASSERT_EQ(base["kind"], "optional");
    const SchemaJson raster = AlternativeOf(Resolve(schema, base["of"]), "raster");
    const SchemaJson region = FieldOf(raster, "crop");
    EXPECT_TRUE(region["default"].is_null());
    ASSERT_EQ(region["schema"]["kind"], "optional");
    const SchemaJson box = Resolve(schema, region["schema"]["of"]);
    std::vector<std::string> names;
    for (const SchemaJson& field : box["fields"]) {
        names.push_back(field["name"]);
        EXPECT_EQ(field["schema"]["kind"], "number");
    }
    EXPECT_EQ(names, (std::vector<std::string>{"min_x", "min_y", "max_x", "max_y"}));
    const SchemaJson layers = FieldOf(schema["root"], "layers")["schema"];
    const SchemaJson choice = Resolve(schema, layers["of"]);
    const SchemaJson upsampling = Resolve(schema, FieldOf(raster, "upsampling")["schema"]);
    EXPECT_EQ(FieldOf(raster, "upsampling")["default"], "cubic");
    ASSERT_EQ(upsampling["kind"], "choice");
    EXPECT_EQ(upsampling["named"], true);
    std::vector<std::string> upsamplers;
    for (const SchemaJson& alternative : upsampling["alternatives"]) {
        upsamplers.push_back(alternative["type"]);
    }
    EXPECT_EQ(upsamplers,
              (std::vector<std::string>{"nearest", "bilinear", "cubic", "catmull_rom", "lanczos3",
                                        "cubic_bspline", "diamond_square"}));
    std::vector<std::string> fractal;
    const SchemaJson diamondSquare = AlternativeOf(upsampling, "diamond_square");
    for (const SchemaJson& field : diamondSquare["fields"]) {
        fractal.push_back(field["name"]);
    }
    EXPECT_EQ(fractal, (std::vector<std::string>{"fractal_dimension", "scale_m", "fit_window_px",
                                                 "interp", "min_pixel_size_m", "seed"}));
    const SchemaJson synthetic = AlternativeOf(Resolve(schema, base["of"]), "synthetic");
    EXPECT_EQ(FieldOf(synthetic, "upsampling")["schema"]["of"],
              FieldOf(raster, "upsampling")["schema"]);
    EXPECT_EQ(FieldOf(synthetic, "stack")["schema"],
              FieldOf(AlternativeOf(choice, "noise"), "stack")["schema"])
        << "the synthetic source shares the noise layer's stack";
    const SchemaJson limit = FieldOf(AlternativeOf(choice, "craters"), "source_resolves_px");
    EXPECT_EQ(limit["default"], 4.0);
    EXPECT_EQ(limit["unit"], "px");
    EXPECT_EQ(limit["minimum"], 0.0);
    EXPECT_EQ(limit["exclusive_minimum"], true);
    for (const char* layer : {"secondaries", "dunes", "bedforms"}) {
        EXPECT_EQ(FieldOf(AlternativeOf(choice, layer), "source_resolves_px")["default"], 4.0)
            << layer;
    }
    for (const char* layer : {"boulders", "noise"}) {
        EXPECT_TRUE(
            FieldOf(AlternativeOf(choice, layer), "source_resolves_px")["default"].is_null())
            << layer;
    }
    const SchemaJson fade = FieldOf(AlternativeOf(choice, "boulders"), "fade_px");
    EXPECT_EQ(fade["schema"]["kind"], "array");
    EXPECT_EQ(fade["schema"]["length"], 2);
    EXPECT_EQ(fade["default"], SchemaJson::array({2.0, 4.0}));
    EXPECT_EQ(fade["unit"], "px");
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
    EXPECT_EQ(ages, (std::vector<std::string>{"uniform", "fresh", "steady_state", "freshness"}));
}

TEST(LayerType, NamesALayerAsItsJsonTypeAndAMaskedLayerByTheLayerInside) {
    EXPECT_EQ(terraingen::LayerType(terraingen::CraterLayerSpec{}), "craters");
    EXPECT_EQ(terraingen::LayerType(terraingen::SandFillLayerSpec{}), "sand_fill");
    EXPECT_EQ(terraingen::LayerType(terraingen::SpectralContinuationLayerSpec{}),
              "spectral_continuation");
    terraingen::MaskedLayerSpec masked;
    masked.layer = terraingen::BoulderLayerSpec{};
    EXPECT_EQ(terraingen::LayerType(masked), "masked (boulders)");
    terraingen::MaskedLayerSpec nested;
    nested.layer = masked;
    EXPECT_EQ(terraingen::LayerType(nested), "masked (masked (boulders))");
}
