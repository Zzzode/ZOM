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
    case DiagSeverity::Note:
      return "\033[1;36m";  // Cyan
    case DiagSeverity::Remark:
      return "\033[1;34m";  // Blue
    case DiagSeverity::Warning:
      return "\033[1;33m";  // Yellow
    case DiagSeverity::Error:
      return "\033[1;31m";  // Red
    case DiagSeverity::Fatal:
      return "\033[1;35m";  // Purple
    default:
      return "\033[0m";  // Default color (reset)
  }
}

constexpr zc::StringPtr RESET_COLOR = "\033[0m"_zc;

}  // namespace

// ================================================================================
// ConsolingDiagnosticConsumer::Impl

struct ConsolingDiagnosticConsumer::Impl {
  zc::std::StdOutputStream stdOut;
  zc::std::StdOutputStream stdErr;
  bool useColors;

  explicit Impl(bool coloredOutput)
      : stdOut(::std::cout), stdErr(::std::cerr), useColors(coloredOutput) {}

  void printSourceLine(zc::OutputStream& output, const source::SourceManager& sm,
                       const Diagnostic& diagnostic);
};

void ConsolingDiagnosticConsumer::Impl::printSourceLine(zc::OutputStream& output,
                                                        const source::SourceManager& sm,
                                                        const Diagnostic& diagnostic) {
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

  // 3. Output source code line
  const zc::String lineStr = zc::str(lineStart, lineEnd - lineStart);
  output.write(lineStr.asBytes());
  output.write("\n"_zcb);

  // 4. Calculate and output the mark position
  const long column = ptr - lineStart;
  // Align to the wrong position
  output.write(zc::str(column, ' ').asBytes());

  // 5. Handle multi-range tags
  if (diagnostic.getRanges().size() != 0) {
    zc::Vector<zc::ArrayPtr<const zc::byte>> underline;
    for (const auto& range : diagnostic.getRanges()) {
      const auto start = range.getStart().getOpaqueValue() - lineStart;
      const auto end = range.getEnd().getOpaqueValue() - lineStart;
      for (int i = 0; i < end - start; ++i) { underline.add((i == 0) ? "^"_zcb : "~"_zcb); }
    }
    output.write(underline.asPtr());
  } else {
    output.write("^~~~"_zcb);  // Default tags
  }

  output.write("\n"_zcb);
}

// ================================================================================
// ConsolingDiagnosticConsumer

ConsolingDiagnosticConsumer::ConsolingDiagnosticConsumer() : impl(zc::heap<Impl>(true)) {}
ConsolingDiagnosticConsumer::~ConsolingDiagnosticConsumer() noexcept(false) = default;

void ConsolingDiagnosticConsumer::handleDiagnostic(const source::SourceManager& sm,
                                                   const Diagnostic& diagnostic) {
  const DiagnosticInfo& info = getDiagnosticInfo(diagnostic.getId());
  zc::std::StdOutputStream& output =
      info.severity >= DiagSeverity::Error ? impl->stdErr : impl->stdOut;

  // Output location information (if any)
  if (diagnostic.getLoc().isValid()) {
    diagnostic.getLoc().print(output, sm);
    output.write(": "_zcb);
  }

  // Output diagnostic level (with color)
  if (impl->useColors) { output.write(getColorForSeverity(info.severity).asBytes()); }
  output.write(zc::str(toString(info.severity)).asBytes());

  // Output error code (gray)
  if (impl->useColors) { output.write("\033[90m"_zcb); }
  output.write(zc::str(" [ZOM", static_cast<int>(diagnostic.getId()), ']').asBytes());
  if (impl->useColors) { output.write(RESET_COLOR.asBytes()); }
  output.write(": "_zcb);

  // Output formatted messages
  DiagnosticEngine::formatDiagnosticMessage(output, info.message, diagnostic.getArgs());
  output.write("\n"_zcb);

  // Output source code context (if any)
  if (diagnostic.getLoc().isValid()) { impl->printSourceLine(output, sm, diagnostic); }

  output.write("\n"_zcb);
}

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang
