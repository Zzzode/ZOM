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

#include <thread>  // For std::thread::hardware_concurrency()

#include "zc/core/function.h"

ZC_BEGIN_HEADER

namespace zomlang {
namespace compiler {
namespace basic {

class ThreadPool {
public:
  explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
  ~ThreadPool() noexcept(false);

  /// Disallow copy and move operations
  ZC_DISALLOW_COPY_AND_MOVE(ThreadPool);

  /// Enqueue a task to the thread pool
  void enqueue(zc::Function<void()> task);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang

ZC_END_HEADER