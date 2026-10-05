#include "ernie/camera/sensor_filters.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <vector>

using ernie::SensorFilter;
namespace render_vars = ernie::render_vars;

namespace {

bool Contains(const std::vector<std::string_view>& names, std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

}  // namespace

TEST(SensorFilters, NamesRoundTrip) {
    for (const SensorFilter filter : ernie::kSensorFilters) {
        EXPECT_EQ(ernie::SensorFilterNamed(ernie::NameOf(filter)), filter);
    }
    EXPECT_FALSE(ernie::SensorFilterNamed("bloom").has_value());
    EXPECT_EQ(ernie::DefaultFilterChain().size(), ernie::kSensorFilters.size());
}

TEST(SensorFilters, CheckFilterChainPrependsTheLensAndChecksOrder) {
    const std::vector<SensorFilter> cie = ernie::CheckFilterChain(
        std::vector<SensorFilter>{SensorFilter::Cie, SensorFilter::Exposure});
    EXPECT_EQ(cie, (std::vector<SensorFilter>{SensorFilter::Lens, SensorFilter::Cie,
                                              SensorFilter::Exposure}));
    EXPECT_EQ(ernie::CheckFilterChain(ernie::DefaultFilterChain()), ernie::DefaultFilterChain());
    EXPECT_EQ(ernie::CheckFilterChain({}), std::vector<SensorFilter>{SensorFilter::Lens});

    EXPECT_THROW(ernie::CheckFilterChain(std::vector<SensorFilter>{SensorFilter::Noise}),
                 std::invalid_argument)
        << "noise needs electrons";
    EXPECT_THROW(ernie::CheckFilterChain(std::vector<SensorFilter>{SensorFilter::Adc}),
                 std::invalid_argument);
    EXPECT_THROW(ernie::CheckFilterChain(std::vector<SensorFilter>{SensorFilter::Exposure}),
                 std::invalid_argument)
        << "exposure needs HdrColor";
    EXPECT_THROW(
        ernie::CheckFilterChain(std::vector<SensorFilter>{SensorFilter::Qe, SensorFilter::Qe}),
        std::invalid_argument);
}

TEST(SensorFilters, VarsWrittenByFollowTheChain) {
    const std::vector<std::string_view> lens =
        ernie::VarsWrittenBy(std::vector<SensorFilter>{SensorFilter::Lens});
    for (const std::string_view name :
         {render_vars::kSpectralRadiance, render_vars::kDistanceToImagePlane,
          render_vars::kDistanceToCamera, render_vars::kNormal, render_vars::kPrimId,
          render_vars::kInstanceId}) {
        EXPECT_TRUE(Contains(lens, name)) << name;
    }
    EXPECT_FALSE(Contains(lens, render_vars::kLdrColor));

    const std::vector<std::string_view> all = ernie::VarsWrittenBy(ernie::DefaultFilterChain());
    EXPECT_EQ(all.size(), ernie::kRenderVarNames.size()) << "the default chain writes every var";
    for (const std::string_view name : ernie::kRenderVarNames) {
        EXPECT_TRUE(Contains(all, name)) << name;
    }
}
