/// @file
/// @brief A parallel loop on a temporary thread pool.
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace terraingen {

unsigned ResolveThreads(unsigned threads) {
    if (threads > 0) {
        return threads;
    }
    return std::max(1u, std::thread::hardware_concurrency());
}

void ParallelFor(std::size_t count, unsigned threads,
                 const std::function<void(std::size_t index)>& task) {
    const std::size_t workers = std::min<std::size_t>(ResolveThreads(threads), count);
    if (workers <= 1) {
        for (std::size_t i = 0; i < count; ++i) {
            task(i);
        }
        return;
    }
    std::atomic<std::size_t> next{0};
    std::atomic<bool> failed{false};
    std::exception_ptr error;
    std::mutex errorMutex;
    const auto work = [&] {
        while (!failed.load()) {
            const std::size_t i = next.fetch_add(1);
            if (i >= count) {
                return;
            }
            try {
                task(i);
            } catch (...) {
                const std::lock_guard lock(errorMutex);
                if (!error) {
                    error = std::current_exception();
                }
                failed.store(true);
            }
        }
    };
    std::vector<std::thread> pool;
    pool.reserve(workers - 1);
    for (std::size_t t = 1; t < workers; ++t) {
        pool.emplace_back(work);
    }
    work();
    for (std::thread& thread : pool) {
        thread.join();
    }
    if (error) {
        std::rethrow_exception(error);
    }
}

}  // namespace terraingen
