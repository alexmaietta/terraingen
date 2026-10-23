// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "terraingen/raster/raster.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

/// The harness every C++ example runs on: an `Example` base class, a registry of the examples
/// linked into a program, and the command line they share, after Newton's examples
/// (`newton.examples`). An example derives from `Example`, generates its terrain in `Generate`,
/// writes files in `Save`, checks its results in `Test`, and registers itself:
///
///     namespace {
///     class CratersSimple : public terraingen::examples::Example {
///       public:
///         using Example::Example;
///         void Generate() override { ...; Show("heights", evaluated); }
///         void Save(const std::filesystem::path& dir) override { ... }
///         void Test() override { Check(craters.Size() > 0, "some craters"); }
///     };
///     }  // namespace
///     TERRAINGEN_EXAMPLE(CratersSimple, "Lunar craters on a synthetic terrain")
///
/// Each example builds into its own program (`example_craters_simple`, run by `StandaloneMain`),
/// and every example into `terraingen-examples`, which lists them and opens them in the GUI's
/// Examples panel (`ExampleBrowser`).
namespace terraingen::examples {

/// What an example runs with: the command line's options and its arguments.
struct ExampleArgs {
    std::filesystem::path outputDir = "output";  ///< Where `Save` writes.
    bool test = false;                           ///< Whether `Test` runs after `Generate`.
    std::vector<std::string> positional;         ///< Arguments that are not options.
};

/// A terrain an example shows in the viewer: heights, and an albedo map on the same grid.
struct ExampleView {
    std::string name;
    Raster<float> heights;
    Raster<float> albedo;  ///< Empty when the view has no albedo.
    bool geographic = false;
};

/// A failed `Example::Check`.
class CheckFailed : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// An example: one terrain (or a few) built from library objects, the text it reports, the
/// files it writes and the checks it passes.
class Example {
  public:
    /// An example reports to `out`: standard output from the command line, the viewer's log in
    /// the GUI.
    Example(ExampleArgs args, std::ostream& out);
    virtual ~Example();
    Example(const Example&) = delete;
    Example& operator=(const Example&) = delete;

    /// Builds and evaluates the example's terrains, reporting to `Out()` and showing them with
    /// `Show`. Runs off the GUI's thread in the viewer.
    virtual void Generate() = 0;

    /// Writes the example's files to `dir`, which exists; nothing by default.
    virtual void Save(const std::filesystem::path& dir);

    /// Checks what `Generate` made with `Check`, as `--test` and the test suite do.
    virtual void Test() = 0;

    /// The terrains `Generate` showed, in order; the viewer shows the first and offers the rest.
    const std::vector<ExampleView>& Views() const;

    const ExampleArgs& Args() const;

  protected:
    std::ostream& Out();

    /// Adds a terrain to `Views()`; `albedo`, when given, lies on the heights' grid.
    void Show(std::string name, const Raster<float>& heights, const Raster<float>* albedo = nullptr,
              bool geographic = false);

    /// Throws `CheckFailed` naming `what` unless `condition` holds.
    static void Check(bool condition, const std::string& what);

  private:
    ExampleArgs args_;
    std::ostream* out_;
    std::vector<ExampleView> views_;
};

/// A registered example: its names from its file, `example_<category>_<name>.cpp`, its
/// one-line description, and how to make one.
struct ExampleInfo {
    std::string name;      ///< The file's stem without "example_", such as "craters_simple".
    std::string category;  ///< The name's first word, such as "craters".
    std::string description;
    std::function<std::unique_ptr<Example>(const ExampleArgs&, std::ostream&)> create;

    /// The program's and file's name, such as "example_craters_simple".
    std::string Program() const;
};

/// The `ExampleInfo` of the example in source file `file`.
/// @throws std::invalid_argument for a file not named `example_<category>_<name>.cpp`.
ExampleInfo
MakeInfo(std::string_view file, std::string description,
         std::function<std::unique_ptr<Example>(const ExampleArgs&, std::ostream&)> create);

/// Adds `info` to the registry; returns true, for `TERRAINGEN_EXAMPLE`'s static initializer.
/// @throws std::invalid_argument if an example of that name is registered.
bool RegisterExample(ExampleInfo info);

/// The registered examples, by category in the order the examples teach them (terrain, craters,
/// distributions, ages, boulders, albedo, layers, planets, sources, tiles, config; others last),
/// then by name.
std::vector<const ExampleInfo*> Examples();

/// The example named `name` ("craters_simple" or "example_craters_simple"), or null.
const ExampleInfo* FindExample(std::string_view name);

/// The example source folder, for inputs such as the example configs.
std::filesystem::path ExamplesDir();

/// Exit codes of the example programs.
inline constexpr int kExitSuccess = 0;
inline constexpr int kExitFailure = 1;
inline constexpr int kExitUsage = 2;

/// Opens `info` in the viewer; given to `Main` by programs built with the GUI.
/// @return an exit code.
using WindowRunner =
    std::function<int(const ExampleInfo* info, const ExampleArgs& args, bool headless)>;

/// Runs `example` without a viewer: `Generate`, `Save` into `args.outputDir` when `save`, then
/// `Test` with `args.test`. Reports to `out` and failures to `err`.
/// @return `kExitSuccess`, or `kExitFailure` if the example threw or a check failed.
int RunHeadless(const ExampleInfo& info, const ExampleArgs& args, bool save, std::ostream& out,
                std::ostream& err);

/// Runs `terraingen-examples [NAME] [ARGS...] [OPTIONS]` with `args` (without the program name):
/// - `--list` prints the examples by category;
/// - `--viewer window` (the default when `window` is given) opens NAME, or the first example,
///   in the GUI's Examples panel; `--headless` renders the window hidden; `--viewer null` runs
///   NAME without a viewer, as its own program does;
/// - `--test` runs the example's checks; `--output-dir DIR` (default ./output) is where outputs
///   go; `--save` / `--no-save` write them or not (saved without a viewer by default);
/// - `-h` or `--help`.
/// @return an exit code.
int Main(const std::vector<std::string>& args, std::ostream& out, std::ostream& err,
         const WindowRunner& window = nullptr);

/// Runs the one example linked into an example's own program:
/// `example_<category>_<name> [ARGS...] [--output-dir DIR] [--no-save] [--test]`.
int StandaloneMain(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace terraingen::examples

/// Registers `Class`, an `Example` with a `(const ExampleArgs&, std::ostream&)` constructor
/// declared in an anonymous namespace, under its file's name with `description`.
#define TERRAINGEN_EXAMPLE(Class, description)                                                     \
    namespace {                                                                                    \
    [[maybe_unused]] const bool terraingen_example_registered =                                    \
        ::terraingen::examples::RegisterExample(::terraingen::examples::MakeInfo(                  \
            __FILE__, description,                                                                 \
            [](const ::terraingen::examples::ExampleArgs& args,                                    \
               std::ostream& out) -> std::unique_ptr<::terraingen::examples::Example> {            \
                return std::make_unique<Class>(args, out);                                         \
            }));                                                                                   \
    }
