#include "zc/core/main.h"
#include "zc/core/string.h"
#include "zomlang/compiler/basic/lang-options.h"
#include "zomlang/compiler/basic/pipeline.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"

class CompilerMain {
public:
  explicit CompilerMain(zc::ProcessContext& context)
      : context(context),
        langOptions(),
        sourceMgr(),
        diagEngine(sourceMgr),
        pipeline(langOptions, sourceMgr, diagEngine) {}

  zc::MainBuilder::Validity setInput(zc::StringPtr inputFile) {
    input = zc::heapString("int x = 5; float y = 3.14;");
    return true;
  }

  zc::MainBuilder::Validity run() {
    if (input.size() == 0) { return "No input provided"; }
    pipeline.process(input);
    return true;
  }

  zc::MainBuilder::Validity showResults() {
    const zc::Vector<zc::String>& results = pipeline.getResults();
    for (const auto& result : results) { context.warning(result); }
    return true;
  }

  zc::MainFunc getMain() {
    return zc::MainBuilder(context, "Compiler v1.0", "Processes input and shows results.")
        .addOptionWithArg({'i', "input"}, ZC_BIND_METHOD(*this, setInput), "<file>",
                          "Input file to process.")
        .callAfterParsing(ZC_BIND_METHOD(*this, run))
        .callAfterParsing(ZC_BIND_METHOD(*this, showResults))
        .build();
  }

private:
  zc::ProcessContext& context;
  zom::basic::LangOptions langOptions;
  zom::source::SourceManager sourceMgr;
  zom::diagnostics::DiagnosticEngine diagEngine;
  zom::basic::CompilerPipeline pipeline;
  zc::String input;
};

ZC_MAIN(CompilerMain)
