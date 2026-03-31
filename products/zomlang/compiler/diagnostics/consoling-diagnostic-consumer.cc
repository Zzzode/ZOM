// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/diagnostics/consoling-diagnostic-consumer.h"

#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/core/io.h"
#include "zc/core/iostream.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/source/location.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace diagnostics {

namespace {

constexpr zc::StringPtr getColorForSeverity(DiagSeverity severity) {
  switch (severity) {
    case DiagSeverity::kNote:
      return "\033[1;36m"_zc;  // Cyan
    case DiagSeverity::kRemark:
      return "\033[1;34m"_zc;  // Blue
    case DiagSeverity::kWarning:
      return "\033[1;33m"_zc;  // Yellow
    case DiagSeverity::kError:
      return "\033[1;31m"_zc;  // Red
    case DiagSeverity::kFatal:
      return "\033[1;35m"_zc;  // Purple
    default:
      return "\033[0m"_zc;  // Default color (reset)
  }
}

constexpr zc::StringPtr RESET_COLOR = "\033[0m"_zc;
constexpr zc::StringPtr GRAY_COLOR = "\033[90m"_zc;

}  // namespace

// ================================================================================
// ConsolingDiagnosticConsumer::Impl

struct ConsolingDiagnosticConsumer::Impl {
  zc::std::StdOutputStream stdOut;
  zc::std::StdOutputStream stdErr;
  bool useColors;
  size_t numErrors = 0;
  size_t numWarnings = 0;

  explicit Impl(bool coloredOutput)
      : stdOut(::std::cout), stdErr(::std::cerr), useColors(coloredOutput) {}

  void printSourceLine(zc::OutputStream& output, const source::SourceManager& sm,
                       const Diagnostic& diagnostic, unsigned int lineNum);
};

void ConsolingDiagnosticConsumer::Impl::printSourceLine(zc::OutputStream& output,
                                                        const source::SourceManager& sm,
                                                        const Diagnostic& diagnostic,
                                                        unsigned int lineNum) {
  const source::SourceLoc loc = diagnostic.getLoc();
  if (!loc.isValid()) return;

  // 1. Get the source line range
  const source::BufferId bufferId = ZC_ASSERT_NONNULL(sm.findBufferContainingLoc(loc));
  const zc::ArrayPtr<const zc::byte> buffer = sm.getEntireTextForBuffer(bufferId);
  const zc::byte* bufStart = buffer.begin();
  const zc::byte* bufEnd = buffer.end();
  const zc::byte* ptr = loc.getOpaqueValue();

  // 2. Position the beginning and end of the line
  const zc::byte* lineStart = ptr;
  while (lineStart > bufStart && lineStart[-1] != '\n' && lineStart[-1] != '\r') { --lineStart; }

  const zc::byte* lineEnd = ptr;
  while (lineEnd < bufEnd && *lineEnd != '\n' && *lineEnd != '\r') { ++lineEnd; }

  // Calculate padding based on line number digits
  auto lineStr = zc::str(lineNum);
  const size_t padding = lineStr.size();

  auto printGutter = [&](bool showLineNum) {
    if (useColors) output.write("\033[1;34m"_zcb);  // Blue
    if (showLineNum) {
      output.write(lineStr.asBytes());
    } else {
      for (size_t i = 0; i < padding; ++i) output.write(" "_zcb);
    }
    output.write(" | "_zcb);
    if (useColors) output.write(RESET_COLOR.asBytes());
  };

  // 3. Print empty gutter line
  printGutter(false);
  output.write("\n"_zcb);

  // 4. Print source line
  printGutter(true);
  output.write(zc::ArrayPtr<const zc::byte>(lineStart, lineEnd - lineStart));
  output.write("\n"_zcb);

  // 5. Print underline/marker line
  printGutter(false);

  // Calculate and output the mark position
  const long column = ptr - lineStart;
  for (long i = 0; i < column; ++i) { output.write(" "_zcb); }

  if (useColors) output.write("\033[1;31m"_zcb);  // Red for error marker

  // Handle multi-range tags
  bool hasUnderline = false;
  if (diagnostic.getRanges().size() != 0) {
    zc::Vector<zc::ArrayPtr<const zc::byte>> underline;
    for (const auto& range : diagnostic.getRanges()) {
      const auto start = range.getStart().getOpaqueValue() - lineStart;
      const auto end = range.getEnd().getOpaqueValue() - lineStart;
      for (int i = 0; i < end - start; ++i) { underline.add((i == 0) ? "^"_zcb : "~"_zcb); }
    }
    if (underline.size() > 0) {
      output.write(underline.asPtr());
      hasUnderline = true;
    }
  }

  if (!hasUnderline) { output.write("^"_zcb); }

  if (useColors) output.write(RESET_COLOR.asBytes());
  output.write("\n"_zcb);
}

// ================================================================================
// ConsolingDiagnosticConsumer

ConsolingDiagnosticConsumer::ConsolingDiagnosticConsumer() : impl(zc::heap<Impl>(true)) {}
ConsolingDiagnosticConsumer::~ConsolingDiagnosticConsumer() noexcept(false) { printSummary(); }

void ConsolingDiagnosticConsumer::printSummary() {
  if (impl->numErrors == 0 && impl->numWarnings == 0) return;

  zc::std::StdOutputStream& output = impl->numErrors > 0 ? impl->stdErr : impl->stdOut;

  auto printCount = [&](size_t count, zc::StringPtr name, DiagSeverity colorSev) {
    if (impl->useColors) output.write(getColorForSeverity(colorSev).asBytes());
    output.write(zc::str(count).asBytes());
    if (impl->useColors) output.write(RESET_COLOR.asBytes());
    output.write(zc::str(" ", name, count > 1 ? "s" : "", " generated").asBytes());
  };

  if (impl->numWarnings > 0) {
    printCount(impl->numWarnings, "warning", DiagSeverity::kWarning);
    if (impl->numErrors > 0) output.write(" and "_zcb);
  }

  if (impl->numErrors > 0) { printCount(impl->numErrors, "error", DiagSeverity::kError); }

  output.write(".\n"_zcb);
}

void ConsolingDiagnosticConsumer::handleDiagnostic(const source::SourceManager& sm,
                                                   const Diagnostic& diagnostic) {
  const DiagnosticInfo& info = getDiagnosticInfo(diagnostic.getId());

  if (info.severity == DiagSeverity::kError || info.severity == DiagSeverity::kFatal) {
    impl->numErrors++;
  } else if (info.severity == DiagSeverity::kWarning) {
    impl->numWarnings++;
  }

  zc::std::StdOutputStream& output =
      info.severity >= DiagSeverity::kError ? impl->stdErr : impl->stdOut;

  // 1. Output diagnostic level (with color) + Error Code + Message
  if (impl->useColors) { output.write(getColorForSeverity(info.severity).asBytes()); }
  output.write(zc::str(toString(info.severity)).asBytes());

  if (impl->useColors) { output.write(RESET_COLOR.asBytes()); }

  // Output error code
  if (impl->useColors) { output.write(GRAY_COLOR.asBytes()); }
  output.write(zc::str(" [ZOM", static_cast<int>(diagnostic.getId()), "]").asBytes());
  if (impl->useColors) { output.write(RESET_COLOR.asBytes()); }
  output.write(": "_zcb);

  // Output formatted messages
  DiagnosticEngine::formatDiagnosticMessage(sm, output, info.message, diagnostic.getArgs());
  output.write("\n"_zcb);

  // 2. Output location information and source snippet (if any)
  if (diagnostic.getLoc().isValid()) {
    ZC_IF_SOME(bufferId, sm.findBufferContainingLoc(diagnostic.getLoc())) {
      auto lineAndCol = sm.getPresumedLineAndColumnForLoc(diagnostic.getLoc(), bufferId);

      // Arrow and Path
      if (impl->useColors) output.write("\033[1;34m"_zcb);  // Blue
      output.write("  --> "_zcb);
      if (impl->useColors) output.write(RESET_COLOR.asBytes());

      output.write(
          zc::str(sm.getIdentifierForBuffer(bufferId), ":", lineAndCol.line, ":", lineAndCol.column)
              .asBytes());
      output.write("\n"_zcb);

      // Source Snippet
      impl->printSourceLine(output, sm, diagnostic, lineAndCol.line);
    }
    else {
      // Fallback if buffer not found or other issues
      diagnostic.getLoc().print(output, sm);
      output.write("\n"_zcb);
    }
  }

  // Handle child diagnostics (related information)
  for (const auto& child : diagnostic.getChildDiagnostics()) { handleDiagnostic(sm, *child); }

  output.write("\n"_zcb);
}

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang
