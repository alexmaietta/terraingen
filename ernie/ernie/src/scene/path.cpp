#include "ernie/scene/path.h"

#include <algorithm>
#include <stdexcept>

namespace ernie {

namespace {

bool IsNameStart(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool IsNameChar(char c) {
    return IsNameStart(c) || (c >= '0' && c <= '9');
}

}  // namespace

bool Path::IsValidName(std::string_view name) {
    if (name.empty() || !IsNameStart(name.front())) {
        return false;
    }
    for (const char c : name) {
        if (!IsNameChar(c)) {
            return false;
        }
    }
    return true;
}

Path::Path(std::string_view text) {
    if (text.empty() || text.front() != '/') {
        throw std::invalid_argument("Prim path '" + std::string(text) + "' is not absolute");
    }
    if (text.size() > 1) {
        std::size_t start = 1;
        while (start <= text.size()) {
            const std::size_t end = std::min(text.find('/', start), text.size());
            if (!IsValidName(text.substr(start, end - start))) {
                throw std::invalid_argument("Prim path '" + std::string(text) +
                                            "' has an invalid name");
            }
            start = end + 1;
        }
    }
    text_ = text;
}

Path Path::Root() {
    return Path("/");
}

std::string_view Path::Name() const {
    if (text_.size() <= 1) {
        return {};
    }
    return std::string_view(text_).substr(text_.rfind('/') + 1);
}

Path Path::Parent() const {
    if (text_.size() <= 1) {
        return {};
    }
    const std::size_t slash = text_.rfind('/');
    return slash == 0 ? Root() : Path(std::string_view(text_).substr(0, slash));
}

Path Path::Child(std::string_view name) const {
    if (IsEmpty() || !IsValidName(name)) {
        throw std::invalid_argument("Cannot append '" + std::string(name) + "' to '" + text_ + "'");
    }
    Path child;
    child.text_ = IsRoot() ? "/" + std::string(name) : text_ + "/" + std::string(name);
    return child;
}

bool Path::HasPrefix(const Path& ancestor) const {
    if (ancestor.IsEmpty() || IsEmpty()) {
        return false;
    }
    if (ancestor.IsRoot()) {
        return true;
    }
    return text_.starts_with(ancestor.text_) &&
           (text_.size() == ancestor.text_.size() || text_[ancestor.text_.size()] == '/');
}

}  // namespace ernie
