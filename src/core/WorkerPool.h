#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// A small persistent worker pool for the simulation's independent per-participant work (movement, animation): run(n, f) calls f(i)
// for every i in [0, n) across the workers and the calling thread and returns when all are done. Each f(i) must touch only its own
// participant (plus read-only shared data), so the result does not depend on scheduling: the simulation stays deterministic.
// WFC_SIMTHREADS=<n> sets the worker count (0 = everything on the calling thread); default: hardware threads - 2, at most 6.
namespace core {

class WorkerPool {
public:
    static WorkerPool& get() { static WorkerPool p; return p; }
    int workers() const { return (int)threads_.size(); }

    void run(int n, const std::function<void(int)>& f) {
        if (n <= 0) return;
        if (threads_.empty() || n == 1) { for (int i = 0; i < n; ++i) f(i); return; }
        {
            std::lock_guard<std::mutex> lk(m_);
            job_ = &f; count_ = n; next_.store(0); pending_.store((int)threads_.size()); ++generation_;
        }
        cv_.notify_all();
        work();                                            // the caller takes indices too
        std::unique_lock<std::mutex> lk(m_);
        doneCv_.wait(lk, [&] { return pending_.load() == 0; });
        job_ = nullptr;
    }

    ~WorkerPool() {
        { std::lock_guard<std::mutex> lk(m_); quit_ = true; ++generation_; }
        cv_.notify_all();
        for (std::thread& t : threads_) t.join();
    }

private:
    WorkerPool() {
        int n = std::max(0, (int)std::thread::hardware_concurrency() - 2);
        n = std::min(n, 6);
        if (const char* e = std::getenv("WFC_SIMTHREADS")) n = std::max(0, std::min(16, std::atoi(e)));
        for (int i = 0; i < n; ++i) threads_.emplace_back([this] { loop(); });
    }
    void work() {
        for (int i = next_.fetch_add(1); i < count_; i = next_.fetch_add(1)) (*job_)(i);
    }
    void loop() {
        unsigned seen = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [&] { return generation_ != seen; });
                seen = generation_;
                if (quit_) return;
            }
            work();
            if (pending_.fetch_sub(1) == 1) { std::lock_guard<std::mutex> lk(m_); doneCv_.notify_one(); }
        }
    }
    std::vector<std::thread> threads_;
    std::mutex m_;
    std::condition_variable cv_, doneCv_;
    const std::function<void(int)>* job_ = nullptr;
    int count_ = 0;
    std::atomic<int> next_{0}, pending_{0};
    unsigned generation_ = 0;
    bool quit_ = false;
};

}  // namespace core
