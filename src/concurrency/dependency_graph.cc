#include "src/concurrency/dependency_graph.h"

#include "task.h"

void DependencyGraph::addTask(std::shared_ptr<Task> task) {
  tasks_[task->getId()] = task;
}

void DependencyGraph::addDependency(int fromId, int toId) {
  dependencies_[toId].insert(fromId);
  dependents_[fromId].insert(toId);
}

const std::unordered_map<int, std::unordered_set<int>>&
DependencyGraph::getDependencies() const {
  return dependencies_;
}

const std::unordered_map<int, std::unordered_set<int>>&
DependencyGraph::getDependents() const {
  return dependents_;
}

std::shared_ptr<Task> DependencyGraph::getTask(int id) const {
  auto it = tasks_.find(id);
  return it != tasks_.end() ? it->second : nullptr;
}