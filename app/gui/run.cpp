// SPDX-License-Identifier: BSD-3-Clause

#include "app/gui/run.h"

#include "app/cli.h"
#include "app/gui/gui_app.h"

#include <exception>
#include <string>

namespace terraingen::cli {

bool GuiAvailable() {
    return true;
}

int RunGui(const std::vector<std::string>& args, std::ostream& /*out*/, std::ostream& err) {
    const GuiArgs parsed = ParseGuiArgs(args);
    gui::GuiAppOptions options;
    options.open = parsed.open;
    options.threads = parsed.threads;
    options.backend = parsed.backend;
    try {
        gui::GuiApp app(options);
        app.Run();
    } catch (const std::exception& error) {
        err << "terraingen: the GUI stopped: " << error.what() << "\n";
        return kExitFailure;
    }
    return kExitSuccess;
}

}  // namespace terraingen::cli
