#pragma once

#include <string>

namespace ernie {

/// One Slang module given as source text. Other modules `import` it by `name`.
struct ShaderModuleSource {
    std::string name;
    std::string source;
};

}  // namespace ernie
