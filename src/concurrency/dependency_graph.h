#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>

class Task;

class DependencyGraph {
 public:
  void addTask(std::shared_ptr<Task> task);
  void addDependency(int fromId, int toId);
  const std::unordered_map<int, std::unordered_set<int>>& getDependencies()
      const;
  const std::unordered_map<int, std::unordered_set<int>>& getDependents() const;
  std::shared_ptr<Task> getTask(int id) const;

 private:
  std::unordered_map<int, std::shared_ptr<Task>> tasks_;
  std::unordered_map<int, std::unordered_set<int>> dependencies_;
  std::unordered_map<int, std::unordered_set<int>> dependents_;
};