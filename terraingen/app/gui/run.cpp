/// @file
/// @brief `terraingen --gui` in a build with the GUI.
#include "terraingen/app/gui/run.h"

#include "terraingen/app/cli.h"
#include "terraingen/app/gui/gui_app.h"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>

namespace terraingen::cli {

bool GuiAvailable() {
    return true;
}

int RunGui(const std::vector<std::string>& args, std::ostream& /*out*/, std::ostream& err) {
    gui::GuiAppOptions options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--threads") {
            if (i + 1 >= args.size()) {
                throw std::invalid_argument("--threads needs a value");
            }
            options.threads = ParseThreads(args[++i]);
        } else if (arg.rfind("--", 0) == 0) {
            throw std::invalid_argument("Unknown option " + arg + " with --gui");
        } else if (!options.open.empty()) {
            throw std::invalid_argument("Unexpected argument " + arg);
        } else {
            options.open = arg;
        }
    }
    try {
        gui::GuiApp app(options);
        app.Run();
    } catch (const std::exception& error) {
        err << "terraingen: the GUI stopped: " << error.what() << "\n";
        return kExitInvalidConfig;
    }
    return kExitSuccess;
}

}  // namespace terraingen::cli
