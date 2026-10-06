#pragma once

#include <cstddef>
#include <functional>

namespace terraingen {

/// The number of threads `threads` asks for: itself when positive, otherwise every hardware thread
/// (at least one).
unsigned ResolveThreads(unsigned threads);

/// Calls `task(i)` for every i in [0, `count`) on up to `threads` threads (0 for every hardware
/// thread), each task exactly once, and returns when all are done. Tasks are handed out in order as
/// threads free up, so results must not depend on which thread runs a task.
///
/// If tasks throw, the remaining tasks are skipped and the first exception is rethrown here.
void ParallelFor(std::size_t count, unsigned threads,
                 const std::function<void(std::size_t index)>& task);

}  // namespace terraingen
