#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace terraingen::render {

/// How 8- and 16-bit stored values map to linear values.
enum class ImageEncoding {
    Srgb,    ///< IEC 61966-2-1 sRGB transfer function, for colour textures.
    Linear,  ///< Stored values are linear, for normal and data maps.
};

/// How `MipChain` filters each level.
enum class MipFilter {
    Average,     ///< 2 x 2 box filter of every channel.
    UnitVector,  ///< Box filter of tangent-space normals stored as rgb in [0, 1], renormalised.
};

/// RGBA image with linear float channels, rows top to bottom.
struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<glm::vec4> pixels;

    Image() = default;
    Image(std::uint32_t w, std::uint32_t h, const glm::vec4& fill = glm::vec4(0.0f))
        : width(w), height(h), pixels(static_cast<std::size_t>(w) * h, fill) {}

    glm::vec4& At(std::uint32_t x, std::uint32_t y) {
        return pixels[static_cast<std::size_t>(y) * width + x];
    }

    const glm::vec4& At(std::uint32_t x, std::uint32_t y) const {
        return pixels[static_cast<std::size_t>(y) * width + x];
    }

    /// Mean of every pixel.
    glm::vec4 Mean() const;
};

/// Loads a PNG (8- or 16-bit), JPEG or OpenEXR file.
///
/// PNG and JPEG values decode with `encoding`; EXR values are linear already, so `encoding` is
/// ignored for them. Grey images replicate into rgb and a missing alpha channel reads as 1. PNG and
/// JPEG use stb_image; EXR uses tinyexr, which reads scanline and tiled images with no, RLE, ZIP
/// and PIZ compression but not DWA or B44.
/// @throws std::runtime_error if the file cannot be read or decoded, or its extension is not
/// .png, .jpg, .jpeg or .exr.
Image LoadImage(const std::filesystem::path& path, ImageEncoding encoding);

/// IEC 61966-2-1 sRGB transfer function for one channel in [0, 1]: decode to linear and encode.
float SrgbToLinear(float value);
float LinearToSrgb(float value);

/// Mip levels of `image` from full size down to 1 x 1, level `n` being
/// `max(1, floor(size / 2^n))` on each side as WebGPU expects. Each texel averages the 2 x 2 texels
/// above it, clamped at odd edges.
std::vector<Image> MipChain(const Image& image, MipFilter filter);

/// Quantises to 8-bit RGBA, sRGB-encoding rgb when `encoding` is `Srgb`; alpha is always linear.
std::vector<std::uint8_t> ToRgba8(const Image& image, ImageEncoding encoding);

}  // namespace terraingen::render
