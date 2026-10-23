#include "terraingen/raster/resample.h"

#include "support/resample.h"

#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::ResampleMethod;
using terraingen::testing::Resample;

namespace {

/// A `width` x `height` raster of 2 m cells with value a + b x + c y at each cell center.
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

TEST(SampleRaster, RejectsEmptyAndRotatedRasters) {
    Raster<float> raster(4, 4, 5.0f);
    EXPECT_THROW(terraingen::SampleRaster(Raster<float>(), 1.0, 1.0, ResampleMethod::Cubic),
                 std::invalid_argument);
    raster.SetTransform(GeoTransform{0.0, 2.0, 0.3, 8.0, 0.3, -2.0});
    EXPECT_THROW(terraingen::SampleRaster(raster, 1.0, 1.0, ResampleMethod::Cubic),
                 std::invalid_argument);
}

TEST(FilledNoData, KeepsTheSourceValueOrMarksMissingCellsNan) {
    EXPECT_EQ(terraingen::FilledNoData(-5.0f, true), -5.0f);
    EXPECT_FALSE(terraingen::FilledNoData(std::nullopt, false).has_value());
    EXPECT_TRUE(std::isnan(*terraingen::FilledNoData(std::nullopt, true)));
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
    EXPECT_EQ(terraingen::detail::KernelWeight(ResampleMethod::Lanczos3, 0.0), 1.0);
    EXPECT_NEAR(terraingen::detail::KernelWeight(ResampleMethod::Lanczos3, 1.0), 0.0, 1e-15);
    EXPECT_NEAR(terraingen::detail::KernelWeight(ResampleMethod::Lanczos3, 0.5),
                std::sin(terraingen::kPi / 2.0) / (terraingen::kPi / 2.0) *
                    std::sin(terraingen::kPi / 6.0) / (terraingen::kPi / 6.0),
                1e-15);
    EXPECT_EQ(terraingen::detail::KernelWeight(ResampleMethod::Lanczos3, 3.0), 0.0);
    EXPECT_DOUBLE_EQ(terraingen::detail::KernelWeight(ResampleMethod::CubicBSpline, 0.0),
                     2.0 / 3.0);
    EXPECT_DOUBLE_EQ(terraingen::detail::KernelWeight(ResampleMethod::CubicBSpline, 1.0),
                     1.0 / 6.0);
    EXPECT_EQ(terraingen::detail::KernelWeight(ResampleMethod::CubicBSpline, 2.0), 0.0);
    EXPECT_EQ(terraingen::KernelWeights(ResampleMethod::Bilinear, 0.25)[0], 0.75);
    for (const double x : {0.0, 0.3, 1.0, 1.6, 2.0}) {
        EXPECT_EQ(terraingen::detail::KernelWeight(ResampleMethod::CatmullRom, x),
                  terraingen::detail::KernelWeight(ResampleMethod::Cubic, x))
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
