// SPDX-License-Identifier: BSD-3-Clause

#include "arrays.h"
#include "bindings.h"
#include "common.h"
#include "options.h"

#include "terraingen/random/philox.h"
#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"
#include "terraingen/random/value_distribution.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace terraingen::python {

namespace {

/// @brief `count` values of `draw()` as a NumPy array.
template <typename T, typename Draw> NumpyArray<T> DrawArray(std::size_t count, Draw draw) {
    std::vector<T> values(count);
    for (T& value : values) {
        value = static_cast<T>(draw());
    }
    return OwnedArray(std::move(values), {count});
}

/// @brief One value of `draw()`, or an array of `count` of them.
template <typename T, typename Draw>
nb::object Sample(std::optional<std::size_t> count, Draw draw) {
    if (!count) {
        return nb::cast(draw());
    }
    return nb::cast(DrawArray<T>(*count, draw));
}

void BindStream(nb::module_& m) {
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
        .def("next_double", &RandomStream::NextDouble, "Uniform float64 in [0, 1).")
        .def(
            "u32s",
            [](RandomStream& s, std::size_t count) {
                return DrawArray<std::uint32_t>(count, [&] { return s.NextU32(); });
            },
            "count"_a, "The next `count` 32-bit words as a NumPy array.")
        .def(
            "doubles",
            [](RandomStream& s, std::size_t count) {
                return DrawArray<double>(count, [&] { return s.NextDouble(); });
            },
            "count"_a, "The next `count` uniform float64 values in [0, 1) as a NumPy array.");
}

void BindSamplers(nb::module_& m) {
    m.def(
        "sample_poisson",
        [](RandomStream& stream, double mean, std::optional<std::size_t> count) {
            return Sample<std::uint64_t>(count, [&] { return SamplePoisson(stream, mean); });
        },
        "random"_a, "mean"_a, "count"_a = nb::none(),
        "Poisson count (multiplication method below 10, Hormann's PTRS above); an array if "
        "`count` is given.");
}

void BindValueDistribution(nb::module_& m) {
    const nb::class_<ValueDistribution> distribution = BindOptions<ValueDistribution>(
        m, "ValueDistribution",
        "A random value: constant `a`, uniform from `a` to `b`, normal or log-normal with mean "
        "`a` and deviation `b`, or piecewise: uniform within bins between `edges`, each drawn "
        "in proportion to its entry in `weights`; clipped to [min, max].",
        {&ValidateValueDistribution, "validate_value_distribution", "distribution"});
    BindEnum<ValueDistribution::Kind>(distribution, "Kind",
                                      "The kind of distribution a random value is drawn from.");
}

}  // namespace

void BindRandom(nb::module_& m) {
    BindStream(m);
    BindSamplers(m);
    BindValueDistribution(m);
}

}  // namespace terraingen::python
