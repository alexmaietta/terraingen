#pragma once

#include <compare>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace ernie {

/// Absolute path naming a prim in a `Scene`, as in USD: `/` is the root and `/World/Sun` is the
/// child `Sun` of `/World`.
///
/// Names are identifiers: a letter or underscore followed by letters, digits and underscores. A
/// default-constructed path is empty and names nothing. Paths order by their text, so a prim's
/// descendants follow it contiguously.
///
/// Reference: Pixar, "SdfPath", https://openusd.org/release/api/class_sdf_path.html
class Path {
  public:
    Path() = default;

    /// Parses `text`.
    /// @throws std::invalid_argument if `text` is not an absolute path of valid names.
    Path(std::string_view text);
    Path(const char* text) : Path(std::string_view(text)) {}
    Path(const std::string& text) : Path(std::string_view(text)) {}

    static Path Root();

    /// Whether `name` is a valid prim name.
    static bool IsValidName(std::string_view name);

    const std::string& String() const {
        return text_;
    }

    bool IsEmpty() const {
        return text_.empty();
    }

    bool IsRoot() const {
        return text_ == "/";
    }

    /// The last name, or an empty view for the root and the empty path.
    std::string_view Name() const;

    /// The parent path; the root and the empty path have an empty parent.
    Path Parent() const;

    /// The child `name` of this path.
    /// @throws std::invalid_argument if this path is empty or `name` is not a valid name.
    Path Child(std::string_view name) const;

    /// Whether this path is `ancestor` or lies below it.
    bool HasPrefix(const Path& ancestor) const;

    auto operator<=>(const Path&) const = default;

  private:
    std::string text_;
};

}  // namespace ernie

template <> struct std::hash<ernie::Path> {
    std::size_t operator()(const ernie::Path& path) const noexcept {
        return std::hash<std::string>()(path.String());
    }
};
