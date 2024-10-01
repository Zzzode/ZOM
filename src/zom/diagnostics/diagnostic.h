#ifndef ZOM_DIAGNOSTIC_DIAGNOSTIC_H_
#define ZOM_DIAGNOSTIC_DIAGNOSTIC_H_

#include "src/zc/base/common.h"
#include "src/zc/containers/vector.h"
#include "src/zc/memory/memory.h"
#include "src/zc/strings/string.h"
#include "src/zom/source/location.h"

namespace zom {
namespace diagnostics {

enum class DiagnosticKind { kNote, kRemark, kWarning, kError, kFatal };

struct FixIt {
  zom::source::CharSourceRange range;
  zc::String replacement_text;
};

class Diagnostic {
 public:
  Diagnostic(DiagnosticKind kind, uint32_t id, zc::StringPtr message,
             const zom::source::CharSourceRange& location)
      : kind_(kind),
        id_(id),
        message_(zc::heapString(message)),
        location_(location) {}

  // 添加移动构造函数和移动赋值运算符
  Diagnostic(Diagnostic&& other) noexcept = default;
  Diagnostic& operator=(Diagnostic&& other) noexcept = default;

  ZC_DISALLOW_COPY(Diagnostic);

  DiagnosticKind kind() const { return kind_; }
  uint32_t id() const { return id_; }
  zc::StringPtr message() const { return message_; }
  const zom::source::CharSourceRange& source_range() const { return location_; }
  const zc::Vector<zc::Own<Diagnostic>>& child_diagnostics() const {
    return child_diagnostics_;
  }
  const zc::Vector<FixIt>& fix_its() const { return fix_its_; }

  void AddChildDiagnostic(zc::Own<Diagnostic> child);
  void AddFixIt(const FixIt& fix_it);
  void set_category(zc::StringPtr new_category) {
    category_ = zc::heapString(new_category);
  }

 private:
  DiagnosticKind kind_;
  uint32_t id_;
  zc::String message_;
  zom::source::CharSourceRange location_;
  zc::String category_;
  zc::Vector<zc::Own<Diagnostic>> child_diagnostics_;
  zc::Vector<FixIt> fix_its_;
};

class DiagnosticConsumer {
 public:
  virtual ~DiagnosticConsumer() = default;
  virtual void HandleDiagnostic(const source::SourceLoc& loc,
                                const Diagnostic& diagnostic) = 0;
};

}  // namespace diagnostics
}  // namespace zom

#endif  // ZOM_DIAGNOSTIC_DIAGNOSTIC_H_