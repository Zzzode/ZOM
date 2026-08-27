#include "zomlang/compiler/trace/trace-config.h"

#include <cstdlib>

#include "zc/core/string.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/trace/trace.h"

using namespace zomlang::compiler::trace;
using namespace zc;

namespace {

ZC_TEST("RuntimeConfig.ShouldEnableFromEnvironment") {
  setenv("ZOM_TRACE_ENABLED", "1", 1);
  ZC_ASSERT(RuntimeConfig::shouldEnableFromEnvironment());

  setenv("ZOM_TRACE_ENABLED", "true", 1);
  ZC_ASSERT(RuntimeConfig::shouldEnableFromEnvironment());

  setenv("ZOM_TRACE_ENABLED", "false", 1);
  ZC_ASSERT(!RuntimeConfig::shouldEnableFromEnvironment());

  unsetenv("ZOM_TRACE_ENABLED");
  ZC_ASSERT(!RuntimeConfig::shouldEnableFromEnvironment());
}

ZC_TEST("RuntimeConfig.GetCategoryMaskFromEnvironment") {
  setenv("ZOM_TRACE_CATEGORIES", "lexer,parser", 1);
  uint32_t mask = RuntimeConfig::getCategoryMaskFromEnvironment();
  ZC_ASSERT(mask == (static_cast<uint32_t>(TraceCategory::kLexer) |
                     static_cast<uint32_t>(TraceCategory::kParser)));

  setenv("ZOM_TRACE_CATEGORIES", "all", 1);
  mask = RuntimeConfig::getCategoryMaskFromEnvironment();
  ZC_ASSERT(mask == static_cast<uint32_t>(TraceCategory::kAll));

  unsetenv("ZOM_TRACE_CATEGORIES");
  mask = RuntimeConfig::getCategoryMaskFromEnvironment();
  ZC_ASSERT(mask == static_cast<uint32_t>(TraceCategory::kAll));
}

ZC_TEST("RuntimeConfig.GetOutputFileFromEnvironment") {
  setenv("ZOM_TRACE_OUTPUT", "trace.log", 1);
  const char* outputFile = RuntimeConfig::getOutputFileFromEnvironment();
  ZC_ASSERT(outputFile != nullptr);
  ZC_ASSERT(zc::str(outputFile) == "trace.log");

  unsetenv("ZOM_TRACE_OUTPUT");
  ZC_ASSERT(RuntimeConfig::getOutputFileFromEnvironment() == nullptr);
}

}  // namespace