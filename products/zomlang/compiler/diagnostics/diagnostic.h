#ifndef ZOM_DIAGNOSTIC_DIAGNOSTIC_H_
#define ZOM_DIAGNOSTIC_DIAGNOSTIC_H_

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {

enum class DiagnosticKind { kNote, kRemark, kWarning, kError, kFatal };

struct FixIt {
  CharSourceRange range;
  zc::String replacementText;
};

class Diagnostic {
public:
  Diagnostic(DiagnosticKind kind, uint32_t id, zc::StringPtr message,
             const CharSourceRange& location)
      : kind(kind), id(id), message(zc::heapString(message)), location(location) {}

  Diagnostic(Diagnostic&& other) noexcept = default;
  Diagnostic& operator=(Diagnostic&& other) noexcept = default;

  ZC_DISALLOW_COPY(Diagnostic);

  DiagnosticKind getKind() const { return kind; }
  uint32_t getId() const { return id; }
  zc::StringPtr getMessage() const { return message; }
  const CharSourceRange& getSourceRange() const { return location; }
  const zc::Vector<zc::Own<Diagnostic>>& getChildDiagnostics() const { return childDiagnostics; }
  const zc::Vector<FixIt>& getFixIts() const { return fixIts; }

  void addChildDiagnostic(zc::Own<Diagnostic> child);
  void addFixIt(const FixIt& fixIt);
  void setCategory(zc::StringPtr newCategory) { category = zc::heapString(newCategory); }

private:
  DiagnosticKind kind;
  uint32_t id;
  zc::String message;
  CharSourceRange location;
  zc::String category;
  zc::Vector<zc::Own<Diagnostic>> childDiagnostics;
  zc::Vector<FixIt> fixIts;
};

class DiagnosticConsumer {
public:
  virtual ~DiagnosticConsumer() = default;
  virtual void handleDiagnostic(const SourceLoc& loc, const Diagnostic& diagnostic) = 0;
};

}  // namespace compiler
}  // namespace zomlang

#endif  // ZOM_DIAGNOSTIC_DIAGNOSTIC_H_
