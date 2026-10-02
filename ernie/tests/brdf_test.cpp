#include "ernie/brdf.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

using ernie::BrdfModel;
using ernie::BrdfParam;
using ernie::BrdfRegistry;
using ernie::ResolveBrdfParams;

namespace {

BrdfModel Custom(std::string id) {
    return {std::move(id),
            "Custom",
            "",
            {},
            {{"k", "Gain", "", 0.5f, 0.0f, 1.0f, false, ""}},
            "fn brdf_radiance_factor(s: BrdfInput) -> vec3<f32> { return s.albedo; }"};
}

}  // namespace

TEST(BrdfRegistry, BuiltinsAreDocumentedAndValid) {
    const BrdfRegistry registry = BrdfRegistry::WithBuiltins();
    const std::vector<std::string> ids = {"lambert", "lommel_seeliger", "lunar_lambert",
                                          "oren_nayar", "hapke"};
    ASSERT_EQ(registry.Models().size(), ids.size());
    for (const std::string& id : ids) {
        const BrdfModel* model = registry.Find(id);
        ASSERT_NE(model, nullptr) << id;
        EXPECT_FALSE(model->name.empty());
        EXPECT_FALSE(model->description.empty());
        EXPECT_FALSE(model->references.empty());
        EXPECT_NE(model->wgsl.find("fn brdf_radiance_factor"), std::string::npos);
        EXPECT_LE(model->params.size(), BrdfRegistry::kMaxParams);
        EXPECT_EQ(model->Defaults().size(), model->params.size());
    }
    EXPECT_EQ(registry.Find("phong"), nullptr);
    EXPECT_EQ(registry.Find("hapke")->params.size(), 6u);
    EXPECT_NE(std::string(ernie::BrdfCommonWgsl()).find("struct BrdfInput"), std::string::npos);
}

TEST(BrdfRegistry, AddsCustomModelsAndRejectsInvalidOnes) {
    BrdfRegistry registry;
    EXPECT_TRUE(registry.Models().empty());
    registry.Add(Custom("custom"));
    ASSERT_NE(registry.Find("custom"), nullptr);
    EXPECT_THROW(registry.Add(Custom("custom")), std::invalid_argument);
    EXPECT_THROW(registry.Add(Custom("")), std::invalid_argument);

    BrdfModel noFunction = Custom("no_function");
    noFunction.wgsl = "fn something_else() {}";
    EXPECT_THROW(registry.Add(noFunction), std::invalid_argument);

    BrdfModel tooMany = Custom("too_many");
    tooMany.params.assign(BrdfRegistry::kMaxParams + 1, tooMany.params[0]);
    EXPECT_THROW(registry.Add(tooMany), std::invalid_argument);

    for (const BrdfParam& bad : {BrdfParam{"k", "", "", 0.5f, 1.0f, 1.0f, false, ""},
                                 BrdfParam{"k", "", "", 2.0f, 0.0f, 1.0f, false, ""},
                                 BrdfParam{"k", "", "", -1.0f, 0.0f, 1.0f, false, ""},
                                 BrdfParam{"k", "", "", 0.5f, 0.0f, 1.0f, true, ""}}) {
        BrdfModel model = Custom("bad_param");
        model.params = {bad};
        EXPECT_THROW(registry.Add(model), std::invalid_argument);
    }
    EXPECT_EQ(registry.Models().size(), 1u);
}

TEST(BrdfRegistry, ResolvesParametersWithDefaultsAndClamping) {
    const BrdfModel* hapke = BrdfRegistry::WithBuiltins().Find("hapke");
    ASSERT_NE(hapke, nullptr);
    const std::vector<float> defaults = hapke->Defaults();
    EXPECT_EQ(ResolveBrdfParams(*hapke, {}), defaults);

    const std::vector<float> partial = {0.4f, 5.0f};
    const std::vector<float> resolved = ResolveBrdfParams(*hapke, partial);
    ASSERT_EQ(resolved.size(), defaults.size());
    EXPECT_FLOAT_EQ(resolved[0], 0.4f);
    EXPECT_FLOAT_EQ(resolved[1], 1.0f) << "clamped to the range";
    for (std::size_t i = 2; i < defaults.size(); ++i) {
        EXPECT_FLOAT_EQ(resolved[i], defaults[i]);
    }
    const std::vector<float> extra(20, 0.1f);
    EXPECT_EQ(ResolveBrdfParams(*hapke, extra).size(), defaults.size());
}
