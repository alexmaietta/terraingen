/// @file
/// @brief The `terraingen` command line: exit codes, usage and argument handling.
#pragma once

#include "terraingen/gpu/backend.h"

#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace terraingen::cli {

/// Exit code of a successful run.
inline constexpr int kExitSuccess = 0;
/// Exit code of an invalid configuration.
inline constexpr int kExitInvalidConfig = 1;
/// Exit code of any other failure, such as an unwritable output; equal to
/// `kExitInvalidConfig`, which scripts already treat as a failed run.
inline constexpr int kExitFailure = 1;
/// Exit code of bad command-line arguments.
inline constexpr int kExitUsage = 2;

/// The usage text `terraingen --help` prints.
std::string Usage();

/// Parses a `--threads` value: a count from 0 (every hardware thread) to 4096.
/// @throws std::invalid_argument for anything that is not such a count.
unsigned ParseThreads(const std::string& text);

/// Parses a `--backend` value (`BackendNamed`), ignoring case.
/// @throws std::invalid_argument for any other name.
Backend ParseBackend(const std::string& text);

/// The options of `terraingen [PATH] --gui`.
struct GuiArgs {
    std::string open;      ///< Configuration (.json) or heights (.tif) to open; empty for none.
    unsigned threads = 0;  ///< Generation threads; 0 for every hardware thread.
    std::optional<Backend> backend = std::nullopt;  ///< Empty for `DefaultBackend()`.
};

/// Parses the arguments of `--gui`, without it: an optional PATH, `--threads N` and
/// `--backend NAME`.
/// @throws std::invalid_argument for anything else.
GuiArgs ParseGuiArgs(const std::vector<std::string>& args);

/// Runs `terraingen`.
///
/// Commands:
/// - `CONFIG [--output-dir DIR] [--log-level LEVEL] [--threads N] [--backend NAME]` generates the
///   terrain the JSON file CONFIG describes and writes its outputs; relative input paths resolve
///   against the config's folder, relative output paths against DIR or the config's folder, and
///   NAME (auto, cpu, gpu, vulkan, cuda or metal; by default `DefaultBackend()`) is where the
///   heights are evaluated;
/// - `validate CONFIG` checks CONFIG without generating and reports its grid and expected craters;
/// - `schema` prints every configuration type with its defaults (`ConfigCatalog`);
/// - `backends` prints each backend with its device or why it is unavailable, then the default;
/// - `[PATH] --gui [--threads N] [--backend NAME]`, `--gui` anywhere, opens the GUI (`RunGui`) on
///   PATH or the starter terrain, generating on NAME as above;
/// - `-V` or `--version`, and `-h` or `--help`.
/// @param args The arguments, without the program name.
/// @return `kExitSuccess`, `kExitInvalidConfig` for a `ConfigError`, `kExitFailure` for another
/// error, or `kExitUsage` for bad arguments, which prints the problem and a pointer to `--help`.
int Run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace terraingen::cli
