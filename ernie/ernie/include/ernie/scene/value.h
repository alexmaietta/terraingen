#pragma once

#include "ernie/scene/path.h"

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ernie {

/// A scene attribute value: one of the scalar, vector, matrix, token, relationship and small-array
/// types Ernie's schemas use. Bulk per-element data goes in a `Tensor` instead.
///
/// Strings serve both as USD tokens (`"invisible"`, `"lambert"`) and asset paths; `Path` values are
/// relationship targets such as `material:binding`.
class Value {
  public:
    using Data = std::variant<std::monostate, bool, int, float, double, glm::ivec2, glm::vec2,
                              glm::vec3, glm::vec4, glm::dvec2, glm::dvec3, glm::dmat4, std::string,
                              Path, std::vector<float>>;

    Value() = default;
    Value(bool value) : data_(value) {}
    Value(int value) : data_(value) {}
    Value(float value) : data_(value) {}
    Value(double value) : data_(value) {}
    Value(glm::ivec2 value) : data_(value) {}
    Value(glm::vec2 value) : data_(value) {}
    Value(glm::vec3 value) : data_(value) {}
    Value(glm::vec4 value) : data_(value) {}
    Value(glm::dvec2 value) : data_(value) {}
    Value(glm::dvec3 value) : data_(value) {}
    Value(const glm::dmat4& value) : data_(value) {}
    Value(std::string value) : data_(std::move(value)) {}
    Value(std::string_view value) : data_(std::string(value)) {}
    Value(const char* value) : data_(std::string(value)) {}
    Value(Path value) : data_(std::move(value)) {}
    Value(std::vector<float> value) : data_(std::move(value)) {}

    bool Empty() const {
        return std::holds_alternative<std::monostate>(data_);
    }

    template <typename T> bool Is() const {
        return std::holds_alternative<T>(data_);
    }

    /// The value as `T`, or null if it holds another type.
    template <typename T> const T* Get() const {
        return std::get_if<T>(&data_);
    }

    const Data& Raw() const {
        return data_;
    }

    bool operator==(const Value&) const = default;

  private:
    Data data_;
};

}  // namespace ernie
