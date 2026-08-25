#include "KeygenPool.h"

#include "../RandomHexKeyGenerator.h"
#include "util/cpu_affinity.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

namespace hashapi {
namespace {

constexpr int kMinThreads = 2;
constexpr int kMaxThreads = 16;

int online_cpus() {
#ifdef _WIN32
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    int n = static_cast<int>(si.dwNumberOfProcessors);
#else
    const long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    int n = static_cast<int>(ncpu);
#endif
    if (n < 1) n = 1;
    return n;
}

int clamp_threads(int requested) {
    const int ncpu = online_cpus();
    const int cap = std::max(kMinThreads, std::min(kMaxThreads, ncpu));
    int n = requested;
    // 0 = half the machine, not every CPU — flush/bag/CUDA host need the rest.
    if (n <= 0) n = std::max(kMinThreads, std::min(kMaxThreads, ncpu / 2));
    return std::max(kMinThreads, std::min(cap, n));
}

struct Job {
    char* dst = nullptr;
    std::size_t count = 0;
    std::size_t key_length = 64;
    const std::string* prefix = nullptr;
    std::atomic<int>* remaining = nullptr;
    std::mutex* done_mu = nullptr;
    std::condition_variable* done_cv = nullptr;
};

class KeygenPool {
public:
    static KeygenPool& instance() {
        static KeygenPool pool;
        return pool;
    }

    void configure(int threads) {
        const int n = clamp_threads(threads);
        if (started_ && n == threads_.load()) return;
        stop();
        threads_.store(n);
        start();
    }

    int threads() const { return threads_.load(); }

    void fill(char* arena, std::size_t count, std::size_t key_length, const std::string& prefix) {
        if (arena == nullptr || count == 0 || key_length == 0) return;
        if (!started_) configure(0);

        const int n = std::max(1, threads_.load());
        const std::size_t use =
            std::max<std::size_t>(1, std::min(static_cast<std::size_t>(n), count));

        std::atomic<int> remaining{static_cast<int>(use)};
        std::mutex done_mu;
        std::condition_variable done_cv;

        {
            std::lock_guard<std::mutex> lock(q_mu_);
            const std::size_t chunk = (count + use - 1) / use;
            for (std::size_t t = 0; t < use; ++t) {
                const std::size_t begin = t * chunk;
                if (begin >= count) {
                    remaining.fetch_sub(1, std::memory_order_relaxed);
                    continue;
                }
                const std::size_t end = std::min(count, begin + chunk);
                Job j;
                j.dst = arena + begin * key_length;
                j.count = end - begin;
                j.key_length = key_length;
                j.prefix = &prefix;
                j.remaining = &remaining;
                j.done_mu = &done_mu;
                j.done_cv = &done_cv;
                q_.push_back(j);
            }
        }
        q_cv_.notify_all();

        std::unique_lock<std::mutex> lock(done_mu);
        done_cv.wait(lock, [&] { return remaining.load(std::memory_order_acquire) <= 0; });
    }

    ~KeygenPool() { stop(); }

private:
    void start() {
        stop_ = false;
        workers_.reserve(static_cast<std::size_t>(threads_));
        for (int i = 0; i < threads_; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
        pin_workers();
        started_ = true;
    }

    void stop() {
        if (!started_) return;
        {
            std::lock_guard<std::mutex> lock(q_mu_);
            stop_ = true;
        }
        q_cv_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
        workers_.clear();
        started_ = false;
        stop_ = false;
    }

    void worker_loop() {
        xn::cpu::pin_this_thread(xn::cpu::Role::CudaHost);
        RandomHexKeyGenerator gen("", 64);
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(q_mu_);
                q_cv_.wait(lock, [&] { return stop_ || !q_.empty(); });
                if (stop_ && q_.empty()) return;
                if (q_.empty()) continue;
                job = q_.front();
                q_.pop_front();
            }
            if (job.dst != nullptr && job.count > 0 && job.prefix != nullptr) {
                gen.setPrefix(*job.prefix);
                gen.fillMany(job.dst, job.count);
            }
            if (job.remaining != nullptr &&
                job.remaining->fetch_sub(1, std::memory_order_acq_rel) == 1) {
                if (job.done_mu != nullptr && job.done_cv != nullptr) {
                    std::lock_guard<std::mutex> lock(*job.done_mu);
                    job.done_cv->notify_all();
                }
            }
        }
    }

    void pin_workers() {
        const int ncpu = online_cpus();
#ifdef _WIN32
        // Spread across whatever LPs this machine actually has (Vast 7800X3D,
        // desktop 9950X3D, SMT on or off). Do not assume a 12-LP first CCD.
        int i = 0;
        for (auto& t : workers_) {
            HANDLE h = reinterpret_cast<HANDLE>(t.native_handle());
            if (h == nullptr) continue;
            const int cpu = i % ncpu;
            DWORD_PTR mask = static_cast<DWORD_PTR>(1) << cpu;
            SetThreadAffinityMask(h, mask);
            ++i;
        }
#else
        int i = 0;
        for (auto& t : workers_) {
            cpu_set_t set;
            CPU_ZERO(&set);
            CPU_SET(i % ncpu, &set);
            pthread_setaffinity_np(t.native_handle(), sizeof(set), &set);
            ++i;
        }
#endif
    }

    std::mutex q_mu_;
    std::condition_variable q_cv_;
    std::deque<Job> q_;
    std::vector<std::thread> workers_;
    std::atomic<int> threads_{0};
    bool started_ = false;
    bool stop_ = false;
};

}  // namespace

void configureKeygenPool(int threads) { KeygenPool::instance().configure(threads); }

int keygenPoolThreads() { return KeygenPool::instance().threads(); }

void keygenFillFlat(char* arena, std::size_t count, std::size_t key_length,
                    const std::string& prefix) {
    KeygenPool::instance().fill(arena, count, key_length, prefix);
}

}  // namespace hashapi
