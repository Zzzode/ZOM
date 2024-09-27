#pragma once

#include <functional>

class Task {
 public:
  using TaskFunction = std::function<void()>;

  Task(int id, TaskFunction func);
  int getId() const;
  void run();

 private:
  int id_;
  TaskFunction func_;
};