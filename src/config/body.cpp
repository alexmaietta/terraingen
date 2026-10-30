// SPDX-License-Identifier: BSD-3-Clause

#include "config/body.h"

#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "terraingen/mesh/shape_model.h"
#include "util/overloaded.h"

#include <map>
#include <memory>
#include <mutex>
#include <system_error>
#include <tuple>

namespace terraingen {

namespace {

using config::At;
using config::AtFile;
using config::Resolve;

/// The radial shape a shape model spec describes, loaded once per file, modification time, Q and
/// unit, so surfaces built from one spec share it and compare equal.
/// @throws ConfigError at "body.path" for a file that cannot be read or resampled.
std::shared_ptr<const RadialShape> LoadShapeModel(const ShapeModelSurfaceSpec& spec,
                                                  const std::filesystem::path& directory) {
    config::CheckNonEmpty(spec.path, "body.path");
    const std::filesystem::path file = Resolve(spec.path, directory);
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(file, error);
    if (error) {
        throw ConfigError("body.path", "cannot read " + file.string());
    }
    using Key = std::tuple<std::string, std::filesystem::file_time_type, std::size_t, double>;
    static std::mutex mutex;
    static std::map<Key, std::shared_ptr<const RadialShape>> loaded;
    const Key key{std::filesystem::absolute(file).string(), modified, spec.q.value_or(0),
                  spec.metersPerUnit.value_or(0.0)};
    const std::lock_guard<std::mutex> lock(mutex);
    if (const auto found = loaded.find(key); found != loaded.end()) {
        return found->second;
    }
    const auto shape = AtFile("body.path", [&] {
        if (IsIcqPath(file)) {
            const IcqModel model = ReadIcq(file, spec.metersPerUnit.value_or(1000.0));
            return spec.q && *spec.q != model.q ? ShapeModelFromMesh(IcqToMesh(model), *spec.q)
                                                : ToRadialShape(model);
        }
        const double scale = spec.metersPerUnit.value_or(1.0);
        const TriangleMesh mesh = Scaled(ReadMesh(file).mesh, {scale, scale, scale});
        return ShapeModelFromMesh(mesh, spec.q.value_or(256));
    });
    return loaded.emplace(key, std::make_shared<const RadialShape>(shape)).first->second;
}

}  // namespace

Surface BuildSurface(const SurfaceSpec& spec, const std::filesystem::path& directory) {
    config::CheckConfigRanges(spec, "body");
    return std::visit(Overloaded{
                          [](const PlaneSurfaceSpec&) { return Surface::Plane(); },
                          [](const SphereSurfaceSpec& s) {
                              return At("body.radius_m", [&] { return Surface::Sphere(s.radius); });
                          },
                          [](const EllipsoidSurfaceSpec& s) {
                              return At("body.radii_m", [&] {
                                  return Surface::Ellipsoid(s.radii[0], s.radii[1], s.radii[2]);
                              });
                          },
                          [&](const ShapeModelSurfaceSpec& s) {
                              return Surface::ShapeModel(LoadShapeModel(s, directory));
                          },
                      },
                      spec);
}

}  // namespace terraingen
