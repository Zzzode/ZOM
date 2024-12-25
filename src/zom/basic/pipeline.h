#ifndef ZOM_BASIC_PIPELINE_H_
#define ZOM_BASIC_PIPELINE_H_

#include "src/zc/core/mutex.h"
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
  CompilerPipeline(const basic::LangOptions& options, source::SourceManager& source_mgr,
                   diagnostics::DiagnosticEngine& diags)
      : options(options),
        sourceMgr(source_mgr),
        diags(diags),
        lexer(options, source_mgr, diags),
        // parser(diags),
        // type_checker_(diags),
        stage(CompilationStage::kNotStarted) {}

  void process(const zc::String& input);
  ZC_NODISCARD const zc::Vector<zc::String>& getResults() const { return results; }
  ZC_NODISCARD CompilationStage getStage() const { return stage; }

private:
  ZC_UNUSED const LangOptions& options;
  source::SourceManager& sourceMgr;
  diagnostics::DiagnosticEngine& diags;

  lexer::Lexer lexer;
  // parser::Parser parser;
  // typecheck::TypeChecker type_checker_;

  // ir::IntermediateRepresentation ir_;
  zc::Vector<zc::String> results;
  CompilationStage stage;

  void runLexer();
  void runParser();
  void runTypeChecker();
  void generateResults();
};

}  // namespace basic
}  // namespace zom

#endif  // ZOM_BASIC_PIPELINE_H_
