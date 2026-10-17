#include "terraingen/util/parallel.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace terraingen {

namespace {

/// One `ParallelFor` call: its tasks, handed out in order to the caller and its helpers.
struct Job {
    std::size_t count = 0;
    const std::function<void(std::size_t)>* task = nullptr;
    std::atomic<std::size_t> next{0};
    std::atomic<int> slots{0};
    std::atomic<bool> failed{false};
    std::mutex mutex;
    std::condition_variable idle;
    int active = 0;
    std::exception_ptr error;

    /// Runs tasks until none are left or one has failed.
    void Work() {
        while (!failed.load()) {
            const std::size_t i = next.fetch_add(1);
            if (i >= count) {
                return;
            }
            try {
                (*task)(i);
            } catch (...) {
                const std::lock_guard lock(mutex);
                if (!error) {
                    error = std::current_exception();
                }
                failed.store(true);
            }
        }
    }

    /// Whether helpers have nothing left to take.
    bool Exhausted() const {
        return failed.load() || next.load() >= count;
    }
};

/// Threads kept for the life of the program, helping whichever jobs are waiting. The pool is
/// never destroyed: joining threads while statics are torn down can deadlock, as when a library is
/// unloaded on Windows, and the threads end with the process.
class Pool {
  public:
    Pool() {
        const unsigned size = std::max(1u, std::thread::hardware_concurrency()) - 1;
        for (unsigned t = 0; t < size; ++t) {
            std::thread([this] { Loop(); }).detach();
        }
    }

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    /// Offers `job` to the pool's threads.
    void Post(const std::shared_ptr<Job>& job) {
        {
            const std::lock_guard lock(mutex_);
            jobs_.push_back(job);
        }
        wake_.notify_all();
    }

  private:
    void Loop() {
        std::unique_lock lock(mutex_);
        while (true) {
            wake_.wait(lock, [this] { return !jobs_.empty(); });
            std::shared_ptr<Job> job = jobs_.front();
            if (job->Exhausted() || job->slots.fetch_sub(1) <= 0) {
                jobs_.pop_front();
                continue;
            }
            lock.unlock();
            {
                const std::lock_guard jobLock(job->mutex);
                ++job->active;
            }
            job->Work();
            {
                const std::lock_guard jobLock(job->mutex);
                --job->active;
            }
            job->idle.notify_all();
            lock.lock();
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::shared_ptr<Job>> jobs_;
};

Pool& SharedPool() {
    static Pool* const pool = new Pool;
    return *pool;
}

}  // namespace

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
    auto job = std::make_shared<Job>();
    job->count = count;
    job->task = &task;
    job->slots.store(static_cast<int>(workers - 1));
    SharedPool().Post(job);
    job->Work();
    {
        std::unique_lock lock(job->mutex);
        job->idle.wait(lock, [&] { return job->active == 0; });
    }
    job->slots.store(0);
    if (job->error) {
        std::rethrow_exception(job->error);
    }
}

}  // namespace terraingen
