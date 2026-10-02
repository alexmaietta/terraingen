#pragma once

#include "ernie/interop/tensor.h"
#include "ernie/scene/path.h"
#include "ernie/scene/value.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ernie {

/// What changed about a prim, as bit flags; see `DirtyOf` for which attributes set which bit.
enum class Dirty : std::uint32_t {
    None = 0,
    Added = 1u << 0,       ///< New, or redefined with another type: rebuild everything.
    Removed = 1u << 1,     ///< Gone; drop it. With `Added`, drop the old prim first.
    Transform = 1u << 2,   ///< Its own or an ancestor's transform.
    Topology = 1u << 3,    ///< Connectivity: face counts and indices, prototypes.
    Points = 1u << 4,      ///< Bulk positions: points, heights, instance positions and the like.
    Primvars = 1u << 5,    ///< `primvars:*`.
    Material = 1u << 6,    ///< The `material:binding` relationship.
    Params = 1u << 7,      ///< Any other attribute.
    Visibility = 1u << 8,  ///< Its own or an ancestor's visibility.
};

inline constexpr Dirty operator|(Dirty a, Dirty b) {
    return static_cast<Dirty>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

inline constexpr Dirty operator&(Dirty a, Dirty b) {
    return static_cast<Dirty>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

inline constexpr Dirty& operator|=(Dirty& a, Dirty b) {
    return a = a | b;
}

/// Whether `bits` has any of `flags`.
inline constexpr bool Any(Dirty bits, Dirty flags) {
    return (bits & flags) != Dirty::None;
}

/// The dirty bit that setting or writing `attribute` raises.
Dirty DirtyOf(std::string_view attribute);

/// A prim's changes since a consumer last looked.
struct Change {
    Path path;
    Dirty bits = Dirty::None;
};

/// A consumer's position in a scene's history; see `Scene::Changes`.
struct ChangeCursor {
    std::uint64_t version = 0;
};

class Scene;

/// Handle to a prim in a `Scene`: its path plus the scene, for chained edits. Copyable and cheap;
/// valid while the scene lives, though the prim it names may be removed.
class Prim {
  public:
    Prim() = default;
    Prim(Scene* scene, Path path) : scene_(scene), path_(std::move(path)) {}

    /// Whether the handle names a prim that exists.
    bool IsValid() const;

    explicit operator bool() const {
        return IsValid();
    }

    const Path& GetPath() const {
        return path_;
    }

    /// @throws std::invalid_argument if the prim does not exist.
    const std::string& Type() const;

    /// As `Scene::Set`; returns this handle for chaining.
    Prim& Set(std::string_view attribute, Value value);

    /// As `Scene::Write`; returns this handle for chaining.
    Prim& Write(std::string_view attribute, Tensor tensor);

    const Value* Get(std::string_view attribute) const;

    template <typename T> T Get(std::string_view attribute, T fallback) const;

    const Tensor* Read(std::string_view attribute) const;

  private:
    Scene* scene_ = nullptr;
    Path path_;
};

/// A lightweight, USD-shaped scene for Ernie: a tree of typed prims named by `Path`, each holding
/// attributes by USD name, plus change tracking for render delegates.
///
/// Small values (`Value`: scalars, vectors, matrices, tokens, relationships) are set one at a time;
/// bulk data (`Tensor`: heights, points, instance transforms) is written as tensors, either to one
/// prim or as rows of one tensor spread over many prims without copying. Attributes are not checked
/// against schemas; `tokens.h` names the ones Ernie reads.
///
/// Every edit advances `Version()` and stamps the prim's dirty bit for that attribute (`DirtyOf`);
/// transform and visibility edits stamp the prim's descendants too. Each consumer keeps a
/// `ChangeCursor` and calls `Changes` to get every prim changed since its last call, so several
/// consumers can follow one scene independently. `Changes` costs O(prims).
///
/// Defining a prim defines its missing ancestors as typeless prims, as `UsdStage::DefinePrim`
/// does. The root `/` always exists, untyped.
///
/// References:
/// - Pixar, "UsdStage", https://openusd.org/release/api/class_usd_stage.html
/// - Pixar, "HdChangeTracker" (dirty bits),
/// https://openusd.org/release/api/class_hd_change_tracker.html
class Scene {
  public:
    Scene();

    /// Defines the prim at `path` with `type` and returns it. An existing prim of the same type is
    /// returned unchanged; one of another type is retyped, loses its attributes and reports
    /// `Added`.
    /// @throws std::invalid_argument for the empty path or the root.
    Prim Define(const Path& path, std::string_view type);

    /// Removes the prim and its descendants; false if it did not exist.
    /// @throws std::invalid_argument for the root.
    bool Remove(const Path& path);

    bool Has(const Path& path) const;

    /// Handle to the prim at `path`, invalid if there is none.
    Prim GetPrim(const Path& path);

    /// @throws std::invalid_argument if the prim does not exist.
    const std::string& TypeOf(const Path& path) const;

    /// The children of `path`, in path order.
    std::vector<Path> Children(const Path& path) const;

    /// Every prim but the root, in path order, or only those of `type` when it is not empty.
    std::vector<Path> Prims(std::string_view type = {}) const;

    /// Sets `attribute` on the prim at `path`, replacing any tensor of that name.
    /// @throws std::invalid_argument if the prim does not exist.
    void Set(const Path& path, std::string_view attribute, Value value);

    /// The value of `attribute`, or null if it is unset or holds a tensor.
    const Value* Get(const Path& path, std::string_view attribute) const;

    /// The value of `attribute` as `T`, or `fallback` if it is unset or of another type.
    template <typename T> T Get(const Path& path, std::string_view attribute, T fallback) const {
        const Value* value = Get(path, attribute);
        const T* typed = value != nullptr ? value->Get<T>() : nullptr;
        return typed != nullptr ? *typed : fallback;
    }

    /// Writes `tensor` as `attribute` of the prim at `path`, replacing any value of that name. The
    /// scene keeps the tensor, sharing its memory, until the attribute is replaced or removed.
    /// @throws std::invalid_argument if the prim does not exist.
    void Write(const Path& path, std::string_view attribute, Tensor tensor);

    /// Writes row `i` of `rows` (`Tensor::Row`) as `attribute` of `paths[i]`, so one tensor from a
    /// simulation updates many prims at once without copying.
    /// @throws std::invalid_argument if a prim does not exist or the first dimension of `rows` is
    /// not `paths.size()`; nothing is written then.
    void Write(std::span<const Path> paths, std::string_view attribute, const Tensor& rows);

    /// The tensor of `attribute`, or null if it is unset or holds a value.
    const Tensor* Read(const Path& path, std::string_view attribute) const;

    /// The attribute names set or written on the prim at `path`, in name order.
    /// @throws std::invalid_argument if the prim does not exist.
    std::vector<std::string> Attributes(const Path& path) const;

    /// Removes `attribute` from the prim; false if it was not set.
    /// @throws std::invalid_argument if the prim does not exist.
    bool Clear(const Path& path, std::string_view attribute);

    /// The version at which `attribute` was last set, written or cleared on the prim at `path`, or
    /// 0 if it never was or the prim does not exist. Lets consumers skip work for attributes that
    /// have not changed, such as re-uploading a heightfield when only a display option moved.
    std::uint64_t AttributeVersion(const Path& path, std::string_view attribute) const;

    /// The prim's `xformOp:transform`, or identity.
    glm::dmat4 LocalTransform(const Path& path) const;

    /// The product of the local transforms from the root down to the prim.
    glm::dmat4 WorldTransform(const Path& path) const;

    /// Whether neither the prim nor any ancestor has `visibility` set to `invisible`.
    bool IsVisible(const Path& path) const;

    /// Increases with every edit.
    std::uint64_t Version() const {
        return version_;
    }

    /// Unique among the scenes of this process, copies included, so a consumer can tell when it is
    /// handed a different scene and its `ChangeCursor` no longer applies.
    std::uint64_t Id() const {
        return id_.value;
    }

    /// The prims changed since `cursor`, in path order, and advances `cursor` to `Version()`. A
    /// prim added since the cursor reports `Added` alone (with `Removed` if an older prim of that
    /// path was removed meanwhile), since consumers rebuild new prims fully. A default cursor sees
    /// every prim as added.
    std::vector<Change> Changes(ChangeCursor& cursor) const;

  private:
    static constexpr std::size_t kStampCount = 9;

    /// A process-wide unique id that copies and assignments renew.
    struct UniqueId {
        std::uint64_t value;

        UniqueId();
        UniqueId(const UniqueId&) : UniqueId() {}
        UniqueId& operator=(const UniqueId&) {
            *this = UniqueId();
            return *this;
        }
        UniqueId(UniqueId&&) noexcept = default;
        UniqueId& operator=(UniqueId&&) noexcept = default;
    };

    struct PrimData {
        std::string type;
        std::map<std::string, Value, std::less<>> values;
        std::map<std::string, Tensor, std::less<>> tensors;
        std::map<std::string, std::uint64_t, std::less<>> versions;
        std::array<std::uint64_t, kStampCount> stamps{};
    };

    PrimData& Find(const Path& path);
    const PrimData& Find(const Path& path) const;
    void Stamp(const Path& path, PrimData& prim, std::string_view attribute);
    void Stamp(const Path& path, PrimData& prim, Dirty bit);
    PrimData& CreateIfMissing(const Path& path);

    std::map<Path, PrimData> prims_;
    std::map<Path, std::uint64_t> removed_;
    std::uint64_t version_ = 0;
    UniqueId id_;
};

template <typename T> T Prim::Get(std::string_view attribute, T fallback) const {
    return scene_ != nullptr ? scene_->Get<T>(path_, attribute, fallback) : fallback;
}

}  // namespace ernie
