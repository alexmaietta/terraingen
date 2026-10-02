#include "ernie/core/spectrum.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numeric>
#include <vector>

using ernie::SpectralBands;
using ernie::Spectrum;

TEST(SpectralBands, SplitTheRangeEvenly) {
    const SpectralBands bands{400.0, 700.0, 6};
    EXPECT_DOUBLE_EQ(bands.Width(), 50.0);
    EXPECT_DOUBLE_EQ(bands.Lower(2), 500.0);
    EXPECT_DOUBLE_EQ(bands.Centre(5), 675.0);
    EXPECT_NO_THROW(bands.Check());
    EXPECT_THROW((SpectralBands{700.0, 400.0, 6}.Check()), std::invalid_argument);
    EXPECT_THROW((SpectralBands{400.0, 700.0, 0}.Check()), std::invalid_argument);
    EXPECT_THROW((SpectralBands{400.0, 700.0, 65}.Check()), std::invalid_argument);
}

TEST(Spectrum, InterpolatesAndAveragesExactly) {
    const Spectrum ramp({400.0, 500.0, 700.0}, {0.0, 1.0, 1.0});
    EXPECT_DOUBLE_EQ(ramp(450.0), 0.5);
    EXPECT_DOUBLE_EQ(ramp(300.0), 0.0) << "constant beyond the samples";
    EXPECT_DOUBLE_EQ(ramp(900.0), 1.0);
    EXPECT_DOUBLE_EQ(ramp.Average(400.0, 500.0), 0.5);
    EXPECT_DOUBLE_EQ(ramp.Average(450.0, 550.0), (0.5 * 50 * (0.5 + 1.0) + 50.0) / 100.0);
    const std::vector<float> binned = ramp.Bin({400.0, 600.0, 2});
    EXPECT_FLOAT_EQ(binned[0], 0.5f);
    EXPECT_FLOAT_EQ(binned[1], 1.0f);
    EXPECT_DOUBLE_EQ(Spectrum::Constant(0.3)(123.0), 0.3);
    EXPECT_DOUBLE_EQ(Spectrum()(500.0), 0.0);

    const float pairs[] = {400.0f, 2.0f, 500.0f, 4.0f};
    EXPECT_DOUBLE_EQ(Spectrum::FromPairs(pairs)(450.0), 3.0);
    EXPECT_THROW(Spectrum::FromPairs(std::span(pairs).first(3)), std::invalid_argument);
    EXPECT_THROW(Spectrum({500.0, 400.0}, {1.0, 1.0}), std::invalid_argument);
    EXPECT_THROW(Spectrum({500.0}, {1.0, 1.0}), std::invalid_argument);
}

TEST(Spectrum, BlackbodyMatchesWiensLawAndTheTotal) {
    double peak = 0.0;
    double peakWavelength = 0.0;
    for (double w = 300.0; w <= 1000.0; w += 1.0) {
        if (ernie::PlanckRadiance(w, 5772.0) > peak) {
            peak = ernie::PlanckRadiance(w, 5772.0);
            peakWavelength = w;
        }
    }
    EXPECT_NEAR(peakWavelength, 2.897771955e6 / 5772.0, 1.0) << "Wien's displacement law";

    const SpectralBands wide{100.0, 100100.0, 64};
    const std::vector<float> all = ernie::BlackbodyIrradiance(wide, 5772.0, 1361.0);
    const double total = std::accumulate(all.begin(), all.end(), 0.0) * wide.Width();
    EXPECT_NEAR(total, 1361.0, 15.0) << "nearly all of the energy lies in 0.1-100 um";
    const std::vector<float> visible =
        ernie::BlackbodyIrradiance({380.0, 780.0, 16}, 5772.0, 1361.0);
    const double visibleTotal = std::accumulate(visible.begin(), visible.end(), 0.0) * 25.0;
    EXPECT_GT(visibleTotal, 0.4 * 1361.0);
    EXPECT_LT(visibleTotal, 0.5 * 1361.0) << "about 45 percent of sunlight is visible";
    EXPECT_THROW(ernie::BlackbodyIrradiance({380.0, 780.0, 16}, 0.0, 1.0), std::invalid_argument);
}

TEST(Spectrum, CieFitMatchesTheStandardObserver) {
    EXPECT_NEAR(ernie::CieXyz(555.0).y, 1.0, 0.01);
    const SpectralBands all{360.0, 830.0, 64};
    const std::vector<float> ones(64, 1.0f);
    const glm::dvec3 equalEnergy = ernie::ToXyz(all, ones);
    EXPECT_NEAR(equalEnergy.y, 106.857, 1.5) << "integral of y over 360-830 nm";
    EXPECT_NEAR(equalEnergy.x / equalEnergy.y, 1.0, 0.02);
    EXPECT_NEAR(equalEnergy.z / equalEnergy.y, 1.0, 0.03);
    EXPECT_THROW(ernie::ToXyz(all, std::span(ones).first(3)), std::invalid_argument);

    const glm::dvec3 white = ernie::XyzToLinearSrgb({0.95047, 1.0, 1.08883});
    EXPECT_NEAR(white.r, 1.0, 1e-3) << "D65 white maps to sRGB white";
    EXPECT_NEAR(white.g, 1.0, 1e-3);
    EXPECT_NEAR(white.b, 1.0, 1e-3);
}

TEST(Spectrum, BradfordAdaptationMapsFlatSpectraToGreys) {
    const glm::dvec3 grey = ernie::ReflectanceToLinearSrgb(Spectrum::Constant(0.5));
    EXPECT_NEAR(grey.r, 0.5, 1e-6) << "up to the rounding of the published matrices";
    EXPECT_NEAR(grey.g, 0.5, 1e-6);
    EXPECT_NEAR(grey.b, 0.5, 1e-6);
}

TEST(Spectrum, SigmoidUpsamplingRoundTripsColoursAndKeepsGreysFlat) {
    const Spectrum grey = ernie::RgbToSpectrum(glm::dvec3(0.12));
    for (double w = 380.0; w <= 780.0; w += 20.0) {
        EXPECT_NEAR(grey(w), 0.12, 1e-9);
    }
    for (const glm::dvec3 rgb :
         {glm::dvec3(0.8, 0.2, 0.1), glm::dvec3(0.1, 0.5, 0.2), glm::dvec3(0.2, 0.3, 0.7),
          glm::dvec3(0.45, 0.4, 0.35), glm::dvec3(0.9, 0.9, 0.3)}) {
        const ernie::SigmoidSpectrum sigmoid = ernie::RgbToSigmoid(rgb);
        const glm::dvec3 back = ernie::ReflectanceToLinearSrgb(sigmoid.Tabulate());
        EXPECT_NEAR(back.r, rgb.r, 2e-3) << rgb.r << " " << rgb.g << " " << rgb.b;
        EXPECT_NEAR(back.g, rgb.g, 2e-3);
        EXPECT_NEAR(back.b, rgb.b, 2e-3);
        for (double w = 360.0; w <= 830.0; w += 10.0) {
            EXPECT_GE(sigmoid(w), 0.0);
            EXPECT_LE(sigmoid(w), 1.0);
        }
    }
    const Spectrum red = ernie::RgbToSpectrum({0.8, 0.05, 0.05});
    EXPECT_GT(red(650.0), red(450.0)) << "red reflects long wavelengths";
}
