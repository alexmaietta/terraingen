#include "terraingen/util/overloaded.h"

#include <gtest/gtest.h>

#include <string>
#include <variant>

TEST(Overloaded, CallsTheLambdaForEachAlternative) {
    const auto describe = [](const std::variant<int, std::string>& value) {
        return std::visit(
            terraingen::Overloaded{[](int number) { return "int " + std::to_string(number); },
                                   [](const std::string& text) { return "string " + text; }},
            value);
    };
    EXPECT_EQ(describe(3), "int 3");
    EXPECT_EQ(describe(std::string("moon")), "string moon");
}
