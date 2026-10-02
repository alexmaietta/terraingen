#include "ernie/scene/scene.h"

#include "ernie/scene/tokens.h"

#include <bit>
#include <set>
#include <stdexcept>
#include <utility>

namespace ernie {

namespace {

std::size_t StampIndex(Dirty bit) {
    return static_cast<std::size_t>(std::countr_zero(static_cast<std::uint32_t>(bit)));
}

Dirty StampBit(std::size_t index) {
    return static_cast<Dirty>(1u << index);
}

std::invalid_argument NoPrim(const Path& path) {
    return std::invalid_argument("No prim at '" + path.String() + "'");
}

}  // namespace

Dirty DirtyOf(std::string_view attribute) {
    using namespace tokens;
    if (attribute == kTransform) {
        return Dirty::Transform;
    }
    if (attribute == kVisibility) {
        return Dirty::Visibility;
    }
    if (attribute == kMaterialBinding) {
        return Dirty::Material;
    }
    if (attribute == kFaceVertexCounts || attribute == kFaceVertexIndices ||
        attribute == kProtoIndices || attribute == kPrototypes) {
        return Dirty::Topology;
    }
    if (attribute == kPoints || attribute == kNormals || attribute == kHeights ||
        attribute == kPositions || attribute == kOrientations || attribute == kScales ||
        attribute == kVelocities) {
        return Dirty::Points;
    }
    if (attribute.starts_with("primvars:")) {
        return Dirty::Primvars;
    }
    return Dirty::Params;
}

bool Prim::IsValid() const {
    return scene_ != nullptr && scene_->Has(path_);
}

const std::string& Prim::Type() const {
    if (scene_ == nullptr) {
        throw NoPrim(path_);
    }
    return scene_->TypeOf(path_);
}

Prim& Prim::Set(std::string_view attribute, Value value) {
    if (scene_ == nullptr) {
        throw NoPrim(path_);
    }
    scene_->Set(path_, attribute, std::move(value));
    return *this;
}

Prim& Prim::Write(std::string_view attribute, Tensor tensor) {
    if (scene_ == nullptr) {
        throw NoPrim(path_);
    }
    scene_->Write(path_, attribute, std::move(tensor));
    return *this;
}

const Value* Prim::Get(std::string_view attribute) const {
    return scene_ != nullptr ? scene_->Get(path_, attribute) : nullptr;
}

const Tensor* Prim::Read(std::string_view attribute) const {
    return scene_ != nullptr ? scene_->Read(path_, attribute) : nullptr;
}

Scene::Scene() {
    prims_.emplace(Path::Root(), PrimData{});
}

Scene::PrimData& Scene::Find(const Path& path) {
    const auto it = prims_.find(path);
    if (it == prims_.end()) {
        throw NoPrim(path);
    }
    return it->second;
}

const Scene::PrimData& Scene::Find(const Path& path) const {
    const auto it = prims_.find(path);
    if (it == prims_.end()) {
        throw NoPrim(path);
    }
    return it->second;
}

void Scene::Stamp(const Path& path, PrimData& prim, Dirty bit) {
    const std::size_t index = StampIndex(bit);
    prim.stamps[index] = version_;
    if (bit == Dirty::Transform || bit == Dirty::Visibility) {
        for (auto it = prims_.upper_bound(path); it != prims_.end() && it->first.HasPrefix(path);
             ++it) {
            it->second.stamps[index] = version_;
        }
    }
}

void Scene::Stamp(const Path& path, PrimData& prim, std::string_view attribute) {
    if (const auto it = prim.versions.find(attribute); it != prim.versions.end()) {
        it->second = version_;
    } else {
        prim.versions.emplace(std::string(attribute), version_);
    }
    Stamp(path, prim, DirtyOf(attribute));
}

Scene::PrimData& Scene::CreateIfMissing(const Path& path) {
    const auto it = prims_.find(path);
    if (it != prims_.end()) {
        return it->second;
    }
    CreateIfMissing(path.Parent());
    PrimData& prim = prims_[path];
    Stamp(path, prim, Dirty::Added);
    return prim;
}

Prim Scene::Define(const Path& path, std::string_view type) {
    if (path.IsEmpty() || path.IsRoot()) {
        throw std::invalid_argument("Cannot define the prim at '" + path.String() + "'");
    }
    ++version_;
    const bool existed = Has(path);
    PrimData& prim = CreateIfMissing(path);
    if (prim.type != type) {
        if (existed) {
            prim.values.clear();
            prim.tensors.clear();
            prim.versions.clear();
            removed_[path] = version_;
            Stamp(path, prim, Dirty::Added);
        }
        prim.type = type;
    }
    return Prim(this, path);
}

bool Scene::Remove(const Path& path) {
    if (path.IsRoot()) {
        throw std::invalid_argument("Cannot remove the root prim");
    }
    auto first = prims_.find(path);
    if (first == prims_.end()) {
        return false;
    }
    ++version_;
    auto last = first;
    while (last != prims_.end() && last->first.HasPrefix(path)) {
        removed_[last->first] = version_;
        ++last;
    }
    prims_.erase(first, last);
    return true;
}

bool Scene::Has(const Path& path) const {
    return prims_.contains(path);
}

Prim Scene::GetPrim(const Path& path) {
    return Has(path) ? Prim(this, path) : Prim();
}

const std::string& Scene::TypeOf(const Path& path) const {
    return Find(path).type;
}

std::vector<Path> Scene::Children(const Path& path) const {
    std::vector<Path> children;
    for (auto it = prims_.upper_bound(path); it != prims_.end() && it->first.HasPrefix(path);
         ++it) {
        if (it->first.Parent() == path) {
            children.push_back(it->first);
        }
    }
    return children;
}

std::vector<Path> Scene::Prims(std::string_view type) const {
    std::vector<Path> paths;
    for (const auto& [path, prim] : prims_) {
        if (!path.IsRoot() && (type.empty() || prim.type == type)) {
            paths.push_back(path);
        }
    }
    return paths;
}

void Scene::Set(const Path& path, std::string_view attribute, Value value) {
    PrimData& prim = Find(path);
    ++version_;
    if (const auto tensor = prim.tensors.find(attribute); tensor != prim.tensors.end()) {
        prim.tensors.erase(tensor);
    }
    if (const auto it = prim.values.find(attribute); it != prim.values.end()) {
        it->second = std::move(value);
    } else {
        prim.values.emplace(std::string(attribute), std::move(value));
    }
    Stamp(path, prim, attribute);
}

const Value* Scene::Get(const Path& path, std::string_view attribute) const {
    const auto prim = prims_.find(path);
    if (prim == prims_.end()) {
        return nullptr;
    }
    const auto it = prim->second.values.find(attribute);
    return it != prim->second.values.end() ? &it->second : nullptr;
}

void Scene::Write(const Path& path, std::string_view attribute, Tensor tensor) {
    PrimData& prim = Find(path);
    ++version_;
    if (const auto value = prim.values.find(attribute); value != prim.values.end()) {
        prim.values.erase(value);
    }
    if (const auto it = prim.tensors.find(attribute); it != prim.tensors.end()) {
        it->second = std::move(tensor);
    } else {
        prim.tensors.emplace(std::string(attribute), std::move(tensor));
    }
    Stamp(path, prim, attribute);
}

void Scene::Write(std::span<const Path> paths, std::string_view attribute, const Tensor& rows) {
    if (rows.Rank() == 0 || rows.Shape()[0] != static_cast<std::int64_t>(paths.size())) {
        throw std::invalid_argument("Scene::Write needs one tensor row per path");
    }
    for (const Path& path : paths) {
        Find(path);
    }
    for (std::size_t i = 0; i < paths.size(); ++i) {
        Write(paths[i], attribute, rows.Row(static_cast<std::int64_t>(i)));
    }
}

const Tensor* Scene::Read(const Path& path, std::string_view attribute) const {
    const auto prim = prims_.find(path);
    if (prim == prims_.end()) {
        return nullptr;
    }
    const auto it = prim->second.tensors.find(attribute);
    return it != prim->second.tensors.end() ? &it->second : nullptr;
}

std::vector<std::string> Scene::Attributes(const Path& path) const {
    const PrimData& prim = Find(path);
    std::set<std::string> names;
    for (const auto& [name, value] : prim.values) {
        names.insert(name);
    }
    for (const auto& [name, tensor] : prim.tensors) {
        names.insert(name);
    }
    return {names.begin(), names.end()};
}

bool Scene::Clear(const Path& path, std::string_view attribute) {
    PrimData& prim = Find(path);
    const auto value = prim.values.find(attribute);
    const auto tensor = prim.tensors.find(attribute);
    if (value == prim.values.end() && tensor == prim.tensors.end()) {
        return false;
    }
    ++version_;
    if (value != prim.values.end()) {
        prim.values.erase(value);
    }
    if (tensor != prim.tensors.end()) {
        prim.tensors.erase(tensor);
    }
    Stamp(path, prim, attribute);
    return true;
}

std::uint64_t Scene::AttributeVersion(const Path& path, std::string_view attribute) const {
    const auto prim = prims_.find(path);
    if (prim == prims_.end()) {
        return 0;
    }
    const auto it = prim->second.versions.find(attribute);
    return it != prim->second.versions.end() ? it->second : 0;
}

glm::dmat4 Scene::LocalTransform(const Path& path) const {
    Find(path);
    return Get<glm::dmat4>(path, tokens::kTransform, glm::dmat4(1.0));
}

glm::dmat4 Scene::WorldTransform(const Path& path) const {
    glm::dmat4 world = LocalTransform(path);
    for (Path parent = path.Parent(); !parent.IsEmpty(); parent = parent.Parent()) {
        world = LocalTransform(parent) * world;
    }
    return world;
}

bool Scene::IsVisible(const Path& path) const {
    Find(path);
    for (Path p = path; !p.IsEmpty(); p = p.Parent()) {
        const Value* visibility = Get(p, tokens::kVisibility);
        const std::string* token = visibility != nullptr ? visibility->Get<std::string>() : nullptr;
        if (token != nullptr && *token == tokens::kInvisible) {
            return false;
        }
    }
    return true;
}

std::vector<Change> Scene::Changes(ChangeCursor& cursor) const {
    std::map<Path, Dirty> changed;
    for (const auto& [path, when] : removed_) {
        if (when > cursor.version) {
            changed[path] = Dirty::Removed;
        }
    }
    for (const auto& [path, prim] : prims_) {
        if (path.IsRoot()) {
            continue;
        }
        Dirty bits = Dirty::None;
        if (prim.stamps[StampIndex(Dirty::Added)] > cursor.version) {
            bits = Dirty::Added;
        } else {
            for (std::size_t i = 0; i < kStampCount; ++i) {
                if (prim.stamps[i] > cursor.version) {
                    bits |= StampBit(i);
                }
            }
        }
        if (bits != Dirty::None) {
            changed[path] |= bits;
        }
    }
    cursor.version = version_;
    std::vector<Change> changes;
    changes.reserve(changed.size());
    for (auto& [path, bits] : changed) {
        changes.push_back({path, bits});
    }
    return changes;
}

}  // namespace ernie
