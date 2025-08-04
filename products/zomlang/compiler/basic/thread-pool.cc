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

#include "zc/core/arena.h"
#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/core/exception.h"
#include "zc/core/list.h"
#include "zc/core/mutex.h"
#include "zc/core/thread.h"
#include "zc/core/vector.h"

namespace zomlang {
namespace compiler {
namespace basic {

struct ThreadPool::Impl {
  zc::Arena arena;

  struct Task {
    zc::Function<void()> func;
    zc::ListLink<Task> link;

    explicit Task(zc::Function<void()> f) : func(zc::mv(f)) {}
  };

  zc::Vector<zc::Own<zc::Thread>> workers;
  zc::MutexGuarded<zc::List<Task, &Task::link>> tasks;
  bool stop = false;

  Impl(size_t numThreads) {
    ZC_ASSERT(numThreads > 0);
    for (size_t i = 0; i < numThreads; ++i) {
      workers.add(zc::heap<zc::Thread>([this] { workerLoop(); }));
    }
  }

  ~Impl() noexcept(false) {
    {  // Lock scope to set stop flag
      auto lockedTasks = tasks.lockExclusive();
      stop = true;
    }
    workers.clear();  // Join all threads
  }

  void enqueue(zc::Function<void()> task) {
    auto lockedTasks = tasks.lockExclusive();
    if (stop) {
      ZC_FAIL_ASSERT("enqueue on stopped ThreadPool");
      return;
    }
    lockedTasks->add(arena.allocate<Task>(zc::mv(task)));
  }

  void workerLoop() {
    while (true) {
      zc::Maybe<Task&> taskToRun;
      auto lockedTasks = tasks.lockExclusive();

      lockedTasks.wait([this](const auto& taskList) { return stop || !taskList.empty(); },
                       zc::none);

      if (stop && lockedTasks->empty()) { return; }

      if (!lockedTasks->empty()) {
        Task& taskRef = lockedTasks->front();
        lockedTasks->remove(taskRef);
        taskToRun = taskRef;
      }

      ZC_IF_SOME(taskRef, taskToRun) {
        zc::Maybe<zc::Exception> exception = zc::runCatchingExceptions([&]() { taskRef.func(); });
        ZC_IF_SOME(e, exception) {
          ZC_LOG(ERROR, "Task executed with exception: ", e.getDescription());
        }
      }
    }
  }
};

ThreadPool::ThreadPool(size_t numThreads) : impl(zc::heap<Impl>(numThreads)) {}

ThreadPool::~ThreadPool() noexcept(false) {}

void ThreadPool::enqueue(zc::Function<void()> task) { impl->enqueue(zc::mv(task)); }

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang
