// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/boulders/boulder_clusters.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

using terraingen::BoulderLayerOptions;

TEST(ValidateBoulderOptions, ChecksEachGroupOnItsOwn) {
    EXPECT_NO_THROW(terraingen::ValidateBoulderLayerOptions({}));
    const auto rejects = [](auto change) {
        BoulderLayerOptions options;
        change(options);
        EXPECT_THROW(terraingen::ValidateBoulderLayerOptions(options), std::invalid_argument);
    };
    rejects([](BoulderLayerOptions& o) { o.sizes.minDiameter = 0.0; });
    rejects([](BoulderLayerOptions& o) { o.sizes.coverage = 1.0; });
    rejects([](BoulderLayerOptions& o) {
        o.sizes.distribution = std::make_shared<terraingen::PowerLawSfd>(
            1.0, 2.0, terraingen::SfdKind::ProductionRate);
    });

    EXPECT_NO_THROW(terraingen::ValidateBoulderPatches({}));
    terraingen::BoulderPatches narrow;
    narrow.spread = 0.5;
    EXPECT_THROW(terraingen::ValidateBoulderPatches(narrow), std::invalid_argument);
    terraingen::BoulderPatches pointPatches;
    pointPatches.size = 0.0;
    EXPECT_THROW(terraingen::ValidateBoulderPatches(pointPatches), std::invalid_argument);

    rejects([](BoulderLayerOptions& o) {
        o.ejecta.minCraterDiameter = 10.0;
        o.ejecta.reach = 1.0;
    });
    rejects([](BoulderLayerOptions& o) { o.bedrock.abundance = 0.0; });
    rejects([](BoulderLayerOptions& o) { o.bedrock.meanSize = -1.0; });
    rejects([](BoulderLayerOptions& o) { o.pose.minHeightRatio = 0.0; });
    rejects([](BoulderLayerOptions& o) {
        o.pose.minBurial = 0.5;
        o.pose.maxBurial = 0.4;
    });
    rejects([](BoulderLayerOptions& o) { o.rendering.models.minSamples = 0.0; });
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

TEST(LatticeMean, AveragesOverTheLatticeCenters) {
    EXPECT_DOUBLE_EQ(terraingen::LatticeMean(4.0, [](double x, double y) { return x + y; }), 4.0);
    EXPECT_EQ(terraingen::LatticeMean(1.0, [](double, double) { return 2.0; }), 2.0);
}
