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

#pragma once

#include "zc/core/arena.h"
#include "zc/core/function.h"
#include "zc/core/list.h"
#include "zc/core/memory.h"
#include "zc/core/mutex.h"
#include "zc/core/thread.h"

ZC_BEGIN_HEADER

namespace zomlang {
namespace compiler {
namespace basic {

class ThreadPool {
public:
  explicit ThreadPool(size_t numThreads);
  ~ThreadPool();

  /// Disallow copy and move operations
  ZC_DISALLOW_COPY_AND_MOVE(ThreadPool);

  /// Enqueue a task to the thread pool
  void enqueue(zc::Function<void()> task);

private:
  /// Memory pool for storing tasks
  zc::Arena arena;

  /// Task structure, containing the task function and ListLink
  struct Task {
    zc::Function<void()> func;
    zc::ListLink<Task> link;

    explicit Task(zc::Function<void()> f) : func(zc::mv(f)) {}
  };

  /// Function executed by worker threads
  void workerLoop();

  /// Worker threads
  zc::Vector<zc::Own<zc::Thread>> workers;

  /// Mutex guarding the task queue and stop flag
  zc::MutexGuarded<zc::List<Task, &Task::link>> tasks;

  /// Stop flag
  bool stop = false;
};

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang

ZC_END_HEADER