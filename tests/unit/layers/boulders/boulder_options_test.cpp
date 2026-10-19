#include "terraingen/layers/boulders/boulder_clusters.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

using terraingen::BoulderLayerOptions;

TEST(ValidateBoulderOptions, ChecksEachGroupOnItsOwn) {
    EXPECT_NO_THROW(terraingen::ValidateBoulderSizes({}));
    terraingen::BoulderSizes pointSize;
    pointSize.minDiameter = 0.0;
    EXPECT_THROW(terraingen::ValidateBoulderSizes(pointSize), std::invalid_argument);
    terraingen::BoulderSizes fullCover;
    fullCover.coverage = 1.0;
    EXPECT_THROW(terraingen::ValidateBoulderSizes(fullCover), std::invalid_argument);
    terraingen::BoulderSizes rate;
    rate.distribution =
        std::make_shared<terraingen::PowerLawSfd>(1.0, 2.0, terraingen::SfdKind::ProductionRate);
    EXPECT_THROW(terraingen::ValidateBoulderSizes(rate), std::invalid_argument);

    EXPECT_NO_THROW(terraingen::ValidateBoulderPatches({}));
    terraingen::BoulderPatches narrow;
    narrow.spread = 0.5;
    EXPECT_THROW(terraingen::ValidateBoulderPatches(narrow), std::invalid_argument);
    terraingen::BoulderPatches pointPatches;
    pointPatches.size = 0.0;
    EXPECT_THROW(terraingen::ValidateBoulderPatches(pointPatches), std::invalid_argument);

    EXPECT_NO_THROW(terraingen::ValidateBoulderEjecta({}, {}));
    terraingen::BoulderEjecta shortReach;
    shortReach.minCraterDiameter = 10.0;
    shortReach.reach = 1.0;
    EXPECT_THROW(terraingen::ValidateBoulderEjecta(shortReach, {}), std::invalid_argument);

    EXPECT_NO_THROW(terraingen::ValidateBoulderBedrock({}));
    terraingen::BoulderBedrock barren;
    barren.abundance = 0.0;
    EXPECT_THROW(terraingen::ValidateBoulderBedrock(barren), std::invalid_argument);
    terraingen::BoulderBedrock negativeSize;
    negativeSize.meanSize = -1.0;
    EXPECT_THROW(terraingen::ValidateBoulderBedrock(negativeSize), std::invalid_argument);

    EXPECT_NO_THROW(terraingen::ValidateBoulderPose({}));
    terraingen::BoulderPose flat;
    flat.minHeightRatio = 0.0;
    EXPECT_THROW(terraingen::ValidateBoulderPose(flat), std::invalid_argument);
    terraingen::BoulderPose inverted;
    inverted.minBurial = 0.5;
    inverted.maxBurial = 0.4;
    EXPECT_THROW(terraingen::ValidateBoulderPose(inverted), std::invalid_argument);

    EXPECT_NO_THROW(terraingen::ValidateBoulderRendering({}));
    terraingen::BoulderRendering noSamples;
    noSamples.models.minSamples = 0.0;
    EXPECT_THROW(terraingen::ValidateBoulderRendering(noSamples), std::invalid_argument);
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

TEST(BoulderBackground, BuildsTheModelsOnceAndMatchesTheirFunctions) {
    BoulderLayerOptions options;
    options.patches.spread = 3.0;
    const terraingen::BoulderBackground plain(options);
    EXPECT_EQ(plain.patchMean, 1.0) << "patches are only rescaled under a map";
    EXPECT_FALSE(plain.map.has_value());
    EXPECT_EQ(plain.max, terraingen::MaxBackgroundAbundance(options));
    EXPECT_EQ(terraingen::BoulderCandidates(options, plain.max)->Incremental(0.5),
              terraingen::BoulderCandidates(options)->Incremental(0.5));
    options.sizes.coverage = 0.5;
    EXPECT_THROW(terraingen::BoulderCandidates(options), std::invalid_argument)
        << "the checked overload validates";
}

TEST(LatticeMean, AveragesOverTheLatticeCentres) {
    EXPECT_DOUBLE_EQ(terraingen::LatticeMean(4.0, [](double x, double y) { return x + y; }), 4.0);
    EXPECT_EQ(terraingen::LatticeMean(1.0, [](double, double) { return 2.0; }), 2.0);
}
