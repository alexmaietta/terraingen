// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/io/feature_lists.h"

#include "terraingen/geometry/geometry.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "util/overloaded.h"
#include "util/text.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace terraingen {

namespace {

using Json = nlohmann::ordered_json;

/// One value of a feature list: a CSV cell and a JSON value.
using Cell = std::variant<double, std::optional<double>, std::uint64_t, int, bool>;

/// A column of a feature list: its name and how a row gives its value.
template <typename Row> struct Column {
    const char* name;
    Cell (*get)(const Row&);
};

/// The columns of every crater.
const std::array<Column<Crater>, 7> kCraterColumns{{
    {"x", [](const Crater& c) { return Cell(c.x); }},
    {"y", [](const Crater& c) { return Cell(c.y); }},
    {"diameter", [](const Crater& c) { return Cell(c.diameter); }},
    {"age_gyr", [](const Crater& c) { return Cell(c.ageGyr); }},
    {"id", [](const Crater& c) { return Cell(c.id); }},
    {"manual", [](const Crater& c) { return Cell(c.manual); }},
    {"replace", [](const Crater& c) { return Cell(c.replace); }},
}};

/// The columns added for craters with attributes.
const std::array<Column<CraterAttributes>, 5> kAttributeColumns{{
    {"floor_depth", [](const CraterAttributes& a) { return Cell(a.floorDepth); }},
    {"rim_height", [](const CraterAttributes& a) { return Cell(a.rimHeight); }},
    {"depth_ratio", [](const CraterAttributes& a) { return Cell(a.depthRatio); }},
    {"freshness", [](const CraterAttributes& a) { return Cell(a.freshness); }},
    {"complex", [](const CraterAttributes& a) { return Cell(a.complex); }},
}};

/// The columns of every boulder.
const std::array<Column<Boulder>, 16> kBoulderColumns{{
    {"x", [](const Boulder& b) { return Cell(b.x); }},
    {"y", [](const Boulder& b) { return Cell(b.y); }},
    {"ground", [](const Boulder& b) { return Cell(b.ground); }},
    {"diameter", [](const Boulder& b) { return Cell(b.diameter); }},
    {"height", [](const Boulder& b) { return Cell(b.height); }},
    {"burial", [](const Boulder& b) { return Cell(b.burial); }},
    {"yaw_deg", [](const Boulder& b) { return Cell(b.yaw * kRadian); }},
    {"lean_deg", [](const Boulder& b) { return Cell(b.lean * kRadian); }},
    {"id", [](const Boulder& b) { return Cell(b.id); }},
    {"model", [](const Boulder& b) { return Cell(b.model); }},
    {"wear", [](const Boulder& b) { return Cell(b.wear); }},
    {"parent", [](const Boulder& b) { return Cell(b.parent); }},
    {"piece", [](const Boulder& b) { return Cell(static_cast<std::uint64_t>(b.piece)); }},
    {"rest", [](const Boulder& b) { return Cell(static_cast<int>(b.rest)); }},
    {"tilt_deg", [](const Boulder& b) { return Cell(b.tilt * kRadian); }},
    {"tilt_azimuth_deg", [](const Boulder& b) { return Cell(b.tiltAzimuth * kRadian); }},
}};

/// A cell as CSV writes it: doubles exactly, an unknown value empty and booleans as 1 or 0.
std::string CsvText(const Cell& cell) {
    return std::visit(Overloaded{
                          [](double value) { return FormatNumber(value, 17); },
                          [](const std::optional<double>& value) {
                              return value ? FormatNumber(*value, 17) : std::string();
                          },
                          [](std::uint64_t value) { return std::to_string(value); },
                          [](int value) { return std::to_string(value); },
                          [](bool value) { return std::string(value ? "1" : "0"); },
                      },
                      cell);
}

/// A cell as JSON writes it, an unknown value null.
Json JsonValue(const Cell& cell) {
    return std::visit(
        Overloaded{
            [](const std::optional<double>& value) { return value ? Json(*value) : Json(nullptr); },
            [](const auto& value) { return Json(value); },
        },
        cell);
}

/// Writes the names of `columns` as CSV cells, the first after `separator` and the rest after ",".
template <typename Row, std::size_t N>
void WriteCsvHeader(std::ostream& file, const std::array<Column<Row>, N>& columns,
                    const char* separator) {
    for (const Column<Row>& column : columns) {
        file << separator << column.name;
        separator = ",";
    }
}

/// Writes the cells of `row` as CSV, the first after `separator` and the rest after ",".
template <typename Row, std::size_t N>
void WriteCsvCells(std::ostream& file, const std::array<Column<Row>, N>& columns, const Row& row,
                   const char* separator) {
    for (const Column<Row>& column : columns) {
        file << separator << CsvText(column.get(row));
        separator = ",";
    }
}

/// Adds the cells of `row` to a JSON object, keyed by column name in order.
template <typename Row, std::size_t N>
void AddJsonCells(Json& entry, const std::array<Column<Row>, N>& columns, const Row& row) {
    for (const Column<Row>& column : columns) {
        entry[column.name] = JsonValue(column.get(row));
    }
}

void WriteCraterCsv(std::ostream& file, const CraterList& craters,
                    const std::vector<CraterAttributes>& attributes) {
    WriteCsvHeader(file, kCraterColumns, "");
    if (!attributes.empty()) {
        WriteCsvHeader(file, kAttributeColumns, ",");
    }
    file << '\n';
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        WriteCsvCells(file, kCraterColumns, craters.At(n), "");
        if (!attributes.empty()) {
            WriteCsvCells(file, kAttributeColumns, attributes[n], ",");
        }
        file << '\n';
    }
}

Json CraterJson(const CraterList& craters, const std::vector<CraterAttributes>& attributes) {
    Json list = Json::array();
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        Json entry = Json::object();
        AddJsonCells(entry, kCraterColumns, craters.At(n));
        if (!attributes.empty()) {
            AddJsonCells(entry, kAttributeColumns, attributes[n]);
        }
        list.push_back(std::move(entry));
    }
    return Json{{"craters", std::move(list)}};
}

void WriteBoulderCsv(std::ostream& file, const BoulderList& boulders) {
    WriteCsvHeader(file, kBoulderColumns, "");
    file << '\n';
    for (std::size_t i = 0; i < boulders.Size(); ++i) {
        WriteCsvCells(file, kBoulderColumns, boulders.At(i), "");
        file << '\n';
    }
}

Json BoulderJson(const BoulderList& boulders) {
    Json list = Json::array();
    for (std::size_t i = 0; i < boulders.Size(); ++i) {
        Json entry = Json::object();
        AddJsonCells(entry, kBoulderColumns, boulders.At(i));
        list.push_back(std::move(entry));
    }
    return Json{{"boulders", std::move(list)}};
}

/// The cells of one CSV line, split at commas, with spaces and enclosing double quotes trimmed.
std::vector<std::string> CsvCells(const std::string& line) {
    std::vector<std::string> cells;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = std::min(line.find(',', start), line.size());
        std::string cell = line.substr(start, end - start);
        const std::size_t first = cell.find_first_not_of(" \t\r");
        const std::size_t last = cell.find_last_not_of(" \t\r");
        cell = first == std::string::npos ? std::string() : cell.substr(first, last - first + 1);
        if (cell.size() >= 2 && cell.front() == '"' && cell.back() == '"') {
            cell = cell.substr(1, cell.size() - 2);
        }
        cells.push_back(std::move(cell));
        if (end == line.size()) {
            return cells;
        }
        start = end + 1;
    }
}

/// @throws std::invalid_argument "Crater list row <row>: <what>".
[[noreturn]] void ThrowRowError(std::size_t row, const std::string& what) {
    throw std::invalid_argument("Crater list row " + std::to_string(row) + ": " + what);
}

/// The number `text` spells in full, for row `row`'s column `name`.
/// @throws std::invalid_argument for anything else.
double ParseNumber(const std::string& text, std::size_t row, const char* name) {
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size()) {
        ThrowRowError(row, std::string(name) + " '" + text + "' is not a number");
    }
    return value;
}

/// A row of a CSV crater list, its cells looked up by the header's names.
class CsvCraterRow {
  public:
    CsvCraterRow(const std::map<std::string, std::size_t>& columns,
                 const std::vector<std::string>& cells, std::size_t row)
        : columns_(columns), cells_(cells), row_(row) {}

    /// Column `name`'s number; none where the list lacks it or the cell is empty.
    std::optional<double> Number(const char* name) const {
        const std::string* text = Cell(name);
        if (text == nullptr || text->empty()) {
            return std::nullopt;
        }
        return ParseNumber(*text, row_, name);
    }

    /// The ID column, 0 where the list lacks it or the cell is empty.
    std::uint64_t Id() const {
        const std::string* text = Cell("id");
        if (text == nullptr || text->empty()) {
            return 0;
        }
        char* end = nullptr;
        const unsigned long long value = std::strtoull(text->c_str(), &end, 10);
        if (end != text->c_str() + text->size() || text->front() == '-') {
            ThrowRowError(row_, "id '" + *text + "' is not an unsigned integer");
        }
        return static_cast<std::uint64_t>(value);
    }

  private:
    const std::string* Cell(const char* name) const {
        const auto found = columns_.find(name);
        return found == columns_.end() || found->second >= cells_.size() ? nullptr
                                                                         : &cells_[found->second];
    }

    const std::map<std::string, std::size_t>& columns_;
    const std::vector<std::string>& cells_;
    std::size_t row_;
};

/// An entry of a JSON crater list.
class JsonCraterRow {
  public:
    JsonCraterRow(const Json& entry, std::size_t row) : entry_(entry), row_(row) {
        if (!entry_.is_object()) {
            ThrowRowError(row_, "not an object");
        }
    }

    /// Key `name`'s number, booleans as 1 and 0; none where the entry lacks it or holds null.
    std::optional<double> Number(const char* name) const {
        const auto found = entry_.find(name);
        if (found == entry_.end() || found->is_null()) {
            return std::nullopt;
        }
        if (found->is_boolean()) {
            return found->get<bool>() ? 1.0 : 0.0;
        }
        if (!found->is_number()) {
            ThrowRowError(row_, std::string(name) + " is not a number");
        }
        return found->get<double>();
    }

    /// The ID, 0 where the entry lacks it or holds null.
    std::uint64_t Id() const {
        const auto found = entry_.find("id");
        if (found == entry_.end() || found->is_null()) {
            return 0;
        }
        if (!found->is_number_unsigned()) {
            ThrowRowError(row_, "id is not an unsigned integer");
        }
        return found->get<std::uint64_t>();
    }

  private:
    const Json& entry_;
    std::size_t row_;
};

/// The crater `row` describes (`ReadCraterList`).
template <typename Row> Crater ParseCrater(const Row& row, std::size_t index) {
    const auto required = [&](const char* name) {
        const std::optional<double> value = row.Number(name);
        if (!value) {
            ThrowRowError(index, std::string("needs ") + name);
        }
        return *value;
    };
    Crater crater;
    crater.x = required("x");
    crater.y = required("y");
    crater.diameter = required("diameter");
    crater.ageGyr = row.Number("age_gyr").value_or(0.0);
    crater.id = row.Id();
    crater.manual = row.Number("manual").value_or(0.0) != 0.0;
    crater.replace = row.Number("replace").value_or(0.0) != 0.0;
    crater.depth = row.Number("depth_m");
    if (!crater.depth) {
        if (const std::optional<double> ratio = row.Number("depth_ratio")) {
            crater.depth = *ratio * crater.diameter;
        }
    }
    if (crater.depth) {
        crater.rimHeight = row.Number("rim_height_m");
        if (!crater.rimHeight) {
            crater.rimHeight = row.Number("rim_height");
        }
    }
    return crater;
}

CraterList ReadCraterCsv(const std::string& text) {
    std::istringstream file(text);
    std::string line;
    std::map<std::string, std::size_t> columns;
    if (std::getline(file, line)) {
        const std::vector<std::string> names = CsvCells(line);
        for (std::size_t n = 0; n < names.size(); ++n) {
            columns.emplace(Lower(names[n]), n);
        }
    }
    CraterList craters;
    std::size_t row = 0;
    while (std::getline(file, line)) {
        if (line.find_first_not_of(" \t\r") == std::string::npos) {
            continue;
        }
        const std::vector<std::string> cells = CsvCells(line);
        craters.Append(ParseCrater(CsvCraterRow(columns, cells, row), row));
        ++row;
    }
    return craters;
}

CraterList ReadCraterJson(const std::string& text) {
    const Json json = Json::parse(text, nullptr, false);
    if (json.is_discarded()) {
        throw std::invalid_argument("Crater list is not valid JSON");
    }
    const Json* list = &json;
    if (json.is_object()) {
        const auto found = json.find("craters");
        if (found == json.end()) {
            throw std::invalid_argument("Crater list JSON needs a \"craters\" array");
        }
        list = &*found;
    }
    if (!list->is_array()) {
        throw std::invalid_argument("Crater list JSON needs a \"craters\" array");
    }
    CraterList craters;
    craters.Reserve(list->size());
    for (std::size_t row = 0; row < list->size(); ++row) {
        craters.Append(ParseCrater(JsonCraterRow((*list)[row], row), row));
    }
    return craters;
}

}  // namespace

CraterList ReadCraterList(const std::filesystem::path& path) {
    const std::string text = ReadFile(path);
    try {
        return HasExtension(path, {".json"}) ? ReadCraterJson(text) : ReadCraterCsv(text);
    } catch (const std::invalid_argument& error) {
        throw std::invalid_argument(path.string() + ": " + error.what());
    }
}

void WriteCraterList(const CraterList& craters, const std::filesystem::path& path,
                     ListFormat format, const std::vector<CraterAttributes>& attributes) {
    if (!attributes.empty() && attributes.size() != craters.Size()) {
        throw std::invalid_argument("Crater attributes must be empty or one per crater");
    }
    std::ostringstream text;
    switch (format) {
    case ListFormat::Csv:
        WriteCraterCsv(text, craters, attributes);
        break;
    case ListFormat::Json:
        text << CraterJson(craters, attributes).dump(2) << '\n';
        break;
    }
    WriteFile(path, text.str());
}

void WriteBoulderList(const BoulderList& boulders, const std::filesystem::path& path,
                      ListFormat format) {
    std::ostringstream text;
    switch (format) {
    case ListFormat::Csv:
        WriteBoulderCsv(text, boulders);
        break;
    case ListFormat::Json:
        text << BoulderJson(boulders).dump(2) << '\n';
        break;
    }
    WriteFile(path, text.str());
}

void WriteBoulderModels(const std::vector<BoulderModel>& boulders,
                        const std::filesystem::path& path, double creaseAngle,
                        const Surface& surface, const Vec2d& site, bool instanced) {
    if (!(creaseAngle >= 0.0 && creaseAngle <= kMaxCreaseAngle)) {
        throw std::invalid_argument("Crease angle must be between 0 and 180 degrees");
    }
    if (boulders.empty()) {
        throw std::invalid_argument("There are no boulder models to write");
    }
    const double radians = creaseAngle * kDegree;
    std::vector<SceneMesh> meshes;
    std::map<const BoulderShape*, std::size_t> indices;
    std::vector<SceneNode> nodes;
    for (const BoulderModel& boulder : boulders) {
        const BoulderPlacement placement = PlacementAtSite(
            boulder.placement, {boulder.boulder.x, boulder.boulder.y}, site, surface);
        if (!instanced) {
            TriangleMesh placed = boulder.shape->mesh;
            for (Vec3d& p : placed.positions) {
                p = placement.Apply(p);
            }
            ShadedMesh shaded = WithCreases(placed, radians);
            nodes.push_back({meshes.size(), {1.0, 1.0, 1.0}, {}});
            meshes.push_back(
                {std::move(shaded.mesh), std::move(shaded.normals), boulder.shape->material});
            continue;
        }
        auto [found, added] = indices.emplace(boulder.shape.get(), meshes.size());
        if (added) {
            ShadedMesh shaded = WithCreases(boulder.shape->mesh, radians);
            meshes.push_back(
                {std::move(shaded.mesh), std::move(shaded.normals), boulder.shape->material});
        }
        nodes.push_back({found->second, placement.scale, placement.pose});
    }
    WriteScene(path, meshes, nodes);
}

}  // namespace terraingen
