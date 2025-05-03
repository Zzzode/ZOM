// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/basic/thread-pool.h"

#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/core/exception.h"
#include "zc/core/thread.h"

namespace zomlang {
namespace compiler {
namespace basic {

ThreadPool::ThreadPool(size_t numThreads) : workers(numThreads) {
  ZC_ASSERT(numThreads > 0);
  // Create and start worker threads
  for (size_t i = 0; i < numThreads; ++i) {
    workers.add(zc::heap<zc::Thread>([this] { this->workerLoop(); }));
  }
}

ThreadPool::~ThreadPool() {
  {
    auto lockedTasks = tasks.lockExclusive();
    stop = true;
    // Note: There is no explicit notify_all here. zc::_::Mutex::unlock handles waking up threads.
    // To ensure all waiting threads are awakened to check the stop flag,
    // we could potentially trigger it manually before unlock (if zc::Mutex supports it),
    // or rely on the default behavior of unlock. The implementation details of zc::Mutex determine
    // the best approach. Assuming unlock wakes up waiters.
  }  // Unlock mutex

  // Wait for all worker threads to finish
  // zc::Thread destructor automatically joins the thread
  workers.clear();  // Explicitly destroy and join threads
}

void ThreadPool::enqueue(zc::Function<void()> task) {
  {
    auto lockedTasks = tasks.lockExclusive();
    if (stop) {
      ZC_FAIL_ASSERT("enqueue on stopped ThreadPool");
      return;  // Or throw an exception
    }

    lockedTasks->add(arena.allocate<Task>(zc::mv(task)));
    // After adding the task, the unlock operation will automatically handle the wake-up logic
    // (depending on zc::Mutex implementation)
  }  // Unlock mutex and potentially wake up a waiting thread
}

void ThreadPool::workerLoop() {
  while (true) {
    zc::Maybe<Task&> taskToRun;
    {
      auto lockedTasks = tasks.lockExclusive();

      // Use a lambda that captures `this` (or `stop`) and accepts the locked task list
      lockedTasks.wait(
          [this](const auto& taskList) {
            // Check stop flag or if the task list (already locked) is not empty
            return stop || !taskList.empty();
          },
          zc::none);

      // Use lockedTasks directly as it's already locked
      if (stop && lockedTasks->empty()) { return; }  // Exit loop if stopping and no tasks left

      // Use lockedTasks directly
      if (!lockedTasks->empty()) {
        Task& taskRef = lockedTasks->front();  // Get task from the front (FIFO)
        // Assuming zc::List has remove(Task&) or similar.
        // If remove requires iterator, adjust accordingly.
        // Let's assume remove(Task&) exists for simplicity based on original code.
        lockedTasks->remove(taskRef);  // Remove the task from the list
        taskToRun = taskRef;           // Assign the task to be run
      }
    }  // Unlock mutex

    // If a task was successfully retrieved, execute it
    ZC_IF_SOME(task, taskToRun) {
      zc::Maybe<zc::Exception> exception = zc::runCatchingExceptions([&]() { task.func(); });
      ZC_IF_SOME(e, exception) {
        // Handle exceptions thrown during task execution, e.g., log the error
        ZC_LOG(ERROR, "Task executed with exception: ", e.getDescription());
      }
    }
  }
}

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang
