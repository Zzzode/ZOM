#include "task.h"

Task::Task(int id, TaskFunction func) : id_(id), func_(func) {}

int Task::getId() const { return id_; }

void Task::run() { func_(); }