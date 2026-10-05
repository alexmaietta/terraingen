#include "ernie/raster/raster_delegate.h"
#include "ernie/render/render_delegate.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace ernie {

std::vector<std::string> RenderBackends() {
    return {"raster"};
}

std::unique_ptr<RenderDelegate> CreateRenderDelegate(std::string_view backend,
                                                     const VulkanContext& context,
                                                     VkFormat presentFormat, BrdfRegistry brdfs) {
    if (backend == "raster") {
        return std::make_unique<RasterDelegate>(context, presentFormat, std::move(brdfs));
    }
    std::string known;
    for (const std::string& name : RenderBackends()) {
        known += (known.empty() ? "" : ", ") + name;
    }
    throw std::invalid_argument("Unknown render backend '" + std::string(backend) +
                                "'; available: " + known);
}

}  // namespace ernie
