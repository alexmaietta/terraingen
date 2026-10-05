#include "app.h"

#include <cerrno>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace terraingen::viewer {

std::string AppUsage() {
    return "Usage: terraingen-viewer [options] [DEM]\n"
           "  DEM                    GeoTIFF to view, in a projected CRS; craters and noise "
           "detail\n"
           "                         start off and can be enabled in the Terrain panel\n"
           "  --brdf ID              Reflectance model to start with (lambert, lommel_seeliger,\n"
           "                         lunar_lambert, oren_nayar, hapke)\n"
           "  --detail-albedo PATH   Tileable albedo texture (PNG, JPEG or EXR)\n"
           "  --detail-normal PATH   Tileable tangent-space normal map (PNG or EXR)\n"
           "  --frames N             Quit after N frames\n"
           "  --log-level LEVEL      trace, debug, info (default), warning, error or off\n"
           "  --validation           Enable the Vulkan validation layer, if installed\n"
           "  --hidden               Render without a window (for tests)\n"
           "  --help                 Show this message\n";
}

AppOptions ParseAppOptions(int argc, const char* const* argv) {
    AppOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view flag = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::invalid_argument(std::string(flag) + " needs a value");
            }
            return argv[++i];
        };
        if (flag == "--frames") {
            const std::string text = value();
            char* end = nullptr;
            errno = 0;
            options.maxFrames = std::strtol(text.c_str(), &end, 10);
            if (text.empty() || *end != '\0' || errno != 0) {
                throw std::invalid_argument("--frames needs an integer, not '" + text + "'");
            }
        } else if (flag == "--log-level") {
            const std::string text = value();
            const std::optional<LogLevel> level = ParseLogLevel(text);
            if (!level) {
                throw std::invalid_argument("--log-level needs trace, debug, info, warning, error "
                                            "or off, not '" +
                                            text + "'");
            }
            options.logLevel = *level;
        } else if (flag == "--validation") {
            options.validation = true;
        } else if (flag == "--hidden") {
            options.hidden = true;
        } else if (flag == "--brdf") {
            options.brdf = value();
        } else if (flag == "--detail-albedo") {
            options.detailAlbedo = value();
        } else if (flag == "--detail-normal") {
            options.detailNormal = value();
        } else if (!flag.starts_with("-")) {
            if (!options.dem.empty()) {
                throw std::invalid_argument("Only one DEM can be given; got '" + options.dem +
                                            "' and '" + std::string(flag) + "'");
            }
            options.dem = flag;
        } else {
            throw std::invalid_argument("Unknown option " + std::string(flag));
        }
    }
    return options;
}

}  // namespace terraingen::viewer
