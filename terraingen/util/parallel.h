/// @file
/// @brief Thread counts and a parallel for loop.
#pragma once

#include <cstddef>
#include <functional>

namespace terraingen {

/// The number of threads a request resolves to: `threads` when positive, otherwise the
/// hardware thread count (at least 1).
unsigned ResolveThreads(unsigned threads);

/// Calls `task(i)` exactly once for every i in [0, `count`) on up to `threads` threads and
/// returns when all are done; tasks are handed out in order, so results must not depend on which
/// thread runs a task.
/// @param threads The thread count; 0 for every hardware thread.
/// @throws Whatever a task throws: the remaining tasks are skipped and the first exception is
/// rethrown.
void ParallelFor(std::size_t count, unsigned threads,
                 const std::function<void(std::size_t index)>& task);

}  // namespace terraingen
