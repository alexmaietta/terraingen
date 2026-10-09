#include "terraingen/app/viewer/view.h"

#include "terraingen/app/cli.h"
#include "terraingen/app/viewer/viewer_app.h"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>

namespace terraingen::cli {

bool ViewerAvailable() {
    return true;
}

int RunView(const std::vector<std::string>& args, std::ostream& /*out*/, std::ostream& err) {
    viewer::ViewerAppOptions options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--threads") {
            if (i + 1 >= args.size()) {
                throw std::invalid_argument("--threads needs a value");
            }
            options.threads = ParseThreads(args[++i]);
        } else if (arg.rfind("--", 0) == 0) {
            throw std::invalid_argument("Unknown option " + arg);
        } else if (!options.open.empty()) {
            throw std::invalid_argument("Unexpected argument " + arg);
        } else {
            options.open = arg;
        }
    }
    try {
        viewer::ViewerApp app(options);
        app.Run();
    } catch (const std::exception& error) {
        err << "terraingen: the viewer stopped: " << error.what() << "\n";
        return kExitInvalidConfig;
    }
    return kExitSuccess;
}

}  // namespace terraingen::cli
