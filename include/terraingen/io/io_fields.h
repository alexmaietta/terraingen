// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The names of list formats.
#pragma once

#include "terraingen/io/feature_lists.h"
#include "terraingen/util/option_fields.h"

#include <array>

namespace terraingen {

template <> struct EnumNames<ListFormat> {
    static constexpr std::array<EnumEntry<ListFormat>, 2> kValues{{
        {"csv", ListFormat::Csv, "Comma-separated values, one row per item."},
        {"json", ListFormat::Json, "A JSON document."},
    }};
};

}  // namespace terraingen
