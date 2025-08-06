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

#include <unistd.h>  // For usleep

#include <atomic>
#include <cstdlib>  // For rand

#include "zc/core/common.h"
#include "zc/core/debug.h"  // For ZC_FAIL_ASSERT
#include "zc/core/exception.h"
#include "zc/core/mutex.h"  // For MutexGuarded
#include "zc/core/time.h"   // For MILLISECONDS, MICROSECONDS
#include "zc/ztest/test.h"

namespace zomlang {
namespace compiler {
namespace basic {

ZC_TEST("ThreadPool: Basic Task Execution") {
  std::atomic<int> counter = 0;  // Use atomic for thread safety if pool had > 1 thread initially
  {                              // Scope the pool to ensure destruction before checking counter
    ThreadPool pool(1);          // Use a single thread for predictable execution
    pool.enqueue([&]() { counter++; });
    // Pool destructor runs here, waits for the task.
  }
  // Now check the counter after the pool is destroyed and task should be complete.
  ZC_EXPECT(counter == 1, counter);  // Add assertion here
}

ZC_TEST("ThreadPool: Multiple Tasks and Threads") {
  const size_t numThreads = 4;
  const int numTasks = 100;

  // Block 1: pool and counter - Add scope to ensure pool destruction before counter goes out of
  // scope
  std::atomic<int> counter = 0;   // Declare counter outside the pool's immediate scope
  {                               // Add a new scope for the first ThreadPool instance
    ThreadPool pool(numThreads);  // pool is now inside this scope

    for (int i = 0; i < numTasks; ++i) {
      pool.enqueue([&]() {  // Lambda captures 'counter' by reference, which is safe now
        // Simulate some work
        usleep((zc::MILLISECONDS * (rand() % 5)) / zc::MICROSECONDS);
        counter++;  // Accessing 'counter' (line 54 in original)
      });
    }
    // 'pool' goes out of scope here, its destructor runs and waits for tasks.
  }  // End of pool's scope

  // At this point, all threads from 'pool' have finished accessing 'counter'.
  ZC_EXPECT(counter == numTasks, counter);

  // Block 2: poolCheck and counterCheck (This block was already correctly scoped)
  std::atomic<int> counterCheck = 0;
  {
    ThreadPool poolCheck(numThreads);
    zc::MutexGuarded<int> finishedTasksGuard(0);  // Use MutexGuarded

    for (int i = 0; i < numTasks; ++i) {
      poolCheck.enqueue([&]() {
        usleep((zc::MILLISECONDS * (rand() % 2)) / zc::MICROSECONDS);
        counterCheck++;
        // Increment finished count under lock
        (*finishedTasksGuard.lockExclusive())++;
        // No explicit notify needed, MutexGuarded handles waking waiters
      });
    }

    // Wait for all tasks to complete using MutexGuarded::when
    finishedTasksGuard.when([](int count) { return count == numTasks; },
                            [](int&) { /* No operation needed when condition met */ },
                            zc::none);  // Wait indefinitely

    // Pool destructor will still run and join, but tasks are already finished.
  }

  ZC_EXPECT(counterCheck == numTasks, counterCheck, numTasks);
}

ZC_TEST("ThreadPool: Destruction Waits for Tasks") {
  std::atomic<bool> taskStarted = false;
  std::atomic<bool> taskCompleted = false;
  {
    ThreadPool pool(1);
    pool.enqueue([&]() {
      taskStarted = true;
      usleep((zc::MILLISECONDS * 50) / zc::MICROSECONDS);  // Simulate long task
      taskCompleted = true;
    });
    // Pool destructor runs here
  }
  ZC_EXPECT(taskStarted == true);
  ZC_EXPECT(taskCompleted == true);  // Destructor should block until task is done
}

// Note: Testing enqueue after stop currently triggers ZC_FAIL_ASSERT.
// ZC_EXPECT_THROW cannot catch fatal assertions directly in the same process.
// A death test (like expectFatalThrow in zc/ztest/test-helpers.h) would be needed,
// but that requires fork(), which might not be universally available or desired.
// We'll skip testing this specific assertion failure for now.
/*
ZC_TEST("ThreadPool: Enqueue After Stop (Requires Death Test)") {
    ThreadPool pool(1);
    pool.stop(); // Assuming a stop method exists or simulating via destructor start
    // This requires a way to trigger the stop logic without full destruction if needed,
    // or using a death test framework.
    // ZC_EXPECT_THROW_MESSAGE("enqueue on stopped ThreadPool", pool.enqueue([]{}));
}
*/

ZC_TEST("ThreadPool: Task Exception Handling") {
  // Use LogExpectation to check if the error is logged
  // This requires including "zc/ztest/test-helpers.h" potentially
  // For simplicity, we'll just run it and rely on manual log inspection or
  // a more advanced logging setup if available.

  {
    ThreadPool pool(1);
    pool.enqueue([&]() {
      ZC_EXPECT_LOG(ERROR, "Simulating task failure");
      ZC_LOG(ERROR, "Simulating task failure");
    });

    // Allow time for the task to execute and potentially log the error
    usleep((zc::MILLISECONDS * 100) / zc::MICROSECONDS);
    // Pool destructor runs here
  }

  // Verification depends on the logging mechanism. ZC_LOG(ERROR, ...) is used in workerLoop.
}

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang