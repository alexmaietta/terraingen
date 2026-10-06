#include "bindings.h"

#include "terraingen/noise/kernels/philox.h"
#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"

#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/vector.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

namespace terraingen::python {

namespace {

template <typename T> nb::ndarray<nb::numpy, T> NewArray(std::size_t count, T*& data) {
    data = new T[count];
    nb::capsule owner(data, [](void* p) noexcept { delete[] static_cast<T*>(p); });
    const std::size_t shape[1] = {count};
    return nb::ndarray<nb::numpy, T>(data, 1, shape, owner);
}

template <typename T, typename Draw>
nb::object Sample(std::optional<std::size_t> count, Draw draw) {
    if (!count) {
        return nb::cast(draw());
    }
    T* data = nullptr;
    auto array = NewArray<T>(*count, data);
    for (std::size_t i = 0; i < *count; ++i) {
        data[i] = static_cast<T>(draw());
    }
    return nb::cast(array);
}

}  // namespace

void BindRandom(nb::module_& m) {
    m.def(
        "philox4x32",
        [](std::uint32_t c0, std::uint32_t c1, std::uint32_t c2, std::uint32_t c3, std::uint32_t k0,
           std::uint32_t k1) {
            const U32x4 out = Philox4x32(U32x4{c0, c1, c2, c3}, U32x2{k0, k1});
            return nb::make_tuple(out.x, out.y, out.z, out.w);
        },
        "c0"_a, "c1"_a, "c2"_a, "c3"_a, "k0"_a, "k1"_a,
        "Philox4x32-10 (Salmon et al. 2011) of a 128-bit counter and 64-bit key, as four words.");

    nb::class_<RandomStream>(m, "RandomStream",
                             "Counter-based random stream on Philox4x32-10. The key is (seed, "
                             "stream); a, b and c address an independent substream, such as a "
                             "cell's x and y and a size band.")
        .def(nb::init<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>(),
             "seed"_a, "stream"_a, "a"_a = 0u, "b"_a = 0u, "c"_a = 0u)
        .def("next_u32", &RandomStream::NextU32)
        .def("next_float", &RandomStream::NextFloat, "Uniform float32 in [0, 1).")
        .def("next_double", &RandomStream::NextDouble, "Uniform float64 in [0, 1).")
        .def("next_double_open_below", &RandomStream::NextDoubleOpenBelow,
             "Uniform float64 in (0, 1].")
        .def(
            "u32s",
            [](RandomStream& stream, std::size_t count) {
                std::uint32_t* data = nullptr;
                auto array = NewArray<std::uint32_t>(count, data);
                for (std::size_t i = 0; i < count; ++i) {
                    data[i] = stream.NextU32();
                }
                return array;
            },
            "count"_a, "The next `count` 32-bit words as a NumPy array.")
        .def(
            "doubles",
            [](RandomStream& stream, std::size_t count) {
                double* data = nullptr;
                auto array = NewArray<double>(count, data);
                for (std::size_t i = 0; i < count; ++i) {
                    data[i] = stream.NextDouble();
                }
                return array;
            },
            "count"_a, "The next `count` uniform float64 values in [0, 1) as a NumPy array.");

    m.def(
        "sample_poisson",
        [](RandomStream& stream, double mean, std::optional<std::size_t> count) {
            return Sample<std::uint64_t>(count, [&] { return SamplePoisson(stream, mean); });
        },
        "stream"_a, "mean"_a, "count"_a = nb::none(),
        "Poisson count (multiplication method below 10, Hormann's PTRS above); an array if "
        "`count` is given.");
    m.def(
        "sample_truncated_power_law",
        [](RandomStream& stream, double min, double max, double exponent,
           std::optional<std::size_t> count) {
            return Sample<double>(
                count, [&] { return SampleTruncatedPowerLaw(stream, min, max, exponent); });
        },
        "stream"_a, "min"_a, "max"_a, "exponent"_a, "count"_a = nb::none(),
        "Power law on [min, max] with P(X > x) proportional to x**-exponent; max may be inf.");
    m.def(
        "sample_exponential",
        [](RandomStream& stream, double rate, std::optional<std::size_t> count) {
            return Sample<double>(count, [&] { return SampleExponential(stream, rate); });
        },
        "stream"_a, "rate"_a, "count"_a = nb::none(), "Exponential sample with the given rate.");
    m.def(
        "sample_normal",
        [](RandomStream& stream, double mean, double sigma, std::optional<std::size_t> count) {
            return Sample<double>(count, [&] { return SampleNormal(stream, mean, sigma); });
        },
        "stream"_a, "mean"_a, "sigma"_a, "count"_a = nb::none(),
        "Normal sample via the Box-Muller transform.");

    nb::class_<TabulatedDistribution>(m, "TabulatedDistribution",
                                      "Distribution given as a piecewise-linear CDF, sampled by "
                                      "inversion.")
        .def(nb::init<std::vector<double>, std::vector<double>>(), "values"_a, "cumulative"_a)
        .def("cdf", &TabulatedDistribution::Cdf, "x"_a)
        .def(
            "sample",
            [](const TabulatedDistribution& table, RandomStream& stream,
               std::optional<std::size_t> count) {
                return Sample<double>(count, [&] { return table.Sample(stream); });
            },
            "stream"_a, "count"_a = nb::none());
}

}  // namespace terraingen::python
