// SPDX-License-Identifier: BSD-3-Clause

#include "config/fields/ranges.h"

#include "config/fields/fields.h"
#include "terraingen/config/validate.h"
#include "util/text.h"

#include <cmath>

namespace terraingen::config {

namespace {

/// A bound as messages give it, such as 4096 or 0.5.
std::string Bound(double value) {
    return FormatNumber(value, 6);
}

/// What a range with a lower bound only allows.
std::string DescribeMinimum(double minimum, bool exclusive) {
    if (minimum == 0.0) {
        return exclusive ? "positive" : "not negative";
    }
    return (exclusive ? "above " : "at least ") + Bound(minimum);
}

/// What a range with both bounds allows.
std::string DescribeInterval(const FieldRange& range) {
    const std::string low = Bound(*range.minimum);
    const std::string high = Bound(*range.maximum);
    if (!range.exclusiveMinimum) {
        return "from " + low + (range.exclusiveMaximum ? " to below " : " to ") + high;
    }
    return "above " + low + (range.exclusiveMaximum ? " and below " : " and at most ") + high;
}

}  // namespace

std::string DescribeRange(const FieldRange& range, std::string_view unit) {
    std::string text;
    if (range.minimum && !range.maximum) {
        text = DescribeMinimum(*range.minimum, range.exclusiveMinimum);
        if (*range.minimum == 0.0) {
            return text;
        }
    } else if (range.minimum) {
        text = DescribeInterval(range);
    } else if (range.maximum) {
        text = (range.exclusiveMaximum ? "below " : "at most ") + Bound(*range.maximum);
    } else {
        return "any number";
    }
    return unit.empty() ? text : text + " " + std::string(unit);
}

void CheckRange(double value, const FieldRange& range, std::string_view unit,
                const std::string& path) {
    if (!range.minimum && !range.maximum) {
        CheckFinite(value, path);
        return;
    }
    const bool aboveMinimum = !range.minimum || (range.exclusiveMinimum ? value > *range.minimum
                                                                        : value >= *range.minimum);
    const bool belowMaximum = !range.maximum || (range.exclusiveMaximum ? value < *range.maximum
                                                                        : value <= *range.maximum);
    if (std::isfinite(value) && aboveMinimum && belowMaximum) {
        return;
    }
    std::string allowed = DescribeRange(range, unit);
    if (allowed == "not negative") {
        allowed = "not be negative";
    } else {
        allowed = "be " + allowed;
    }
    throw ConfigError(path, "must " + allowed + ", got " + Bound(value));
}

void CheckConfigRanges(const TerrainGenConfig& config) {
    TerrainGenConfig copy = config;
    RangeChecker checker("");
    Fields(checker, copy);
}

}  // namespace terraingen::config
