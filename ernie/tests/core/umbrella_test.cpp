#include "ernie/ernie.h"

#include <gtest/gtest.h>

#include <type_traits>

TEST(Umbrella, ExposesEveryLayer) {
    EXPECT_TRUE((std::is_class_v<ernie::Scene>));
    EXPECT_TRUE((std::is_class_v<ernie::Tensor>));
    EXPECT_TRUE((std::is_class_v<ernie::VulkanContext>));
    EXPECT_TRUE((std::is_class_v<ernie::CudaInterop>));
    EXPECT_TRUE((std::is_class_v<ernie::RasterRenderer>));
    EXPECT_TRUE((std::is_base_of_v<ernie::RasterPart, ernie::TerrainPart>));
    EXPECT_TRUE((std::is_base_of_v<ernie::RasterPart, ernie::MeshPart>));
    EXPECT_TRUE((std::is_class_v<ernie::RasterDelegate>));
    EXPECT_EQ(ernie::tokens::kTerrain, "Terrain");
}
