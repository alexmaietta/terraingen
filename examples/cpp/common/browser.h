// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "common/example.h"
#include "terraingen/app/gui/gui_app.h"

#include <cstddef>
#include <memory>
#include <string>

namespace terraingen::examples {

/// The Examples panel of `terraingen-examples`, docked left in the GUI (`gui::GuiApp`) in place
/// of its configuration panels (the Window menu shows those again): the registered examples by
/// category to pick from, and for the one picked its description, status, the terrain to view
/// when it shows several, and buttons to run it again, run its checks and save its outputs.
///
/// Picking an example generates it on a thread of its own, so the GUI stays responsive; its text
/// goes to the log and its first terrain replaces the scene's, framed.
class ExampleBrowser {
  public:
    /// Adds the panel to `app`'s viewer; examples run with `args` (output folder, test flag).
    ExampleBrowser(gui::GuiApp& app, ExampleArgs args);
    /// Waits for a generation that is running.
    ~ExampleBrowser();
    ExampleBrowser(const ExampleBrowser&) = delete;
    ExampleBrowser& operator=(const ExampleBrowser&) = delete;

    /// Generates `info` in the background; while another generates, it runs once that one ends.
    void Select(const ExampleInfo& info);

    /// Whether an example is generating or waiting to.
    bool Busy() const;

    /// The example picked last, or null.
    const ExampleInfo* Current() const;

    /// The example generated last, or null while it generates or when it failed.
    Example* CurrentExample();

    /// Shows the current example's terrain `index` (of its `Views()`).
    void ShowView(std::size_t index);
    std::size_t ViewIndex() const;

    /// Runs the current example's checks, reporting to the log.
    /// @return whether they passed; false without a generated example.
    bool RunTest();

    /// Writes the current example's outputs to the output folder, reporting to the log.
    /// @return whether they were written.
    bool SaveOutputs();

    /// One line on what last happened.
    const std::string& Status() const;

    /// Takes a finished generation and starts a waiting one; the viewer calls it every frame.
    void Update();

  private:
    void Draw();

    struct State;
    std::unique_ptr<State> state_;
};

/// Opens the GUI with the Examples panel on `info`, or the first example, and runs it until the
/// window closes; or with `headless`, renders hidden until the example has generated. With
/// `args.test`, then runs the example's checks. `Main`'s `WindowRunner` in builds with the GUI.
/// @return `kExitSuccess`, or `kExitFailure` if the GUI could not start, the example failed or
/// a check failed.
int RunWindow(const ExampleInfo* info, const ExampleArgs& args, bool headless);

}  // namespace terraingen::examples
