#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace terraingen::cli {

/// Exit codes of `terraingen`.
inline constexpr int kExitSuccess = 0;
inline constexpr int kExitInvalidConfig = 1;
inline constexpr int kExitUsage = 2;

/// Usage text for `terraingen --help`.
std::string Usage();

/// Runs `terraingen` with `args` (without the program name), writing results to `out` and errors
/// to `err`:
/// - `CONFIG [--output-dir DIR] [--log-level LEVEL] [--threads N]` generates the terrain the JSON
///   file CONFIG describes and writes its outputs; relative input paths resolve against the
///   config's folder, relative output paths against DIR or the config's folder;
/// - `validate CONFIG` checks CONFIG without generating and reports its grid and the craters
///   expected;
/// - `schema` prints every configuration type with its defaults (`ConfigCatalog`);
/// - `-V` or `--version`, and `-h` or `--help`.
/// @return `kExitSuccess`, `kExitInvalidConfig` for a configuration, input or output error, or
/// `kExitUsage` for bad arguments.
int Run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace terraingen::cli
