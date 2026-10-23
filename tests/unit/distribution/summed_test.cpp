// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/summed.h"

#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/tabulated.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

using terraingen::PowerLawSfd;
using terraingen::SummedSfd;

TEST(SummedSfd, AddsWeightedTermsOverTheirCommonRange) {
    const auto steep = std::make_shared<PowerLawSfd>(0.01, 3.0);
    const auto table = std::make_shared<terraingen::TabulatedSfd>(std::vector<double>{1.0, 10.0},
                                                                  std::vector<double>{0.1, 0.001});
    const SummedSfd sum({{2.0, steep}, {1.0, table}});
    EXPECT_DOUBLE_EQ(sum.Cumulative(2.0), 2.0 * steep->Cumulative(2.0) + table->Cumulative(2.0));
    EXPECT_DOUBLE_EQ(sum.Incremental(2.0), 2.0 * steep->Incremental(2.0) + table->Incremental(2.0));
    EXPECT_EQ(sum.ValidRange().min, 1.0);
    EXPECT_EQ(sum.ValidRange().max, 10.0);
    EXPECT_THROW(sum.Cumulative(0.5), std::domain_error);
    EXPECT_EQ(sum.Terms().size(), 2u);

    table->SetAllowExtrapolation(true);
    EXPECT_EQ(SummedSfd({{1.0, steep}, {1.0, table}}).ValidRange().min, 0.0)
        << "an extrapolating term is valid everywhere";
}

TEST(SummedSfd, RejectsMalformedTerms) {
    const auto steep = std::make_shared<PowerLawSfd>(0.01, 3.0);
    EXPECT_THROW(SummedSfd({}), std::invalid_argument);
    EXPECT_THROW(SummedSfd({{1.0, nullptr}}), std::invalid_argument);
    EXPECT_THROW(SummedSfd({{0.0, steep}}), std::invalid_argument);
    EXPECT_THROW(SummedSfd({{1.0, steep},
                            {1.0, std::make_shared<PowerLawSfd>(
                                      0.01, 3.0, terraingen::SfdKind::ProductionRate)}}),
                 std::invalid_argument);
    const auto low = std::make_shared<PowerLawSfd>(0.01, 3.0, terraingen::SfdKind::Density,
                                                   terraingen::DiameterRange{0.0, 1.0});
    const auto high = std::make_shared<PowerLawSfd>(0.01, 3.0, terraingen::SfdKind::Density,
                                                    terraingen::DiameterRange{2.0, 5.0});
    EXPECT_THROW(SummedSfd({{1.0, low}, {1.0, high}}), std::invalid_argument);
}
