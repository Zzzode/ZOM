#ifndef ZOM_BASIC_PIPELINE_H_
#define ZOM_BASIC_PIPELINE_H_

#include "src/zc/concurrent/mutex.h"
#include "src/zom/lexer/lexer.h"
#include "src/zom/parser/parser.h"
#include "src/zom/typecheck/typechecker.h"

namespace zom {
namespace basic {

enum class CompilationStage {
  kNotStarted,
  kLexingComplete,
  kParsingComplete,
  kTypeCheckingComplete,
  kCompilationComplete
};

class CompilerPipeline {
 public:
  CompilerPipeline(const basic::LangOptions& options,
                   source::SourceManager& source_mgr,
                   diagnostics::DiagnosticEngine& diags)
      : options_(options),
        source_mgr_(source_mgr),
        diags_(diags),
        lexer_(options, source_mgr, diags),
        // parser_(diags),
        // type_checker_(diags),
        stage_(CompilationStage::kNotStarted) {}

  void Process(const zc::String& input);
  ZC_NODISCARD const zc::Vector<zc::String>& results() const {
    return results_;
  }
  ZC_NODISCARD CompilationStage stage() const { return stage_; }

 private:
  ZC_UNUSED const LangOptions& options_;
  source::SourceManager& source_mgr_;
  diagnostics::DiagnosticEngine& diags_;

  lexer::Lexer lexer_;
  // parser::Parser parser_;
  // typecheck::TypeChecker type_checker_;

  // ir::IntermediateRepresentation ir_;
  zc::Vector<zc::String> results_;
  CompilationStage stage_;

  void RunLexer();
  void RunParser();
  void RunTypeChecker();
  void GenerateResults();
};

}  // namespace basic
}  // namespace zom

#endif  // ZOM_BASIC_PIPELINE_H_