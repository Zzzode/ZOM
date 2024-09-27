#include "task_scheduler.h"

#include <iostream>

#include "task.h"

TaskScheduler::TaskScheduler(DependencyGraph& graph) : graph_(graph) {}

void TaskScheduler::schedule(
    std::function<void(std::function<void()>)> executor) {
  std::unordered_map<int, int> inDegree;
  std::queue<int> readyTasks;

  // 计算每个任务的入度
  for (const auto& [taskId, deps] : graph_.getDependencies()) {
    inDegree[taskId] = deps.size();
  }

  // 将入度为 0 的任务加入准备队列
  for (const auto& [taskId, task] : graph_.getTasks()) {
    if (inDegree.find(taskId) == inDegree.end()) {
      readyTasks.push(taskId);
    }
  }

  // 调度任务
  while (!readyTasks.empty()) {
    int taskId = readyTasks.front();
    readyTasks.pop();
    executor([this, taskId] { executeTask(taskId); });

    // 更新依赖的任务
    for (int dependent : graph_.getDependents().at(taskId)) {
      inDegree[dependent]--;
      if (inDegree[dependent] == 0) {
        readyTasks.push(dependent);
      }
    }
  }
}

void TaskScheduler::executeTask(int taskId) {
  auto task = graph_.getTask(taskId);
  if (task) {
    task->run();
  }
}