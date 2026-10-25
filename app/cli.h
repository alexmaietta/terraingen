// SPDX-License-Identifier: BSD-3-Clause

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
/// Exit code of a failed run: an invalid configuration or any other error, such as an unwritable
/// output.
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

/// Runs `terraingen`.
///
/// Commands:
/// - `CONFIG [--output-dir DIR] [--log-level LEVEL] [--threads N] [--backend NAME]` generates the
///   terrain the JSON file CONFIG describes and writes its outputs; relative input paths resolve
///   against the config's folder, relative output paths against DIR or the config's folder, and
///   NAME (one of `kBackendNames`; by default `DefaultBackend()`) is where the heights are
///   evaluated;
/// - `validate CONFIG` checks CONFIG without generating and reports its grid and expected craters;
/// - `schema` prints the structure of a configuration for editors (`ConfigSchema`);
/// - `backends` prints each backend with its device or why it is unavailable, then the default;
/// - `-V` or `--version`, and `-h` or `--help`.
/// @param args The arguments, without the program name.
/// @return `kExitSuccess`, `kExitFailure` for a `ConfigError` or another error, or `kExitUsage`
/// for bad arguments, which prints the problem and a pointer to `--help`.
int Run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace terraingen::cli
