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

#include "zomlang/compiler/basic/task-queue.h"

#include <unistd.h>

#include <deque>

#include "zc/async/async-io.h"
#include "zc/async/async.h"
#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/core/exception.h"
#include "zc/core/mutex.h"
#include "zc/core/string.h"

namespace zomlang {
namespace compiler {
namespace basic {

struct TaskContext {
  zc::String exec;
  zc::Vector<zc::String> args;
  zc::Vector<zc::String> env;
  void* userCtx;
  bool separateErrors;

  // Process related information
  TaskProcessInfo procInfo;

  // Asynchronous I/O
  zc::Own<zc::AsyncIoStream> outPipe;
  zc::Own<zc::AsyncIoStream> errPipe;
  zc::String output;
  zc::String errors;
};

class TaskQueue::Impl : private zc::TaskSet::ErrorHandler {
public:
  Impl(unsigned parallelism, zc::EventLoop& extLoop)
      : maxParallelism(parallelism), taskSet(*this), loop(extLoop) {}
  ~Impl();

  void taskFailed(zc::Exception&& exception) override { handleError(zc::mv(exception)); }

  void addTask(zc::StringPtr exec, zc::ArrayPtr<const char*> args, zc::ArrayPtr<const char*> env,
               void* ctx, bool sep_err);

  bool execute(TaskBeganCallback began, TaskFinishedCallback finished,
               TaskSignalledCallback signalled);

  bool hasPending() const;

  zc::Promise<TaskProcessInfo> monitorProcess(pid_t pid);

  void handleError(zc::Exception&& e);

private:
  // Concurrency control
  const unsigned maxParallelism;

  zc::MutexGuarded<std::deque<zc::Own<TaskContext>>> pendingTasks;
  zc::TaskSet taskSet;

  // Async infrastructure
  zc::EventLoop& loop;

  // Callbacks
  TaskFinishedCallback onFinished;
  TaskSignalledCallback onSignalled;

  // Process launching logic
  zc::Promise<void> launchTask(zc::Own<TaskContext> task);
};

void TaskQueue::Impl::addTask(zc::StringPtr exec, zc::ArrayPtr<const char*> args,
                              zc::ArrayPtr<const char*> envs, void* ctx, bool sepErr) {
  auto task = zc::heap<TaskContext>();
  for (auto arg : args) task->args.add(zc::str(arg));
  for (auto env : envs) task->env.add(zc::str(env));

  taskSet.add(launchTask(zc::mv(task)));
}

zc::Promise<void> TaskQueue::Impl::launchTask(zc::Own<TaskContext> task) {
  return zc::evalLater([this, task = zc::mv(task)]() mutable {
    auto pipe = zc::newTwoWayPipe();

    // Capture all resources into lambda
    pid_t pid;
    ZC_SYSCALL(pid = fork());
    ZC_IREQUIRE(pid != -1, "Failed to fork process");

    if (pid == 0) {
      // Child process transfers data and releases parent resources
      auto exec = zc::mv(task->exec);
      auto args = zc::mv(task->args);

      pipe.ends[0]->abortRead();
      pipe.ends[0]->shutdownWrite();

      zc::Vector<const char*> argv;
      for (auto& arg : args) argv.add(arg.cStr());
      argv.add(nullptr);

      execvp(exec.cStr(), const_cast<char* const*>(argv.begin()));
      ZC_EXCEPTION(FAILED, "execvp failed: ", strerror(errno));
    }

    // 父进程设置进程信息
    task->procInfo.pid = pid;
    task->outPipe = zc::mv(pipe.ends[0]);

    // Parent process setup
    return task->outPipe->readAllText()
        .then([task = zc::mv(task)](zc::String text) mutable {
          task->output = zc::mv(text);
          return zc::mv(task);
        })
        .then([this](zc::Own<TaskContext> task) {
          auto pending = pendingTasks.lockExclusive();
          auto it = std::find_if(pending->begin(), pending->end(),
                                 [&](const auto& t) { return t.get() == task.get(); });
          if (it != pending->end()) { pending->erase(it); }

          return monitorProcess(task->procInfo.pid)
              .then([this, task = zc::mv(task)](TaskProcessInfo info) mutable {
                task->procInfo = info;
                onFinished(info.pid, info.exitCode, zc::mv(task->output), zc::mv(task->errors),
                           info, task->userCtx);
              });
        })
        .catch_([this](zc::Exception&& e) { handleError(zc::mv(e)); });
  });
}

bool TaskQueue::Impl::execute(TaskBeganCallback began = TaskBeganCallback(),
                              TaskFinishedCallback finished = TaskFinishedCallback(),
                              TaskSignalledCallback signalled = TaskSignalledCallback()) {
  auto waitScope = zc::WaitScope(loop);
  return taskSet.onEmpty()
      .then([this] {
        auto pending = pendingTasks.lockExclusive();
        if (!pending->empty()) { ZC_EXCEPTION(FAILED, "Some tasks were not executed"); }
        return true;
      })
      .wait(waitScope);
}

// 新增 monitorProcess 实现
zc::Promise<TaskProcessInfo> TaskQueue::Impl::monitorProcess(pid_t pid) {
  return zc::evalLater([this, pid] {
    try {
      int status;
      ZC_IREQUIRE(waitpid(pid, &status, 0) != -1, "waitpid failed");

      struct rusage usage;
      ZC_IREQUIRE(getrusage(RUSAGE_CHILDREN, &usage) == 0, "getrusage failed");

      return TaskProcessInfo{
          .pid = pid,
          .exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1,
          .cpuTimeUs = usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec,
          .systemTimeUs = usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec,
          .maxResidentSetKB = usage.ru_maxrss,
          .contextSwitchCount = usage.ru_nivcsw + usage.ru_nvcsw};
    } catch (zc::Exception& e) {
      handleError(zc::mv(e));
      return TaskProcessInfo{.pid = pid, .contextSwitchCount = 0};
    }
  });
}

void TaskQueue::Impl::handleError(zc::Exception&& e) {
  auto errorType = e.getType();
  const zc::StringPtr message = e.getDescription();

  // Handle process disconnection
  if (errorType == zc::Exception::Type::DISCONNECTED) {
    ZC_LOG(ERROR, "Child process disconnected: ", message);
    onSignalled(-1, SIGPIPE, "", "", nullptr);
    return;
  }

  // Handle system overload
  if (errorType == zc::Exception::Type::OVERLOADED) {
    ZC_LOG(WARNING, "System overload: ", message);
    auto pending = pendingTasks.lockExclusive();
    if (pending->size() > maxParallelism * 2) {
      // Limit queue size
      pending->resize(maxParallelism);
    }
    return;
  }

  // General error handling
  auto pending = pendingTasks.lockExclusive();
  if (!pending->empty()) {
    auto& task = pending->front();

    // Automatically release I/O resources
    task->outPipe = nullptr;
    task->errPipe = nullptr;

    // The callback notifies the task of failure
    onFinished(task->procInfo.pid, -1, zc::mv(task->output), zc::mv(task->errors), task->procInfo,
               task->userCtx);

    // Remove failed task
    pending->pop_front();
  }

  // Propagate unhandled exceptions
  if (errorType == zc::Exception::Type::UNIMPLEMENTED) {
    e.wrapContext(__FILE__, __LINE__, zc::str("Unhandled async task error"));
    throwRecoverableException(zc::mv(e));
  }
}

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang