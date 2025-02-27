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

#include "zc/core/function.h"
#include "zc/core/string.h"

namespace zomlang {
namespace compiler {
namespace basic {

struct TaskProcessInfo {
  pid_t pid;
  int32_t exitCode;            // 退出码字段
  int64_t cpuTimeUs;           // 用户态CPU时间（微秒）
  int64_t systemTimeUs;        // 内核态CPU时间（微秒）
  int64_t maxResidentSetKB;    // 最大常驻内存集（千字节）
  int64_t contextSwitchCount;  // 上下文切换次数
};

class TaskQueue {
public:
  enum class FlowControl {
    Continue,  // 继续执行后续任务
    Stop       // 停止队列执行
  };

  using TaskBeganCallback = zc::Function<void(uint64_t pid, void* ctx)>;
  using TaskFinishedCallback =
      zc::Function<FlowControl(uint64_t pid, int code, zc::StringPtr output, zc::StringPtr errors,
                               TaskProcessInfo info, void* ctx)>;
  using TaskSignalledCallback = zc::Function<FlowControl(pid_t pid, int sig, zc::StringPtr output,
                                                         zc::StringPtr errors, void* ctx)>;

  explicit TaskQueue(unsigned parallelism = 0);
  ~TaskQueue();

  // 添加任务到队列
  void addTask(zc::StringPtr exec, zc::ArrayPtr<const char*> args,
               zc::ArrayPtr<const char*> env = {}, void* ctx = nullptr,
               bool separateErrors = false);

  // 执行队列并返回是否全部完成
  bool execute(TaskBeganCallback began = nullptr, TaskFinishedCallback finished = nullptr,
               TaskSignalledCallback signalled = nullptr);

  // 状态查询
  bool hasPendingTasks() const;
  size_t runningCount() const;
  size_t completedCount() const;

private:
  class Impl;
  zc::Own<Impl> impl;
};

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang