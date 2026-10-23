// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/util/span.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

using terraingen::Span;

TEST(Span, ViewsVectorsArraysAndPointers) {
    std::vector<int> vector{1, 2, 3};
    const Span<int> fromVector = vector;
    EXPECT_EQ(fromVector.data(), vector.data());
    EXPECT_EQ(fromVector.size(), 3u);
    fromVector[1] = 20;
    EXPECT_EQ(vector[1], 20);

    std::array<double, 4> array{0.5, 1.5, 2.5, 3.5};
    const Span<double> fromArray = array;
    EXPECT_EQ(fromArray.data(), array.data());
    EXPECT_EQ(fromArray.size(), 4u);
    EXPECT_EQ(fromArray.size_bytes(), 4u * sizeof(double));

    float raw[5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    const Span<float> fromRaw = raw;
    EXPECT_EQ(fromRaw.data(), raw);
    EXPECT_EQ(fromRaw.size(), 5u);
    EXPECT_EQ(fromRaw.front(), 1.0f);
    EXPECT_EQ(fromRaw.back(), 5.0f);

    const Span<float> fromPointer(raw + 1, 3);
    EXPECT_EQ(fromPointer.size(), 3u);
    EXPECT_EQ(fromPointer[0], 2.0f);
    const Span<float> fromRange(raw + 1, raw + 4);
    EXPECT_EQ(fromRange.data(), fromPointer.data());
    EXPECT_EQ(fromRange.size(), fromPointer.size());

    const Span<int> empty;
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty.data(), nullptr);
    EXPECT_EQ(empty.begin(), empty.end());
}

TEST(Span, ConvertsToConstButNotBack) {
    std::vector<int> vector{4, 5, 6};
    const Span<int> mutableView = vector;
    const Span<const int> constView = mutableView;
    EXPECT_EQ(constView.data(), vector.data());
    EXPECT_EQ(constView.size(), 3u);
    const std::vector<int>& readOnly = vector;
    const Span<const int> fromConstVector = readOnly;
    EXPECT_EQ(fromConstVector.size(), 3u);
    EXPECT_TRUE((std::is_convertible_v<Span<int>, Span<const int>>));
    EXPECT_FALSE((std::is_convertible_v<Span<const int>, Span<int>>));
    EXPECT_FALSE((std::is_convertible_v<const std::vector<int>&, Span<int>>));
    EXPECT_FALSE((std::is_convertible_v<std::vector<double>&, Span<int>>));
}

TEST(Span, DeducesItsElementType) {
    std::vector<int> vector{1, 2};
    const std::vector<int>& readOnly = vector;
    std::array<float, 2> array{1.0f, 2.0f};
    double raw[3] = {1.0, 2.0, 3.0};
    const std::uint32_t value = 7u;
    EXPECT_TRUE((std::is_same_v<decltype(Span(vector)), Span<int>>));
    EXPECT_TRUE((std::is_same_v<decltype(Span(readOnly)), Span<const int>>));
    EXPECT_TRUE((std::is_same_v<decltype(Span(array)), Span<float>>));
    EXPECT_TRUE((std::is_same_v<decltype(Span(raw)), Span<double>>));
    EXPECT_TRUE((std::is_same_v<decltype(Span(&value, 1)), Span<const std::uint32_t>>));
    EXPECT_EQ(Span(raw).size(), 3u);
}

TEST(Span, TakesFirstLastAndSubspans) {
    std::vector<int> vector{0, 1, 2, 3, 4, 5};
    const Span<int> all = vector;
    const auto first = all.first(2);
    EXPECT_EQ(first.data(), vector.data());
    EXPECT_EQ(first.size(), 2u);
    const auto last = all.last(3);
    EXPECT_EQ(last.data(), vector.data() + 3);
    EXPECT_EQ(last.size(), 3u);
    const auto middle = all.subspan(1, 4);
    EXPECT_EQ(middle.front(), 1);
    EXPECT_EQ(middle.back(), 4);
    const auto rest = all.subspan(4);
    EXPECT_EQ(rest.size(), 2u);
    EXPECT_EQ(rest[0], 4);
    EXPECT_TRUE(all.subspan(6).empty());
    EXPECT_TRUE(all.first(0).empty());
    int sum = 0;
    for (const int value : all.subspan(2, 3)) {
        sum += value;
    }
    EXPECT_EQ(sum, 2 + 3 + 4);
}

TEST(Span, ViewsTheBytesOfItsElements) {
    std::vector<std::uint32_t> words{1u, 2u, 3u};
    const auto bytes = terraingen::AsBytes(Span(words));
    EXPECT_TRUE((std::is_same_v<decltype(bytes), const Span<const std::byte>>));
    EXPECT_EQ(bytes.size(), 3u * sizeof(std::uint32_t));
    EXPECT_EQ(static_cast<const void*>(bytes.data()), static_cast<const void*>(words.data()));
    const auto writable = terraingen::AsWritableBytes(Span(words));
    EXPECT_TRUE((std::is_same_v<decltype(writable), const Span<std::byte>>));
    EXPECT_EQ(writable.size(), bytes.size());
    for (std::byte& byte : writable) {
        byte = std::byte{0};
    }
    EXPECT_EQ(words[0], 0u);
    EXPECT_EQ(words[2], 0u);
    const std::vector<double> doubles(5, 1.0);
    EXPECT_EQ(terraingen::AsBytes(Span(doubles)).size(), 5u * sizeof(double));
}
