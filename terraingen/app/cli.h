/// @file
/// @brief The `terraingen` command line: exit codes, usage and argument handling.
#pragma once

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

/// Runs `terraingen`.
///
/// Commands:
/// - `CONFIG [--output-dir DIR] [--log-level LEVEL] [--threads N]` generates the terrain the JSON
///   file CONFIG describes and writes its outputs; relative input paths resolve against the
///   config's folder, relative output paths against DIR or the config's folder;
/// - `validate CONFIG` checks CONFIG without generating and reports its grid and expected craters;
/// - `schema` prints every configuration type with its defaults (`ConfigCatalog`);
/// - `[PATH] --gui [--threads N]`, `--gui` anywhere, opens the GUI (`RunGui`);
/// - `-V` or `--version`, and `-h` or `--help`.
/// @param args The arguments, without the program name.
/// @return `kExitSuccess`, `kExitInvalidConfig` for a `ConfigError`, `kExitFailure` for another
/// error, or `kExitUsage` for bad arguments.
int Run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace terraingen::cli
