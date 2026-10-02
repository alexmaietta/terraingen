#include "ernie/image.h"

#include <stb_image.h>
#include <tinyexr.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace ernie {

namespace {

std::string Lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

Image LoadExr(const std::filesystem::path& path) {
    float* rgba = nullptr;
    int width = 0;
    int height = 0;
    const char* error = nullptr;
    if (LoadEXR(&rgba, &width, &height, path.string().c_str(), &error) != TINYEXR_SUCCESS) {
        const std::string message = error != nullptr ? error : "unknown error";
        FreeEXRErrorMessage(error);
        throw std::runtime_error("Cannot read " + path.string() + ": " + message);
    }
    Image image(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        image.pixels[i] = glm::vec4(rgba[4 * i], rgba[4 * i + 1], rgba[4 * i + 2], rgba[4 * i + 3]);
    }
    std::free(rgba);
    return image;
}

template <typename T>
Image Decode(T* data, int width, int height, float scale, ImageEncoding encoding) {
    Image image(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        glm::vec4 value;
        for (int c = 0; c < 4; ++c) {
            value[c] = static_cast<float>(data[4 * i + static_cast<std::size_t>(c)]) * scale;
        }
        if (encoding == ImageEncoding::Srgb) {
            value = glm::vec4(SrgbToLinear(value.r), SrgbToLinear(value.g), SrgbToLinear(value.b),
                              value.a);
        }
        image.pixels[i] = value;
    }
    stbi_image_free(data);
    return image;
}

Image LoadStb(const std::filesystem::path& path, ImageEncoding encoding) {
    const std::string name = path.string();
    int width = 0;
    int height = 0;
    int channels = 0;
    if (stbi_is_16_bit(name.c_str()) != 0) {
        if (stbi_us* data = stbi_load_16(name.c_str(), &width, &height, &channels, 4)) {
            return Decode(data, width, height, 1.0f / 65535.0f, encoding);
        }
    } else if (stbi_uc* data = stbi_load(name.c_str(), &width, &height, &channels, 4)) {
        return Decode(data, width, height, 1.0f / 255.0f, encoding);
    }
    throw std::runtime_error("Cannot read " + name + ": " + stbi_failure_reason());
}

}  // namespace

glm::vec4 Image::Mean() const {
    glm::dvec4 sum(0.0);
    for (const glm::vec4& p : pixels) {
        sum += glm::dvec4(p);
    }
    return pixels.empty() ? glm::vec4(0.0f) : glm::vec4(sum / static_cast<double>(pixels.size()));
}

Image LoadImage(const std::filesystem::path& path, ImageEncoding encoding) {
    const std::string extension = Lowercase(path.extension().string());
    if (extension == ".exr") {
        return LoadExr(path);
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg") {
        return LoadStb(path, encoding);
    }
    throw std::runtime_error("Unsupported image format: " + path.string());
}

float SrgbToLinear(float value) {
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float LinearToSrgb(float value) {
    return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

std::vector<Image> MipChain(const Image& image, MipFilter filter) {
    std::vector<Image> levels{image};
    while (levels.back().width > 1 || levels.back().height > 1) {
        const Image& above = levels.back();
        Image level(std::max(1u, above.width / 2), std::max(1u, above.height / 2));
        for (std::uint32_t y = 0; y < level.height; ++y) {
            const std::uint32_t y0 = std::min(2 * y, above.height - 1);
            const std::uint32_t y1 = std::min(2 * y + 1, above.height - 1);
            for (std::uint32_t x = 0; x < level.width; ++x) {
                const std::uint32_t x0 = std::min(2 * x, above.width - 1);
                const std::uint32_t x1 = std::min(2 * x + 1, above.width - 1);
                glm::vec4 texel = 0.25f * (above.At(x0, y0) + above.At(x1, y0) + above.At(x0, y1) +
                                           above.At(x1, y1));
                if (filter == MipFilter::UnitVector) {
                    glm::vec3 n = 2.0f * glm::vec3(texel) - 1.0f;
                    const float length = glm::length(n);
                    n = length > 0.0f ? n / length : glm::vec3(0.0f, 0.0f, 1.0f);
                    texel = glm::vec4(0.5f * n + 0.5f, texel.a);
                }
                level.At(x, y) = texel;
            }
        }
        levels.push_back(std::move(level));
    }
    return levels;
}

std::vector<std::uint8_t> ToRgba8(const Image& image, ImageEncoding encoding) {
    std::vector<std::uint8_t> bytes(image.pixels.size() * 4);
    auto quantise = [](float value) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        const glm::vec4& p = image.pixels[i];
        for (int c = 0; c < 3; ++c) {
            const float value = encoding == ImageEncoding::Srgb ? LinearToSrgb(p[c]) : p[c];
            bytes[4 * i + static_cast<std::size_t>(c)] = quantise(value);
        }
        bytes[4 * i + 3] = quantise(p.a);
    }
    return bytes;
}

}  // namespace ernie
