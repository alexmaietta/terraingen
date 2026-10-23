#include "terraingen/raster/resample.h"

#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>

using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::Resample;
using terraingen::ResampleMethod;

namespace {

/// A `width` x `height` raster of 2 m cells with value a + b x + c y at each cell centre.
Raster<float> Plane(std::size_t width, std::size_t height, double a, double b, double c) {
    Raster<float> raster(width, height);
    raster.SetTransform(GeoTransform::NorthUp(-10.0, 20.0, 2.0));
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t col = 0; col < width; ++col) {
            const auto p = raster.CellCenter(col, row);
            raster(col, row) = static_cast<float>(a + b * p.x + c * p.y);
        }
    }
    return raster;
}

}  // namespace

TEST(Resample, KeepsTheExtentAndDirectionWithTheNewCellSize) {
    const Raster<float> source = Plane(10, 6, 0.0, 1.0, 0.0);
    const Raster<float> fine = Resample(source, 0.5, ResampleMethod::Bilinear);
    EXPECT_EQ(fine.Width(), 40u);
    EXPECT_EQ(fine.Height(), 24u);
    EXPECT_EQ(fine.Transform(), GeoTransform::NorthUp(-10.0, 20.0, 0.5));
    const Raster<float> coarse = Resample(source, 3.0, ResampleMethod::Nearest);
    EXPECT_EQ(coarse.Width(), 7u);
    EXPECT_EQ(coarse.Height(), 4u);
    EXPECT_EQ(Resample(source, 1000.0, ResampleMethod::Cubic).Width(), 1u);
}

TEST(Resample, BilinearAndCubicReproduceLinearSurfacesAwayFromTheEdge) {
    const Raster<float> source = Plane(16, 12, 5.0, 0.3, -0.7);
    for (const ResampleMethod method : {ResampleMethod::Bilinear, ResampleMethod::Cubic}) {
        const Raster<float> fine = Resample(source, 0.5, method);
        for (std::size_t row = 8; row + 8 < fine.Height(); ++row) {
            for (std::size_t col = 8; col + 8 < fine.Width(); ++col) {
                const auto p = fine.CellCenter(col, row);
                EXPECT_NEAR(fine(col, row), 5.0 + 0.3 * p.x - 0.7 * p.y, 1e-4) << col << " " << row;
            }
        }
    }
}

TEST(Resample, CubicIsCloserThanBilinearOnASmoothCurve) {
    Raster<float> source(32, 32);
    source.SetTransform(GeoTransform::NorthUp(0.0, 32.0, 1.0));
    const auto curve = [](double x, double y) { return std::sin(0.3 * x) * std::cos(0.2 * y); };
    for (std::size_t row = 0; row < 32; ++row) {
        for (std::size_t col = 0; col < 32; ++col) {
            const auto p = source.CellCenter(col, row);
            source(col, row) = static_cast<float>(curve(p.x, p.y));
        }
    }
    double errors[2] = {0.0, 0.0};
    for (int m = 0; m < 2; ++m) {
        const Raster<float> fine =
            Resample(source, 0.25, m == 0 ? ResampleMethod::Bilinear : ResampleMethod::Cubic);
        for (std::size_t row = 16; row + 16 < fine.Height(); ++row) {
            for (std::size_t col = 16; col + 16 < fine.Width(); ++col) {
                const auto p = fine.CellCenter(col, row);
                errors[m] = std::fmax(
                    errors[m], std::fabs(static_cast<double>(fine(col, row)) - curve(p.x, p.y)));
            }
        }
    }
    EXPECT_LT(errors[1], 0.5 * errors[0]);
}

TEST(Resample, NearestCopiesCellsAndNoDataPropagates) {
    Raster<float> source = Plane(4, 4, 0.0, 1.0, 10.0);
    source.SetNoData(-9999.0f);
    source(1, 1) = -9999.0f;
    const Raster<float> nearest = Resample(source, 1.0, ResampleMethod::Nearest);
    EXPECT_EQ(nearest(0, 0), source(0, 0));
    EXPECT_EQ(nearest(7, 7), source(3, 3));
    EXPECT_EQ(nearest(2, 2), -9999.0f);
    const Raster<float> bilinear = Resample(source, 1.0, ResampleMethod::Bilinear);
    EXPECT_EQ(bilinear.NoData(), source.NoData());
    EXPECT_EQ(bilinear(2, 2), -9999.0f);
    EXPECT_EQ(bilinear(3, 3), -9999.0f);
    EXPECT_NE(bilinear(7, 7), -9999.0f);
    EXPECT_EQ(Resample(source, 2.0, ResampleMethod::Cubic)(1, 1), -9999.0f);
}

TEST(Resample, RejectsInvalidInputs) {
    const Raster<float> source = Plane(4, 4, 0.0, 1.0, 0.0);
    EXPECT_THROW(Resample(Raster<float>(), 1.0, ResampleMethod::Nearest), std::invalid_argument);
    EXPECT_THROW(Resample(source, 0.0, ResampleMethod::Nearest), std::invalid_argument);
    EXPECT_THROW(Resample(source, std::numeric_limits<double>::infinity(), ResampleMethod::Nearest),
                 std::invalid_argument);
    EXPECT_THROW(Resample(source, 1e-12, ResampleMethod::Nearest), std::invalid_argument);
    Raster<float> rotated = source;
    rotated.SetTransform(GeoTransform{0.0, 1.0, 0.1, 0.0, 0.0, -1.0});
    EXPECT_THROW(Resample(rotated, 1.0, ResampleMethod::Nearest), std::invalid_argument);
}

TEST(SampleRaster, InterpolatesAtWorldPositions) {
    Raster<float> raster(4, 3);
    raster.SetTransform(GeoTransform::NorthUp(10.0, 30.0, 2.0));
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            raster(col, row) = static_cast<float>(col * 10 + row);
        }
    }
    EXPECT_EQ(terraingen::SampleRaster(raster, 13.0, 27.0, ResampleMethod::Bilinear), 11.0f);
    EXPECT_FLOAT_EQ(terraingen::SampleRaster(raster, 14.0, 27.0, ResampleMethod::Bilinear), 16.0f);
    EXPECT_EQ(terraingen::SampleRaster(raster, 14.1, 26.9, ResampleMethod::Nearest), 21.0f);
    EXPECT_TRUE(std::isnan(terraingen::SampleRaster(raster, 9.0, 27.0, ResampleMethod::Cubic)));
    EXPECT_TRUE(std::isnan(terraingen::SampleRaster(raster, 13.0, 31.0, ResampleMethod::Cubic)));
    raster.SetNoData(-1.0f);
    raster(1, 1) = -1.0f;
    EXPECT_TRUE(std::isnan(terraingen::SampleRaster(raster, 13.0, 27.0, ResampleMethod::Nearest)));
    EXPECT_TRUE(std::isnan(terraingen::SampleRaster(raster, 14.0, 27.0, ResampleMethod::Bilinear)));
}

TEST(ResampleTo, SamplesEachCellCenterAndMarksCellsBeyondTheRaster) {
    Raster<float> raster(4, 4, 5.0f);
    raster.SetTransform(GeoTransform::NorthUp(0.0, 8.0, 2.0));
    const terraingen::RasterGrid grid{6, 6, GeoTransform::NorthUp(-2.0, 10.0, 2.0)};
    const Raster<float> resampled = terraingen::ResampleTo(raster, grid, ResampleMethod::Cubic);
    EXPECT_EQ(terraingen::RasterGrid::FromRaster(resampled), grid);
    EXPECT_FLOAT_EQ(resampled(2, 2), 5.0f);
    EXPECT_TRUE(resampled.IsNoData(resampled(0, 0)));
    EXPECT_TRUE(std::isnan(resampled(5, 3)));
    EXPECT_THROW(terraingen::ResampleTo(raster, terraingen::RasterGrid{}, ResampleMethod::Cubic),
                 std::invalid_argument);
    EXPECT_THROW(terraingen::ResampleTo(Raster<float>(), grid, ResampleMethod::Cubic),
                 std::invalid_argument);
    raster.SetTransform(GeoTransform{0.0, 2.0, 0.3, 8.0, 0.3, -2.0});
    EXPECT_THROW(terraingen::SampleRaster(raster, 1.0, 1.0, ResampleMethod::Cubic),
                 std::invalid_argument);
}

namespace {

/// A 30 x 20 raster of uneven heights on an awkward lattice, with a no-data hole.
Raster<float> Bumpy() {
    Raster<float> raster(30, 20);
    raster.SetTransform(GeoTransform::NorthUp(100.3, 200.7, 1.7));
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            raster(col, row) = static_cast<float>(std::sin(0.7 * static_cast<double>(col)) * 9.1 +
                                                  std::cos(0.37 * static_cast<double>(row * col)));
        }
    }
    raster.SetNoData(-9999.0f);
    raster(12, 9) = -9999.0f;
    return raster;
}

/// The cells of `raster` in `window`, placed where they lie.
Raster<float> Crop(const Raster<float>& raster, const terraingen::PixelWindow& window) {
    Raster<float> part =
        terraingen::Subgrid(terraingen::RasterGrid::FromRaster(raster), window).MakeRaster();
    part.SetNoData(raster.NoData());
    for (std::size_t row = 0; row < part.Height(); ++row) {
        for (std::size_t col = 0; col < part.Width(); ++col) {
            part(col, row) = raster(window.colBegin + col, window.rowBegin + row);
        }
    }
    return part;
}

/// Whether two floats have the same bits, NaNs included.
bool SameBits(float a, float b) {
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

}  // namespace

TEST(SampleRasterWindow, GivesTheWholesBitsWithinTheWindow) {
    const Raster<float> whole = Bumpy();
    for (const terraingen::PixelWindow& window :
         {terraingen::PixelWindow{8, 22, 5, 15}, terraingen::PixelWindow{0, 10, 0, 8},
          terraingen::PixelWindow{20, 30, 12, 20}}) {
        const Raster<float> part = Crop(whole, window);
        const terraingen::RasterPlacement placement{terraingen::RasterGrid::FromRaster(whole),
                                                    window.colBegin, window.rowBegin};
        const auto inner = [](std::size_t index, std::size_t edge, double inward) {
            return static_cast<double>(index) + (index == edge ? 0.0 : inward);
        };
        const double colBegin = inner(window.colBegin, 0, 3.0);
        const double colEnd = inner(window.colEnd, whole.Width(), -3.0);
        const double rowBegin = inner(window.rowBegin, 0, 3.0);
        const double rowEnd = inner(window.rowEnd, whole.Height(), -3.0);
        for (const ResampleMethod method :
             {ResampleMethod::Nearest, ResampleMethod::Bilinear, ResampleMethod::Cubic}) {
            for (double col = colBegin; col < colEnd; col += 0.37) {
                for (double row = rowBegin; row < rowEnd; row += 0.41) {
                    const auto p = whole.Transform().PixelToWorld(col, row);
                    ASSERT_TRUE(
                        SameBits(terraingen::SampleRasterWindow(part, placement, p.x, p.y, method),
                                 terraingen::SampleRaster(whole, p.x, p.y, method)))
                        << col << "," << row;
                }
            }
        }
        EXPECT_TRUE(std::isnan(
            terraingen::SampleRasterWindow(part, placement, 90.0, 190.0, ResampleMethod::Cubic)));
    }
}

TEST(ResampleWindow, GivesTheWholesCellsWithinTheWindow) {
    const Raster<float> whole = Bumpy();
    const terraingen::RasterGrid fine =
        terraingen::ResampledGrid(terraingen::RasterGrid::FromRaster(whole), 0.6);
    const terraingen::PixelWindow window{8, 22, 5, 15};
    const Raster<float> part = Crop(whole, window);
    const terraingen::RasterPlacement placement{terraingen::RasterGrid::FromRaster(whole),
                                                window.colBegin, window.rowBegin};
    const double scale = 1.7 / 0.6;
    const terraingen::PixelWindow cells{static_cast<std::size_t>(std::ceil(11 * scale)),
                                        static_cast<std::size_t>(std::floor(19 * scale)),
                                        static_cast<std::size_t>(std::ceil(8 * scale)),
                                        static_cast<std::size_t>(std::floor(12 * scale))};
    for (const ResampleMethod method :
         {ResampleMethod::Nearest, ResampleMethod::Bilinear, ResampleMethod::Cubic}) {
        const Raster<float> expected = Crop(Resample(whole, 0.6, method), cells);
        const Raster<float> resampled =
            terraingen::ResampleWindow(part, placement, 0.6, method, cells);
        EXPECT_EQ(terraingen::RasterGrid::FromRaster(resampled), terraingen::Subgrid(fine, cells));
        EXPECT_EQ(resampled.NoData(), whole.NoData());
        for (std::size_t i = 0; i < expected.Size(); ++i) {
            ASSERT_TRUE(SameBits(resampled.Values()[i], expected.Values()[i])) << i;
        }
    }
    const terraingen::PixelWindow corner{0, 10, 0, 8};
    const terraingen::PixelWindow cornerCells{0, static_cast<std::size_t>(std::floor(7 * scale)), 0,
                                              static_cast<std::size_t>(std::floor(5 * scale))};
    const Raster<float> expected = Crop(Resample(whole, 0.6, ResampleMethod::Cubic), cornerCells);
    const Raster<float> resampled = terraingen::ResampleWindow(
        Crop(whole, corner), {terraingen::RasterGrid::FromRaster(whole), 0, 0}, 0.6,
        ResampleMethod::Cubic, cornerCells);
    for (std::size_t i = 0; i < expected.Size(); ++i) {
        ASSERT_TRUE(SameBits(resampled.Values()[i], expected.Values()[i])) << i;
    }
    EXPECT_THROW(
        terraingen::ResampleWindow(part, placement, 0.6, ResampleMethod::Cubic, {3, 3, 0, 4}),
        std::invalid_argument);
    EXPECT_THROW(terraingen::ResampleWindow(part, placement, 0.6, ResampleMethod::Cubic,
                                            {0, fine.width + 1, 0, 4}),
                 std::invalid_argument);
    EXPECT_THROW(
        terraingen::ResampleWindow(Raster<float>(), placement, 0.6, ResampleMethod::Cubic, cells),
        std::invalid_argument);
}

TEST(FilledNoData, KeepsTheSourceValueOrMarksMissingCellsNan) {
    EXPECT_EQ(terraingen::FilledNoData(-5.0f, true), -5.0f);
    EXPECT_FALSE(terraingen::FilledNoData(std::nullopt, false).has_value());
    EXPECT_TRUE(std::isnan(*terraingen::FilledNoData(std::nullopt, true)));
}

TEST(SampleAtCenters, CallsTheSamplerAtEachCellCenter) {
    Raster<float> target(3, 2);
    target.SetTransform(GeoTransform::NorthUp(0.0, 2.0, 1.0));
    terraingen::SampleAtCenters(target, std::nullopt, [](double x, double y) {
        return x > 2.0 ? std::numeric_limits<float>::quiet_NaN() : static_cast<float>(x + 10 * y);
    });
    EXPECT_FLOAT_EQ(target(0, 0), 15.5f);
    EXPECT_FLOAT_EQ(target(1, 1), 6.5f);
    EXPECT_TRUE(std::isnan(target(2, 0)));
    EXPECT_TRUE(target.NoData().has_value());
    terraingen::SampleAtCenters(target, -1.0f, [](double, double) { return 1.0f; });
    EXPECT_EQ(target(2, 0), 1.0f);
    EXPECT_EQ(target.NoData(), -1.0f);
}

namespace {

/// Every kernel method.
constexpr ResampleMethod kKernels[] = {ResampleMethod::Bilinear, ResampleMethod::Cubic,
                                       ResampleMethod::CatmullRom, ResampleMethod::Lanczos3,
                                       ResampleMethod::CubicBSpline};

/// `values` on a lattice of whole positions interpolated at (u, v) with `method`.
float InterpolateAt(const std::function<float(double, double)>& values, ResampleMethod method,
                    double u, double v) {
    return *terraingen::InterpolateLattice(u, v, method, values,
                                           [](float value) { return std::isnan(value); });
}

}  // namespace

TEST(Kernels, HaveTheirRadiiAndWeightsThatSumToOne) {
    EXPECT_EQ(terraingen::KernelRadius(ResampleMethod::Nearest), 1);
    EXPECT_EQ(terraingen::KernelRadius(ResampleMethod::Bilinear), 1);
    EXPECT_EQ(terraingen::KernelRadius(ResampleMethod::Cubic), 2);
    EXPECT_EQ(terraingen::KernelRadius(ResampleMethod::CatmullRom), 2);
    EXPECT_EQ(terraingen::KernelRadius(ResampleMethod::Lanczos3), 3);
    EXPECT_EQ(terraingen::KernelRadius(ResampleMethod::CubicBSpline), 2);
    for (const ResampleMethod method : kKernels) {
        for (const double fraction : {0.0, 0.1, 0.25, 0.5, 0.77, 0.999}) {
            const std::array<double, 6> weights = terraingen::KernelWeights(method, fraction);
            double total = 0.0;
            for (const double weight : weights) {
                total += weight;
            }
            EXPECT_NEAR(total, 1.0, 1e-12) << static_cast<int>(method) << " " << fraction;
        }
    }
    EXPECT_EQ(terraingen::KernelWeight(ResampleMethod::Lanczos3, 0.0), 1.0);
    EXPECT_NEAR(terraingen::KernelWeight(ResampleMethod::Lanczos3, 1.0), 0.0, 1e-15);
    EXPECT_NEAR(terraingen::KernelWeight(ResampleMethod::Lanczos3, 0.5),
                std::sin(terraingen::kPi / 2.0) / (terraingen::kPi / 2.0) *
                    std::sin(terraingen::kPi / 6.0) / (terraingen::kPi / 6.0),
                1e-15);
    EXPECT_EQ(terraingen::KernelWeight(ResampleMethod::Lanczos3, 3.0), 0.0);
    EXPECT_DOUBLE_EQ(terraingen::KernelWeight(ResampleMethod::CubicBSpline, 0.0), 2.0 / 3.0);
    EXPECT_DOUBLE_EQ(terraingen::KernelWeight(ResampleMethod::CubicBSpline, 1.0), 1.0 / 6.0);
    EXPECT_EQ(terraingen::KernelWeight(ResampleMethod::CubicBSpline, 2.0), 0.0);
    EXPECT_EQ(terraingen::KernelWeight(ResampleMethod::Bilinear, 0.25), 0.75);
    for (const double x : {0.0, 0.3, 1.0, 1.6, 2.0}) {
        EXPECT_EQ(terraingen::KernelWeight(ResampleMethod::CatmullRom, x),
                  terraingen::KernelWeight(ResampleMethod::Cubic, x))
            << "Catmull-Rom is Keys cubic convolution with a = -0.5";
    }
}

TEST(Kernels, ReproduceConstantsAndLinearRamps) {
    const auto constant = [](double, double) { return 7.25f; };
    const auto ramp = [](double u, double v) {
        return static_cast<float>(3.0 + 0.5 * u - 2.0 * v);
    };
    for (const ResampleMethod method : kKernels) {
        for (const double u : {-2.0, 0.3, 1.5, 4.71}) {
            for (const double v : {-1.0, 0.65, 2.2}) {
                EXPECT_NEAR(InterpolateAt(constant, method, u, v), 7.25f, 1e-5)
                    << static_cast<int>(method);
                const double tolerance = method == ResampleMethod::Lanczos3 ? 0.06 : 1e-5;
                EXPECT_NEAR(InterpolateAt(ramp, method, u, v), 3.0 + 0.5 * u - 2.0 * v, tolerance)
                    << static_cast<int>(method) << " " << u << "," << v;
            }
        }
    }
}

TEST(Kernels, InterpolatingKernelsPassThroughThePostsAndTheBSplineSmooths) {
    const auto spike = [](double u, double v) { return u == 0.0 && v == 0.0 ? 6.0f : 0.0f; };
    for (const ResampleMethod method : {ResampleMethod::Bilinear, ResampleMethod::Cubic,
                                        ResampleMethod::CatmullRom, ResampleMethod::Lanczos3}) {
        EXPECT_EQ(InterpolateAt(spike, method, 0.0, 0.0), 6.0f) << static_cast<int>(method);
        EXPECT_EQ(InterpolateAt(spike, method, 1.0, 0.0), 0.0f) << static_cast<int>(method);
        EXPECT_EQ(InterpolateAt(spike, method, -2.0, 1.0), 0.0f) << static_cast<int>(method);
    }
    EXPECT_NEAR(InterpolateAt(spike, ResampleMethod::CubicBSpline, 0.0, 0.0),
                6.0 * (2.0 / 3.0) * (2.0 / 3.0), 1e-6);
    EXPECT_NEAR(InterpolateAt(spike, ResampleMethod::CubicBSpline, 1.0, 0.0),
                6.0 * (1.0 / 6.0) * (2.0 / 3.0), 1e-6);
    const auto wave = [](double u, double) {
        return static_cast<float>(u == std::floor(u) && static_cast<long long>(u) % 2 == 0 ? 1.0
                                                                                           : -1.0);
    };
    EXPECT_NEAR(InterpolateAt(wave, ResampleMethod::CubicBSpline, 0.0, 0.0), 1.0 / 3.0, 1e-6)
        << "the B-spline damps the shortest wavelength to a third";
    EXPECT_EQ(InterpolateAt(wave, ResampleMethod::CatmullRom, 0.0, 0.0), 1.0f);
}

TEST(SampleRaster, InterpolatesWithEveryKernel) {
    const Raster<float> plane = Plane(12, 10, 4.0, 0.25, -0.5);
    for (const ResampleMethod method : kKernels) {
        const float value = terraingen::SampleRaster(plane, 3.3, 9.1, method);
        const double tolerance = method == ResampleMethod::Lanczos3 ? 0.05 : 1e-4;
        EXPECT_NEAR(value, 4.0 + 0.25 * 3.3 - 0.5 * 9.1, tolerance) << static_cast<int>(method);
    }
}
