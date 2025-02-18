#include "zomlang/compiler/parser/parser.h"

namespace zomlang {
namespace compiler {
namespace parser {

// ================================================================================
// Parser::Impl
class Parser::Impl {
public:
  Impl(diagnostics::DiagnosticEngine& diagnosticEngine, uint64_t bufferId) noexcept;
  ~Impl() noexcept(false) = default;

  ZC_DISALLOW_COPY_AND_MOVE(Impl);

  void parse(zc::ArrayPtr<const lexer::Token> tokens);

private:
  uint64_t bufferId;
  diagnostics::DiagnosticEngine& diagnosticEngine;
};

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang