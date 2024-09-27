#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
 public:
  ThreadPool(size_t numThreads);
  ~ThreadPool();

  template <class F>
  void enqueue(F&& f);

 private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex queueMutex_;
  std::condition_variable condition_;
  bool stop_ = false;
};

template <class F>
void ThreadPool::enqueue(F&& f) {
  {
    std::unique_lock<std::mutex> lock(queueMutex_);
    tasks_.emplace(std::forward<F>(f));
  }
  condition_.notify_one();
}