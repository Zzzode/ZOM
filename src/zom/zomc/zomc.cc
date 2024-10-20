#include "src/zc/base/main.h"
#include "src/zc/strings/string.h"
#include "src/zom/basic/lang_options.h"
#include "src/zom/basic/pipeline.h"
#include "src/zom/diagnostics/diagnostic_engine.h"
#include "src/zom/source/manager.h"

class CompilerMain {
public:
  explicit CompilerMain(zc::ProcessContext& context)
      : context(context),
        lang_options_(),
        source_mgr_(),
        diag_engine_(source_mgr_),
        pipeline(lang_options_, source_mgr_, diag_engine_) {}

  zc::MainBuilder::Validity setInput(zc::StringPtr inputFile) {
    input = zc::heapString("int x = 5; float y = 3.14;");
    return true;
  }

  zc::MainBuilder::Validity run() {
    if (input.size() == 0) { return "No input provided"; }
    pipeline.Process(input);
    return true;
  }

  zc::MainBuilder::Validity showResults() {
    const zc::Vector<zc::String>& results = pipeline.results();
    for (const auto& result : results) {
      context.warning(result);  // 使用 warning 来输出结果
    }
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
  zom::basic::LangOptions lang_options_;
  zom::source::SourceManager source_mgr_;
  zom::diagnostics::DiagnosticEngine diag_engine_;
  zom::basic::CompilerPipeline pipeline;
  zc::String input;
};

ZC_MAIN(CompilerMain)
