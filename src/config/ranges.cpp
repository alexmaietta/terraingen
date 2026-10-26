// SPDX-License-Identifier: BSD-3-Clause

#include "config/fields/ranges.h"
#include "config/build_steps.h"

#include "config/fields/fields.h"
#include "terraingen/config/validate.h"
#include "util/text.h"

#include <cctype>
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

/// `count` items, in words: "1 item", "3 items".
std::string Items(std::size_t count) {
    return std::to_string(count) + (count == 1 ? " item" : " items");
}

/// `text` as a sentence: its first letter capitalized and a period after it.
std::string Sentence(std::string text) {
    if (!text.empty()) {
        text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    }
    return text + ".";
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

std::string RangeRule(const FieldRange& range, std::string_view unit) {
    if (!range.Bounded()) {
        return {};
    }
    const std::string allowed = DescribeRange(range, unit);
    return allowed == "not negative" ? "must not be negative" : "must be " + allowed;
}

std::string DescribeLength(const FieldRange& range) {
    if (!range.Counted()) {
        return {};
    }
    if (!range.maxLength) {
        return "at least " + Items(*range.minLength);
    }
    const std::size_t fewest = range.minLength.value_or(0);
    if (fewest == *range.maxLength) {
        return "exactly " + Items(fewest);
    }
    return std::to_string(fewest) + (range.lengthAtBounds ? " or " : " to ") +
           Items(*range.maxLength);
}

std::string FieldHelp(std::string_view description, std::string_view unit, const FieldRange& range,
                      bool list) {
    std::string help(description);
    const auto add = [&](const std::string& sentence) {
        help += (help.empty() ? "" : " ") + sentence;
    };
    if (range.Counted()) {
        add("Holds " + DescribeLength(range) + ".");
    }
    if (range.Bounded()) {
        add(Sentence((list ? "each " : "") + RangeRule(range, unit)));
    }
    if (range.required) {
        add("Required.");
    }
    return help;
}

void CheckRange(double value, const FieldRange& range, std::string_view unit,
                const std::string& path) {
    if (!range.Bounded()) {
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
    throw ConfigError(path, RangeRule(range, unit) + ", got " + Bound(value));
}

void CheckLength(std::size_t count, const FieldRange& range, const std::string& path) {
    const std::size_t fewest = range.minLength.value_or(0);
    const bool fits = range.lengthAtBounds
                          ? count == fewest || count == range.maxLength
                          : count >= fewest && (!range.maxLength || count <= *range.maxLength);
    if (!fits) {
        throw ConfigError(path,
                          "must hold " + DescribeLength(range) + ", got " + std::to_string(count));
    }
}

void CheckConfigRanges(const ConfigSpec& config) {
    ConfigSpec copy = config;
    RangeChecker checker("");
    Fields(checker, copy);
}

}  // namespace terraingen::config
