#pragma once

#include "zc/async/async.h"
#include "zc/core/debug.h"
#include "zc/core/vector.h"

namespace zom {
namespace basic {

template <typename Input, typename Output>
class CompilerStage {
public:
  explicit CompilerStage(zc::TaskSet::ErrorHandler& error_handler) : tasks_(error_handler) {}

  virtual ~CompilerStage() = default;

  zc::Promise<void> process(Input input) {
    return zc::evalLater(
               [this, input = zc::mv(input)]() mutable { return this->ProcessImpl(zc::mv(input)); })
        .then([this](zc::Vector<Output>&& outputs) {
          for (auto& output : outputs) { output_queue_.add(zc::mv(output)); }
        })
        .eagerlyEvaluate([](zc::Exception&& e) { ZC_LOG(ERROR, "Error processing input", e); });
  }

  zc::Maybe<Output> GetOutput() {
    if (output_queue_.size() > 0) {
      auto output = zc::mv(output_queue_[0]);
      output_queue_.erase(output_queue_.begin());
      return zc::mv(output);
    }
    return zc::none;
  }

  zc::Promise<void> OnEmpty() { return tasks_.onEmpty(); }

protected:
  virtual zc::Promise<zc::Vector<Output>> ProcessImpl(Input input) = 0;

private:
  zc::TaskSet tasks_;
  zc::Vector<Output> output_queue_;
};

}  // namespace basic
}  // namespace zom
