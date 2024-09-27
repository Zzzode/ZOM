#pragma once

#include <functional>
#include <queue>
#include <unordered_map>

#include "dependency_graph.h"

class TaskScheduler {
 public:
  TaskScheduler(DependencyGraph& graph);
  void schedule();

 private:
  void executeTask(int taskId);
  DependencyGraph& graph_;
};