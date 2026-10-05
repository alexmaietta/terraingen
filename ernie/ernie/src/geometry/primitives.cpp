#include "ernie/geometry/primitives.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace ernie {

namespace {

constexpr float kPi = 3.14159265358979f;

/// A point of a surface of revolution's profile: distance from the axis, height, and the outward
/// normal's radial and axial parts.
struct ProfilePoint {
    float radius;
    float height;
    float normalRadial;
    float normalAxial;
};

class Builder {
  public:
    std::uint32_t Add(const glm::vec3& point, const glm::vec3& normal) {
        const auto index = static_cast<std::uint32_t>(mesh_.points.size() / 3);
        mesh_.points.insert(mesh_.points.end(), {point.x, point.y, point.z});
        const glm::vec3 n = glm::normalize(normal);
        mesh_.normals.insert(mesh_.normals.end(), {n.x, n.y, n.z});
        return index;
    }

    void Triangle(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        mesh_.indices.insert(mesh_.indices.end(), {a, b, c});
    }

    /// A surface of revolution around +z through `profile`, listed from the top down.
    void Revolve(const std::vector<ProfilePoint>& profile, std::uint32_t slices) {
        const std::uint32_t first = Count();
        for (const ProfilePoint& p : profile) {
            for (std::uint32_t j = 0; j <= slices; ++j) {
                const float phi = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(slices);
                const glm::vec2 around(std::cos(phi), std::sin(phi));
                Add({p.radius * around, p.height}, {p.normalRadial * around, p.normalAxial});
            }
        }
        const std::uint32_t row = slices + 1;
        for (std::uint32_t i = 0; i + 1 < profile.size(); ++i) {
            for (std::uint32_t j = 0; j < slices; ++j) {
                const std::uint32_t a = first + i * row + j;
                const std::uint32_t c = a + row;
                Triangle(a, c, a + 1);
                Triangle(a + 1, c, c + 1);
            }
        }
    }

    /// A disc of `radius` at `height` facing +z (`up`) or -z.
    void Disc(float radius, float height, bool up, std::uint32_t slices) {
        const glm::vec3 normal(0.0f, 0.0f, up ? 1.0f : -1.0f);
        const std::uint32_t centre = Add({0.0f, 0.0f, height}, normal);
        const std::uint32_t first = Count();
        for (std::uint32_t j = 0; j <= slices; ++j) {
            const float phi = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(slices);
            Add({radius * std::cos(phi), radius * std::sin(phi), height}, normal);
        }
        for (std::uint32_t j = 0; j < slices; ++j) {
            if (up) {
                Triangle(centre, first + j, first + j + 1);
            } else {
                Triangle(centre, first + j + 1, first + j);
            }
        }
    }

    /// A quad with corners `centre` -+ `u` -+ `v`, facing u x v.
    void Quad(const glm::vec3& centre, const glm::vec3& u, const glm::vec3& v) {
        const glm::vec3 normal = glm::cross(u, v);
        const std::uint32_t a = Add(centre - u - v, normal);
        const std::uint32_t b = Add(centre + u - v, normal);
        const std::uint32_t c = Add(centre + u + v, normal);
        const std::uint32_t d = Add(centre - u + v, normal);
        Triangle(a, b, c);
        Triangle(a, c, d);
    }

    /// The mesh, turned from +z to `axis`.
    MeshData Take(Axis axis) {
        if (axis != Axis::Z) {
            for (std::vector<float>* values : {&mesh_.points, &mesh_.normals}) {
                for (std::size_t i = 0; i < values->size(); i += 3) {
                    float* v = values->data() + i;
                    const glm::vec3 p(v[0], v[1], v[2]);
                    const glm::vec3 turned =
                        axis == Axis::X ? glm::vec3(p.z, p.x, p.y) : glm::vec3(p.y, p.z, p.x);
                    v[0] = turned.x;
                    v[1] = turned.y;
                    v[2] = turned.z;
                }
            }
        }
        return std::move(mesh_);
    }

  private:
    std::uint32_t Count() const {
        return static_cast<std::uint32_t>(mesh_.points.size() / 3);
    }

    MeshData mesh_;
};

void CheckPositive(float value, const char* what) {
    if (!(value > 0.0f) || !std::isfinite(value)) {
        throw std::invalid_argument(std::string("A primitive's ") + what +
                                    " must be positive and finite");
    }
}

void CheckSlices(std::uint32_t slices) {
    if (slices < 3) {
        throw std::invalid_argument("A primitive needs at least 3 slices");
    }
}

void CheckStacks(std::uint32_t stacks) {
    if (stacks < 2) {
        throw std::invalid_argument("A primitive needs at least 2 stacks");
    }
}

/// `count + 1` profile points around a circular arc of `radius` centred at `height`, from polar
/// angle `from` to `to` (0 at +z).
void Arc(std::vector<ProfilePoint>& profile, float radius, float height, float from, float to,
         std::uint32_t count) {
    for (std::uint32_t i = 0; i <= count; ++i) {
        const float theta = from + (to - from) * static_cast<float>(i) / static_cast<float>(count);
        // Poles land exactly on the axis, as sin(pi) is not 0 in float.
        const bool pole = theta == 0.0f || theta == kPi;
        const float s = pole ? 0.0f : std::sin(theta);
        const float c = pole ? (theta == 0.0f ? 1.0f : -1.0f) : std::cos(theta);
        profile.push_back({radius * s, height + radius * c, s, c});
    }
}

}  // namespace

std::optional<Axis> AxisNamed(std::string_view name) {
    if (name == "X") {
        return Axis::X;
    }
    if (name == "Y") {
        return Axis::Y;
    }
    if (name == "Z") {
        return Axis::Z;
    }
    return std::nullopt;
}

MeshData BoxMesh(const glm::vec3& size) {
    CheckPositive(size.x, "width");
    CheckPositive(size.y, "depth");
    CheckPositive(size.z, "height");
    const glm::vec3 half = 0.5f * size;
    Builder builder;
    for (int axis = 0; axis < 3; ++axis) {
        for (const float sign : {1.0f, -1.0f}) {
            glm::vec3 normal(0.0f);
            normal[axis] = sign;
            glm::vec3 u(0.0f);
            glm::vec3 v(0.0f);
            u[(axis + 1) % 3] = sign * half[(axis + 1) % 3];
            v[(axis + 2) % 3] = half[(axis + 2) % 3];
            builder.Quad(normal * half[axis], u, v);
        }
    }
    return builder.Take(Axis::Z);
}

MeshData SphereMesh(float radius, std::uint32_t slices, std::uint32_t stacks) {
    CheckPositive(radius, "radius");
    CheckSlices(slices);
    CheckStacks(stacks);
    std::vector<ProfilePoint> profile;
    Arc(profile, radius, 0.0f, 0.0f, kPi, stacks);
    Builder builder;
    builder.Revolve(profile, slices);
    return builder.Take(Axis::Z);
}

MeshData CylinderMesh(float radius, float height, Axis axis, std::uint32_t slices) {
    CheckPositive(radius, "radius");
    CheckPositive(height, "height");
    CheckSlices(slices);
    const float half = 0.5f * height;
    Builder builder;
    builder.Revolve({{radius, half, 1.0f, 0.0f}, {radius, -half, 1.0f, 0.0f}}, slices);
    builder.Disc(radius, half, true, slices);
    builder.Disc(radius, -half, false, slices);
    return builder.Take(axis);
}

MeshData ConeMesh(float radius, float height, Axis axis, std::uint32_t slices) {
    CheckPositive(radius, "radius");
    CheckPositive(height, "height");
    CheckSlices(slices);
    const float half = 0.5f * height;
    const float slant = std::sqrt(radius * radius + height * height);
    const float radial = height / slant;
    const float axial = radius / slant;
    Builder builder;
    builder.Revolve({{0.0f, half, radial, axial}, {radius, -half, radial, axial}}, slices);
    builder.Disc(radius, -half, false, slices);
    return builder.Take(axis);
}

MeshData CapsuleMesh(float radius, float height, Axis axis, std::uint32_t slices,
                     std::uint32_t stacks) {
    CheckPositive(radius, "radius");
    if (!(height >= 0.0f) || !std::isfinite(height)) {
        throw std::invalid_argument("A capsule's height must be finite and not negative");
    }
    CheckSlices(slices);
    CheckStacks(stacks);
    const std::uint32_t cap = std::max(stacks / 2, 1u);
    const float half = 0.5f * height;
    std::vector<ProfilePoint> profile;
    Arc(profile, radius, half, 0.0f, 0.5f * kPi, cap);
    Arc(profile, radius, -half, 0.5f * kPi, kPi, cap);
    Builder builder;
    builder.Revolve(profile, slices);
    return builder.Take(axis);
}

MeshData PlaneMesh(float width, float length, Axis axis) {
    CheckPositive(width, "width");
    CheckPositive(length, "length");
    Builder builder;
    const float w = 0.5f * width;
    const float l = 0.5f * length;
    switch (axis) {
    case Axis::Z:
        builder.Quad({}, {w, 0.0f, 0.0f}, {0.0f, l, 0.0f});
        break;
    case Axis::Y:
        builder.Quad({}, {0.0f, 0.0f, l}, {w, 0.0f, 0.0f});
        break;
    case Axis::X:
        builder.Quad({}, {0.0f, l, 0.0f}, {0.0f, 0.0f, w});
        break;
    }
    return builder.Take(Axis::Z);
}

}  // namespace ernie
