#include "ernie/render/render_delegate.h"
#include "ernie/render/resolve.h"
#include "ernie/render/sampling.h"
#include "ernie/scene/tokens.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using ernie::AttributeBinding;
using ernie::Bounds3;
using ernie::FrameId;
using ernie::Path;
using ernie::RenderDelegate;
using ernie::Scene;
using ernie::Tensor;
namespace tokens = ernie::tokens;

namespace {

/// A backend that renders nothing, to exercise what `RenderDelegate` provides itself.
class FakeDelegate : public RenderDelegate {
  public:
    const ernie::RendererInfo& Info() const override {
        static const ernie::RendererInfo info{"fake", true};
        return info;
    }
    const ernie::BrdfRegistry& Brdfs() const override {
        return brdfs_;
    }
    void Sync(const Scene&) override {}
    void Update(const Scene&, const Path&, VkExtent2D) override {}
    std::vector<ernie::TimelinePoint> SubmitWaits() const override {
        return {};
    }
    void Record(VkCommandBuffer, const ernie::RenderingTarget&) override {}
    ernie::StepOperation StepAsync(const Scene&, std::span<const Path>, const Path&) override {
        throw std::logic_error("not rendered");
    }
    ernie::CudaInterop* Cuda(std::string* why) override {
        if (why != nullptr) {
            *why = "no CUDA in tests";
        }
        return nullptr;
    }
    AttributeBinding Bind(const Scene&, const Path& path, std::string_view attribute) override {
        return MakeBinding(path, std::string(attribute),
                           Bounds3{glm::vec3(-1.0f), glm::vec3(1.0f)});
    }
    Path PrimPath(std::uint32_t) const override {
        return {};
    }
    ernie::RenderStats Stats() const override {
        return {};
    }
    std::optional<Bounds3> SceneBounds() const override {
        return std::nullopt;
    }

    using RenderDelegate::NextFrame;

    std::vector<std::pair<std::string, bool>> calls;

  protected:
    Tensor MapBinding(const AttributeBinding& binding, std::uintptr_t) override {
        calls.emplace_back(binding.Attribute(), true);
        return {};
    }
    void UnmapBinding(const AttributeBinding& binding, std::uintptr_t) override {
        calls.emplace_back(binding.Attribute(), false);
    }

  private:
    ernie::BrdfRegistry brdfs_ = ernie::BrdfRegistry::WithBuiltins();
};

}  // namespace

TEST(RenderDelegate, FramesCountPerProductUnlessTheSettingsAuthorOne) {
    Scene scene;
    scene.Define("/Render/Settings", tokens::kRenderSettings);
    FakeDelegate delegate;
    const Path settings("/Render/Settings");
    const Path left("/Render/Left");
    const Path right("/Render/Right");
    EXPECT_EQ(delegate.NextFrame(scene, settings, left).frame, 0u);
    EXPECT_EQ(delegate.NextFrame(scene, settings, left).frame, 1u);
    EXPECT_EQ(delegate.NextFrame(scene, settings, right).frame, 0u)
        << "products count their own frames";
    const FrameId id = delegate.NextFrame(scene, settings, left);
    EXPECT_EQ(id.frame, 2u);
    EXPECT_EQ(id.stream, ernie::StreamOf("/Render/Left"));
    EXPECT_NE(id.stream, delegate.NextFrame(scene, settings, right).stream);

    scene.Set(settings, tokens::kFrame, 41);
    EXPECT_EQ(delegate.NextFrame(scene, settings, left).frame, 41u);
    EXPECT_EQ(delegate.NextFrame(scene, settings, left).frame, 41u) << "authored frames repeat";
    EXPECT_EQ(delegate.NextFrame(scene, {}, left).frame, 5u) << "no settings: the count";
}

TEST(RenderDelegate, BindingsMapAndUnmapThroughTheirDelegate) {
    Scene scene;
    FakeDelegate delegate;
    AttributeBinding binding = delegate.Bind(scene, Path("/World/Mesh"), tokens::kPoints);
    EXPECT_EQ(binding.GetPath(), Path("/World/Mesh"));
    EXPECT_EQ(binding.Attribute(), tokens::kPoints);
    ASSERT_TRUE(binding.Extent().has_value());
    EXPECT_EQ(binding.Extent()->second, glm::vec3(1.0f));
    EXPECT_FALSE(binding.IsMapped());
    binding.Unmap();
    EXPECT_TRUE(delegate.calls.empty()) << "unmapping an unmapped binding does nothing";
    binding.Map();
    EXPECT_TRUE(binding.IsMapped());
    binding.Unmap();
    EXPECT_FALSE(binding.IsMapped());
    ASSERT_EQ(delegate.calls.size(), 2u);
    EXPECT_TRUE(delegate.calls[0].second);
    EXPECT_FALSE(delegate.calls[1].second);

    AttributeBinding empty;
    EXPECT_THROW(empty.Map(), std::invalid_argument);
    empty.Unmap();
}

TEST(RenderDelegate, CudaRendersNeedCudaInterop) {
    Scene scene;
    FakeDelegate delegate;
    EXPECT_THROW(delegate.Render(scene, {}, ernie::Device{ernie::DeviceKind::Cuda, 0}),
                 std::invalid_argument);
    EXPECT_THROW(delegate.Step(scene), std::logic_error) << "Step runs StepAsync";
}

TEST(RenderDelegate, TheRasterizerIsABackend) {
    EXPECT_EQ(ernie::RenderBackends(), std::vector<std::string>{"raster"});
}
