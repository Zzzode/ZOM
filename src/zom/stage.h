#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

template <typename Input, typename Output>
class CompilerStage {
 protected:
  std::queue<Input> inputQueue;
  std::queue<Output> outputQueue;
  std::mutex inputMutex, outputMutex;
  std::condition_variable inputCV, outputCV;
  std::atomic<bool> done{false};
  std::thread workerThread;

  virtual void process(const Input& input, std::vector<Output>& outputs) = 0;

  void worker() {
    while (!done) {
      std::unique_lock<std::mutex> lock(inputMutex);
      inputCV.wait(lock, [this] { return !inputQueue.empty() || done; });

      if (!inputQueue.empty()) {
        Input input = zc::mv(inputQueue.front());
        inputQueue.pop();
        lock.unlock();

        std::vector<Output> outputs;
        process(input, outputs);

        std::unique_lock<std::mutex> outLock(outputMutex);
        for (auto& output : outputs) {
          outputQueue.push(zc::mv(output));
        }
        outLock.unlock();
        outputCV.notify_all();
      }
    }
  }

 public:
  CompilerStage() : workerThread(&CompilerStage::worker, this) {}

  virtual ~CompilerStage() {
    done = true;
    inputCV.notify_all();
    if (workerThread.joinable()) {
      workerThread.join();
    }
  }

  void pushInput(Input input) {
    std::unique_lock<std::mutex> lock(inputMutex);
    inputQueue.push(zc::mv(input));
    lock.unlock();
    inputCV.notify_one();
  }

  bool getOutput(Output& output) {
    std::unique_lock<std::mutex> lock(outputMutex);
    if (outputQueue.empty()) {
      return false;
    }
    output = zc::mv(outputQueue.front());
    outputQueue.pop();
    return true;
  }

  void setDone() {
    done = true;
    inputCV.notify_all();
  }
};