#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

using terraingen::BoulderLayerOptions;

TEST(ValidateBoulderOptions, ChecksEachGroupOnItsOwn) {
    EXPECT_NO_THROW(terraingen::ValidateBoulderSizes({}));
    EXPECT_THROW(terraingen::ValidateBoulderSizes({.minDiameter = 0.0}), std::invalid_argument);
    EXPECT_THROW(terraingen::ValidateBoulderSizes({.coverage = 1.0}), std::invalid_argument);
    terraingen::BoulderSizes rate;
    rate.distribution =
        std::make_shared<terraingen::PowerLawSfd>(1.0, 2.0, terraingen::SfdKind::ProductionRate);
    EXPECT_THROW(terraingen::ValidateBoulderSizes(rate), std::invalid_argument);

    EXPECT_NO_THROW(terraingen::ValidateBoulderPatches({}));
    EXPECT_THROW(terraingen::ValidateBoulderPatches({.spread = 0.5}), std::invalid_argument);
    EXPECT_THROW(terraingen::ValidateBoulderPatches({.size = 0.0}), std::invalid_argument);

    EXPECT_NO_THROW(terraingen::ValidateBoulderEjecta({}, {}));
    EXPECT_THROW(terraingen::ValidateBoulderEjecta({.minCraterDiameter = 10.0, .reach = 1.0}, {}),
                 std::invalid_argument);

    EXPECT_NO_THROW(terraingen::ValidateBoulderBedrock({}));
    EXPECT_THROW(terraingen::ValidateBoulderBedrock({.abundance = 0.0}), std::invalid_argument);
    EXPECT_THROW(terraingen::ValidateBoulderBedrock({.exposureSpacing = -1.0}),
                 std::invalid_argument);

    EXPECT_NO_THROW(terraingen::ValidateBoulderPose({}));
    EXPECT_THROW(terraingen::ValidateBoulderPose({.minHeightRatio = 0.0}), std::invalid_argument);
    EXPECT_THROW(terraingen::ValidateBoulderPose({.minBurial = 0.5, .maxBurial = 0.4}),
                 std::invalid_argument);

    EXPECT_NO_THROW(terraingen::ValidateBoulderRendering({}));
    EXPECT_THROW(terraingen::ValidateBoulderRendering({.models = {.minSamples = 0.0}}),
                 std::invalid_argument);
}

TEST(BoulderCandidates, CoverTheRichestBackgroundPlusCraterBlocks) {
    BoulderLayerOptions options;
    options.patches.spread = 2.0;
    EXPECT_EQ(terraingen::MaxBackgroundAbundance(options), 2.0);
    options.bedrock.exposure = terraingen::ExposureParams{};
    options.bedrock.abundance = 3.0;
    EXPECT_EQ(terraingen::MaxBackgroundAbundance(options), 6.0);
    options.bedrock.abundance = 0.5;
    EXPECT_EQ(terraingen::MaxBackgroundAbundance(options), 2.0) << "bedrock never thins candidates";

    const BoulderLayerOptions plain;
    EXPECT_DOUBLE_EQ(terraingen::BoulderCandidates(plain)->Incremental(1.0),
                     terraingen::GolombekRappSfd(0.02).Incremental(1.0));
    BoulderLayerOptions ejecta;
    ejecta.ejecta.minCraterDiameter = 10.0;
    EXPECT_DOUBLE_EQ(terraingen::EjectaSizes(ejecta)->Incremental(1.0),
                     terraingen::GolombekRappSfd(0.1).Incremental(1.0));
    EXPECT_DOUBLE_EQ(terraingen::BoulderCandidates(ejecta)->Incremental(1.0),
                     terraingen::GolombekRappSfd(0.02).Incremental(1.0) +
                         terraingen::GolombekRappSfd(0.1).Incremental(1.0));
}
