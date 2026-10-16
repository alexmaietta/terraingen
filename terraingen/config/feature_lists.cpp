#include "terraingen/config/build.h"
#include "terraingen/config/generate.h"
#include "terraingen/mesh/io/mesh_io.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdio>
#include <fstream>
#include <map>
#include <numbers>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

using Json = nlohmann::ordered_json;

/// `value` with the digits to read it back exactly.
std::string Number(double value) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.17g", value);
    return buffer.data();
}

/// @throws std::runtime_error if `path` cannot be opened.
std::ofstream OpenForWriting(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot write " + path.string());
    }
    return file;
}

/// @throws std::runtime_error if writing to `file` failed.
void CheckWritten(const std::ofstream& file, const std::filesystem::path& path) {
    if (!file) {
        throw std::runtime_error("Cannot write " + path.string());
    }
}

void WriteCraterCsv(std::ostream& file, const CraterList& craters,
                    const std::vector<CraterAttributes>& attributes) {
    const bool detailed = !attributes.empty();
    file << "x,y,diameter,age_gyr,id,manual"
         << (detailed ? ",floor_depth,rim_height,depth_ratio,freshness,complex" : "") << '\n';
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        const Crater crater = craters.At(n);
        file << Number(crater.x) << ',' << Number(crater.y) << ',' << Number(crater.diameter) << ','
             << Number(crater.ageGyr) << ',' << crater.id << ',' << (crater.manual ? 1 : 0);
        if (detailed) {
            const CraterAttributes& a = attributes[n];
            file << ',' << Number(a.floorDepth) << ',' << Number(a.rimHeight) << ','
                 << Number(a.depthRatio) << ',' << Number(a.freshness) << ','
                 << (a.complex ? 1 : 0);
        }
        file << '\n';
    }
}

Json CraterJson(const CraterList& craters, const std::vector<CraterAttributes>& attributes) {
    Json list = Json::array();
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        const Crater crater = craters.At(n);
        Json entry{{"x", crater.x},
                   {"y", crater.y},
                   {"diameter", crater.diameter},
                   {"age_gyr", crater.ageGyr},
                   {"id", crater.id},
                   {"manual", crater.manual}};
        if (!attributes.empty()) {
            const CraterAttributes& a = attributes[n];
            entry["floor_depth"] = a.floorDepth;
            entry["rim_height"] = a.rimHeight;
            entry["depth_ratio"] = a.depthRatio;
            entry["freshness"] = a.freshness;
            entry["complex"] = a.complex;
        }
        list.push_back(std::move(entry));
    }
    return Json{{"craters", std::move(list)}};
}

void WriteBoulderCsv(std::ostream& file, const std::vector<Boulder>& boulders) {
    file << "x,y,ground,diameter,height,burial,yaw,lean,id,model\n";
    for (const Boulder& boulder : boulders) {
        file << Number(boulder.x) << ',' << Number(boulder.y) << ','
             << (boulder.ground ? Number(*boulder.ground) : std::string()) << ','
             << Number(boulder.diameter) << ',' << Number(boulder.height) << ','
             << Number(boulder.burial) << ',' << Number(boulder.yaw) << ',' << Number(boulder.lean)
             << ',' << boulder.id << ',' << (boulder.model ? 1 : 0) << '\n';
    }
}

Json BoulderJson(const std::vector<Boulder>& boulders) {
    Json list = Json::array();
    for (const Boulder& boulder : boulders) {
        list.push_back({{"x", boulder.x},
                        {"y", boulder.y},
                        {"ground", boulder.ground ? Json(*boulder.ground) : Json(nullptr)},
                        {"diameter", boulder.diameter},
                        {"height", boulder.height},
                        {"burial", boulder.burial},
                        {"yaw", boulder.yaw},
                        {"lean", boulder.lean},
                        {"id", boulder.id},
                        {"model", boulder.model}});
    }
    return Json{{"boulders", std::move(list)}};
}

BoulderPlacement PlacementOf(const BoulderModel& boulder, const Surface& surface,
                             const Vec2d& site) {
    return PlacementAtSite(boulder.placement, {boulder.boulder.x, boulder.boulder.y}, site,
                           surface);
}

/// Writes each shared shape once, with crease normals, and a node per boulder.
void WriteBoulderScene(const std::vector<BoulderModel>& boulders, const std::filesystem::path& path,
                       double creaseAngle, const Surface& surface, const Vec2d& site) {
    const double radians = creaseAngle * std::numbers::pi / 180.0;
    std::vector<SceneMesh> meshes;
    std::map<const BoulderShape*, std::size_t> indices;
    std::vector<SceneNode> nodes;
    for (const BoulderModel& boulder : boulders) {
        auto [found, added] = indices.emplace(boulder.shape.get(), meshes.size());
        if (added) {
            ShadedMesh shaded = WithCreases(boulder.shape->mesh, radians);
            meshes.push_back(
                {std::move(shaded.mesh), std::move(shaded.normals), boulder.shape->material});
        }
        const BoulderPlacement placement = PlacementOf(boulder, surface, site);
        nodes.push_back({found->second, placement.scale, placement.pose});
    }
    WriteScene(path, meshes, nodes);
}

}  // namespace

void WriteCraterList(const CraterList& craters, const std::filesystem::path& path,
                     ListFormat format, const std::vector<CraterAttributes>& attributes) {
    if (!attributes.empty() && attributes.size() != craters.Size()) {
        throw std::invalid_argument("Crater attributes must be empty or one per crater");
    }
    std::ofstream file = OpenForWriting(path);
    switch (format) {
    case ListFormat::Csv:
        WriteCraterCsv(file, craters, attributes);
        break;
    case ListFormat::Json:
        file << CraterJson(craters, attributes).dump(2) << '\n';
        break;
    }
    CheckWritten(file, path);
}

void WriteBoulderList(const std::vector<Boulder>& boulders, const std::filesystem::path& path,
                      ListFormat format) {
    std::ofstream file = OpenForWriting(path);
    switch (format) {
    case ListFormat::Csv:
        WriteBoulderCsv(file, boulders);
        break;
    case ListFormat::Json:
        file << BoulderJson(boulders).dump(2) << '\n';
        break;
    }
    CheckWritten(file, path);
}

void WriteBoulderModels(const std::vector<BoulderModel>& boulders,
                        const std::filesystem::path& path, double creaseAngle,
                        const Surface& surface, const Vec2d& site) {
    if (!(creaseAngle >= 0.0 && creaseAngle <= 180.0)) {
        throw std::invalid_argument("Crease angle must be between 0 and 180 degrees");
    }
    if (boulders.empty()) {
        throw std::invalid_argument("There are no boulder models to write");
    }
    WriteBoulderScene(boulders, path, creaseAngle, surface, site);
}

}  // namespace terraingen
