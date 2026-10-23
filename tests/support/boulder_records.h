// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `Records` and `List`, between a `BoulderList` and a vector of `Boulder` records, for
/// tests.
#pragma once

#include "terraingen/layers/boulders/boulder.h"

#include <cstddef>
#include <vector>

namespace terraingen::testing {

/// Every boulder of `list`, in order.
inline std::vector<Boulder> Records(const BoulderList& list) {
    std::vector<Boulder> records;
    records.reserve(list.Size());
    for (std::size_t i = 0; i < list.Size(); ++i) {
        records.push_back(list.At(i));
    }
    return records;
}

/// A `BoulderList` of `records`, in order.
inline BoulderList List(const std::vector<Boulder>& records) {
    BoulderList list;
    for (const Boulder& boulder : records) {
        list.Append(boulder);
    }
    return list;
}

}  // namespace terraingen::testing
