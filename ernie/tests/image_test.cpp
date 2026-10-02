#include "ernie/image.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <stb_image_write.h>
#include <tinyexr.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

using ernie::Image;
using ernie::ImageEncoding;
using ernie::LinearToSrgb;
using ernie::LoadImage;
using ernie::MipChain;
using ernie::MipFilter;
using ernie::SrgbToLinear;
using ernie::ToRgba8;

namespace {

class ImageFiles : public testing::Test {
  protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("terraingen_image_test_" +
                std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        std::filesystem::create_directories(dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(dir_);
    }

    std::filesystem::path dir_;
};

}  // namespace

TEST(Srgb, MatchesTheStandardCurveAndRoundTrips) {
    EXPECT_FLOAT_EQ(SrgbToLinear(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(SrgbToLinear(1.0f), 1.0f);
    EXPECT_NEAR(SrgbToLinear(0.5f), 0.21404f, 1e-5f);
    EXPECT_NEAR(SrgbToLinear(0.02f), 0.02f / 12.92f, 1e-7f);
    for (float v = 0.0f; v <= 1.0f; v += 0.05f) {
        EXPECT_NEAR(LinearToSrgb(SrgbToLinear(v)), v, 1e-5f);
    }
}

TEST(MipChain, HalvesDownToOneTexelAndAveragesBlocks) {
    Image image(5, 2);
    for (std::uint32_t y = 0; y < 2; ++y) {
        for (std::uint32_t x = 0; x < 5; ++x) {
            image.At(x, y) = glm::vec4(static_cast<float>(x + 10 * y));
        }
    }
    const std::vector<Image> mips = MipChain(image, MipFilter::Average);
    ASSERT_EQ(mips.size(), 3u);
    EXPECT_EQ(mips[1].width, 2u);
    EXPECT_EQ(mips[1].height, 1u);
    EXPECT_EQ(mips[2].width, 1u);
    EXPECT_EQ(mips[2].height, 1u);
    EXPECT_FLOAT_EQ(mips[1].At(0, 0).r, (0 + 1 + 10 + 11) / 4.0f);
    EXPECT_FLOAT_EQ(mips[1].At(1, 0).r, (2 + 3 + 12 + 13) / 4.0f);
    EXPECT_EQ(MipChain(Image(1, 1), MipFilter::Average).size(), 1u);
    EXPECT_EQ(MipChain(Image(1, 8), MipFilter::Average).size(), 4u);
}

TEST(MipChain, UnitVectorFilterRenormalisesNormals) {
    Image normals(2, 1);
    const glm::vec3 a = glm::normalize(glm::vec3(0.8f, 0.0f, 0.6f));
    const glm::vec3 b = glm::normalize(glm::vec3(-0.8f, 0.0f, 0.6f));
    normals.At(0, 0) = glm::vec4(0.5f * a + 0.5f, 1.0f);
    normals.At(1, 0) = glm::vec4(0.5f * b + 0.5f, 1.0f);
    const Image top = MipChain(normals, MipFilter::UnitVector).back();
    const glm::vec3 n = 2.0f * glm::vec3(top.At(0, 0)) - 1.0f;
    EXPECT_NEAR(glm::length(n), 1.0f, 1e-5f);
    EXPECT_NEAR(n.z, 1.0f, 1e-5f);

    Image opposite(2, 1);
    opposite.At(0, 0) = glm::vec4(1.0f, 0.5f, 0.5f, 1.0f);
    opposite.At(1, 0) = glm::vec4(0.0f, 0.5f, 0.5f, 1.0f);
    const glm::vec4 flat = MipChain(opposite, MipFilter::UnitVector).back().At(0, 0);
    EXPECT_FLOAT_EQ(flat.b, 1.0f) << "degenerate averages fall back to straight up";
}

TEST(ImageMean, AveragesEveryPixel) {
    Image image(2, 1);
    image.At(0, 0) = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
    image.At(1, 0) = glm::vec4(0.4f, 0.0f, 0.2f, 0.0f);
    EXPECT_NEAR(image.Mean().r, 0.3f, 1e-6f);
    EXPECT_NEAR(image.Mean().a, 0.5f, 1e-6f);
    EXPECT_EQ(Image().Mean(), glm::vec4(0.0f));
}

TEST(ToRgba8, QuantisesWithOrWithoutTheSrgbCurve) {
    Image image(2, 1);
    image.At(0, 0) = glm::vec4(SrgbToLinear(128.0f / 255.0f), 1.5f, -1.0f, 0.5f);
    image.At(1, 0) = glm::vec4(1.0f);
    const auto srgb = ToRgba8(image, ImageEncoding::Srgb);
    EXPECT_EQ(srgb[0], 128);
    EXPECT_EQ(srgb[1], 255);
    EXPECT_EQ(srgb[2], 0);
    EXPECT_EQ(srgb[3], 128) << "alpha is linear";
    const auto linear = ToRgba8(image, ImageEncoding::Linear);
    EXPECT_EQ(linear[0], 55);
}

TEST_F(ImageFiles, LoadsEightBitPngWithEitherEncoding) {
    const std::array<std::uint8_t, 8> rgba = {128, 0, 255, 255, 64, 32, 16, 128};
    const auto path = dir_ / "colour.png";
    ASSERT_NE(stbi_write_png(path.string().c_str(), 2, 1, 4, rgba.data(), 8), 0);

    const Image linear = LoadImage(path, ImageEncoding::Linear);
    ASSERT_EQ(linear.width, 2u);
    ASSERT_EQ(linear.height, 1u);
    EXPECT_NEAR(linear.At(0, 0).r, 128.0f / 255.0f, 1e-6f);
    EXPECT_NEAR(linear.At(1, 0).a, 128.0f / 255.0f, 1e-6f);
    const Image srgb = LoadImage(path, ImageEncoding::Srgb);
    EXPECT_NEAR(srgb.At(0, 0).r, SrgbToLinear(128.0f / 255.0f), 1e-6f);
    EXPECT_NEAR(srgb.At(1, 0).a, 128.0f / 255.0f, 1e-6f) << "alpha stays linear";

    const std::array<std::uint8_t, 2> grey = {0, 200};
    const auto greyPath = dir_ / "GREY.PNG";
    ASSERT_NE(stbi_write_png(greyPath.string().c_str(), 2, 1, 1, grey.data(), 2), 0);
    const Image expanded = LoadImage(greyPath, ImageEncoding::Linear);
    EXPECT_NEAR(expanded.At(1, 0).g, 200.0f / 255.0f, 1e-6f);
    EXPECT_FLOAT_EQ(expanded.At(1, 0).a, 1.0f);
}

TEST_F(ImageFiles, LoadsSixteenBitPng) {
    const Image image =
        LoadImage(std::filesystem::path(ERNIE_TEST_FIXTURES) / "images" / "grey16.png",
                  ImageEncoding::Linear);
    ASSERT_EQ(image.width, 2u);
    EXPECT_FLOAT_EQ(image.At(0, 0).r, 0.0f);
    EXPECT_FLOAT_EQ(image.At(1, 0).b, 1.0f);
    EXPECT_NEAR(image.At(0, 1).r, 32768.0f / 65535.0f, 1e-7f);
    EXPECT_NEAR(image.At(1, 1).r, 1000.0f / 65535.0f, 1e-7f) << "16-bit precision kept";
}

TEST_F(ImageFiles, LoadsJpeg) {
    const std::array<std::uint8_t, 3 * 16> grey([] {
        std::array<std::uint8_t, 3 * 16> a{};
        a.fill(100);
        return a;
    }());
    const auto path = dir_ / "flat.jpg";
    ASSERT_NE(stbi_write_jpg(path.string().c_str(), 4, 4, 3, grey.data(), 95), 0);
    const Image image = LoadImage(path, ImageEncoding::Linear);
    EXPECT_EQ(image.width, 4u);
    EXPECT_NEAR(image.At(2, 2).r, 100.0f / 255.0f, 3.0f / 255.0f);
}

TEST_F(ImageFiles, LoadsFloatExrAsLinear) {
    const std::vector<float> rgba = {0.25f, 2.0f, -0.5f, 1.0f, 0.0f, 0.125f, 10.0f, 0.5f};
    const auto path = dir_ / "data.exr";
    const char* error = nullptr;
    ASSERT_EQ(SaveEXR(rgba.data(), 2, 1, 4, 0, path.string().c_str(), &error), TINYEXR_SUCCESS)
        << (error != nullptr ? error : "");
    const Image image = LoadImage(path, ImageEncoding::Srgb);
    ASSERT_EQ(image.width, 2u);
    EXPECT_FLOAT_EQ(image.At(0, 0).r, 0.25f) << "EXR ignores the sRGB encoding";
    EXPECT_FLOAT_EQ(image.At(0, 0).g, 2.0f);
    EXPECT_FLOAT_EQ(image.At(0, 0).b, -0.5f);
    EXPECT_FLOAT_EQ(image.At(1, 0).b, 10.0f);
    EXPECT_FLOAT_EQ(image.At(1, 0).a, 0.5f);
}

TEST_F(ImageFiles, ReportsMissingCorruptAndUnsupportedFiles) {
    EXPECT_THROW(LoadImage(dir_ / "missing.png", ImageEncoding::Linear), std::runtime_error);
    EXPECT_THROW(LoadImage(dir_ / "missing.exr", ImageEncoding::Linear), std::runtime_error);
    std::ofstream(dir_ / "corrupt.png") << "not a png";
    EXPECT_THROW(LoadImage(dir_ / "corrupt.png", ImageEncoding::Linear), std::runtime_error);
    std::ofstream(dir_ / "texture.tga") << "tga";
    EXPECT_THROW(LoadImage(dir_ / "texture.tga", ImageEncoding::Linear), std::runtime_error);
}
