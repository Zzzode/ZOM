// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/diagnostics/consumer/terminal-diagnostic-renderer.h"

#include <cstdio>

#include "compiler/diagnostics/core/diagnostic-info.h"
#include "compiler/diagnostics/text/diagnostic-text.h"
#include "zc/core/debug.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::diagnostics {
namespace {

constexpr zc::StringPtr resetColor = "\033[0m"_zc;
constexpr zc::StringPtr grayColor = "\033[90m"_zc;
constexpr zc::StringPtr locationColor = "\033[1;34m"_zc;

constexpr zc::StringPtr severityColor(DiagSeverity severity) {
  switch (severity) {
    case DiagSeverity::kNote:
      return "\033[1;36m"_zc;
    case DiagSeverity::kRemark:
      return "\033[1;34m"_zc;
    case DiagSeverity::kWarning:
      return "\033[1;33m"_zc;
    case DiagSeverity::kError:
      return "\033[1;31m"_zc;
    case DiagSeverity::kFatal:
      return "\033[1;35m"_zc;
  }
  ZC_UNREACHABLE
}

zc::String diagnosticCode(DiagID id) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "ZOM%04u", static_cast<unsigned>(id));
  return zc::str(buffer);
}

void writeFormattedMessage(zc::OutputStream& output, zc::StringPtr format,
                           zc::ArrayPtr<const zc::String> arguments) {
  size_t cursor = 0;
  for (size_t index = 0; index < arguments.size(); ++index) {
    size_t placeholder = cursor;
    while (placeholder + 2 < format.size() &&
           !(format[placeholder] == '{' &&
             format[placeholder + 1] == static_cast<char>('0' + index) &&
             format[placeholder + 2] == '}')) {
      ++placeholder;
    }
    ZC_IREQUIRE(placeholder + 2 < format.size(), "diagnostic template does not match arguments");
    output.write(format.slice(cursor, placeholder).asBytes());
    output.write(arguments[index].asBytes());
    cursor = placeholder + 3;
  }
  output.write(format.slice(cursor).asBytes());
}

bool sameIdentity(const ResolvedDiagnosticLocation& left, const ResolvedDiagnosticLocation& right) {
  if (left.origin() != right.origin()) return false;
  if (left.origin() == DiagnosticFactOrigin::Document) {
    return left.documentIdentityBytes() == right.documentIdentityBytes();
  }
  return left.source().sameAs(right.source());
}

struct LineView final {
  size_t lineStart;
  size_t lineEnd;
  uint32_t line;
};

LineView lineForOffset(zc::ArrayPtr<const zc::byte> source, size_t offset) {
  size_t lineStart = 0;
  uint32_t line = 1;
  size_t cursor = 0;
  while (cursor < offset) {
    if (source[cursor] == '\r') {
      ++line;
      ++cursor;
      if (cursor < offset && source[cursor] == '\n') ++cursor;
      lineStart = cursor;
    } else if (source[cursor] == '\n') {
      ++line;
      lineStart = ++cursor;
    } else {
      ++cursor;
    }
  }
  size_t lineEnd = offset;
  while (lineEnd < source.size() && source[lineEnd] != '\n' && source[lineEnd] != '\r') {
    ++lineEnd;
  }
  return LineView{lineStart, lineEnd, line};
}

size_t escapedOffset(zc::ArrayPtr<const zc::byte> line, size_t offset) {
  return escapeDiagnosticText(line.first(offset)).size();
}

void renderHeader(zc::OutputStream& output, DiagID code, zc::ArrayPtr<const zc::String> arguments,
                  bool useColors) {
  const auto& info = getDiagnosticInfo(code);
  if (useColors) output.write(severityColor(info.severity).asBytes());
  output.write(zc::str(toString(info.severity)).asBytes());
  if (useColors) output.write(resetColor.asBytes());
  if (useColors) output.write(grayColor.asBytes());
  output.write(zc::str(" [", diagnosticCode(code), "]").asBytes());
  if (useColors) output.write(resetColor.asBytes());
  output.write(": "_zcb);
  writeFormattedMessage(output, info.message, arguments);
  output.write("\n"_zcb);
}

void renderLocation(zc::OutputStream& output, const DiagnosticPresentationLocation& location,
                    zc::ArrayPtr<const DiagnosticSourceRange> highlights, bool useColors) {
  const auto line = lineForOffset(location.source, location.range.byteStart);
  const auto lineBytes = location.source.slice(line.lineStart, line.lineEnd);
  const auto escapedLine = escapeDiagnosticText(lineBytes);
  const size_t primaryOffset = escapedOffset(lineBytes, location.range.byteStart - line.lineStart);
  const uint32_t column = static_cast<uint32_t>(primaryOffset + 1);

  if (useColors) output.write(locationColor.asBytes());
  output.write("  --> "_zcb);
  if (useColors) output.write(resetColor.asBytes());
  output.write(zc::str(location.displayName, ":", line.line, ":", column, "\n").asBytes());

  const auto lineNumber = zc::str(line.line);
  const size_t gutterWidth = lineNumber.size();
  const auto writeGutter = [&](bool numbered) {
    if (useColors) output.write(locationColor.asBytes());
    if (numbered) {
      output.write(lineNumber.asBytes());
    } else {
      for (size_t index = 0; index < gutterWidth; ++index) output.write(" "_zcb);
    }
    output.write(" | "_zcb);
    if (useColors) output.write(resetColor.asBytes());
  };

  writeGutter(false);
  output.write("\n"_zcb);
  writeGutter(true);
  output.write(escapedLine.asBytes());
  output.write("\n"_zcb);
  writeGutter(false);

  zc::Vector<char> markers(escapedLine.size() + 1);
  for (size_t index = 0; index <= escapedLine.size(); ++index) markers.add(' ');
  const auto mark = [&](const DiagnosticSourceRange& range) {
    if (range.byteStart < line.lineStart || range.byteStart > line.lineEnd) return;
    const size_t start = escapedOffset(lineBytes, range.byteStart - line.lineStart);
    size_t end = start + 1;
    if (range.isTokenRange && range.byteEnd > range.byteStart) {
      const size_t boundedEnd = range.byteEnd < line.lineEnd ? range.byteEnd : line.lineEnd;
      end = escapedOffset(lineBytes, boundedEnd - line.lineStart);
      if (end <= start) end = start + 1;
    }
    markers[start] = '^';
    for (size_t index = start + 1; index < end; ++index) markers[index] = '~';
  };
  mark(location.range);
  for (const auto& range : highlights) mark(range);
  while (markers.size() != 0 && markers[markers.size() - 1] == ' ') markers.removeLast();
  if (useColors) output.write(severityColor(DiagSeverity::kError).asBytes());
  output.write(markers.asPtr().asBytes());
  if (useColors) output.write(resetColor.asBytes());
  output.write("\n"_zcb);
}

struct ResolvedPresentation final {
  DiagnosticPresentationLocation primary;
  zc::Vector<DiagnosticPresentationLocation> related;
};

}  // namespace

zc::Maybe<TerminalDiagnosticRenderFailure> renderTerminalDiagnostics(
    const DiagnosticPolicyResult& diagnostics, const DiagnosticPresentationResolver& resolver,
    zc::OutputStream& output, bool useColors) {
  zc::Vector<ResolvedPresentation> presentations(diagnostics.displayedCount());
  for (size_t index = 0; index < diagnostics.displayedCount(); ++index) {
    const auto& diagnostic = diagnostics.displayed(index);
    auto primary = resolver.resolve(diagnostic.primary());
    if (primary == zc::none) {
      return TerminalDiagnosticRenderFailure::MissingPresentationAuthority;
    }
    if (ZC_ASSERT_NONNULL(primary).range.byteStart > ZC_ASSERT_NONNULL(primary).range.byteEnd ||
        ZC_ASSERT_NONNULL(primary).range.byteEnd > ZC_ASSERT_NONNULL(primary).source.size()) {
      return TerminalDiagnosticRenderFailure::InvalidPresentationRange;
    }
    zc::Vector<DiagnosticPresentationLocation> related(diagnostic.related().size());
    for (const auto& item : diagnostic.related()) {
      auto location = resolver.resolve(item.location);
      if (location == zc::none) {
        return TerminalDiagnosticRenderFailure::MissingPresentationAuthority;
      }
      if (ZC_ASSERT_NONNULL(location).range.byteStart > ZC_ASSERT_NONNULL(location).range.byteEnd ||
          ZC_ASSERT_NONNULL(location).range.byteEnd > ZC_ASSERT_NONNULL(location).source.size()) {
        return TerminalDiagnosticRenderFailure::InvalidPresentationRange;
      }
      related.add(ZC_ASSERT_NONNULL(location));
    }
    presentations.add(ResolvedPresentation{ZC_ASSERT_NONNULL(primary), zc::mv(related)});
  }

  zc::VectorOutputStream rendered;
  size_t warningCount = 0;
  for (size_t index = 0; index < diagnostics.displayedCount(); ++index) {
    const auto& diagnostic = diagnostics.displayed(index);
    const auto& presentation = presentations[index];
    if (diagnostic.severity() == DiagSeverity::kWarning) ++warningCount;
    renderHeader(rendered, diagnostic.code(), diagnostic.arguments(), useColors);
    zc::Vector<DiagnosticSourceRange> primaryHighlights;
    for (size_t relatedIndex = 0; relatedIndex < diagnostic.related().size(); ++relatedIndex) {
      const auto& related = diagnostic.related()[relatedIndex];
      if (related.role == DiagnosticSecondaryRole::Highlight &&
          sameIdentity(diagnostic.primary(), related.location)) {
        primaryHighlights.add(presentation.related[relatedIndex].range);
      }
    }
    renderLocation(rendered, presentation.primary, primaryHighlights.asPtr(), useColors);
    for (size_t relatedIndex = 0; relatedIndex < diagnostic.related().size(); ++relatedIndex) {
      const auto& related = diagnostic.related()[relatedIndex];
      if (related.role == DiagnosticSecondaryRole::Highlight) {
        if (!sameIdentity(diagnostic.primary(), related.location)) {
          renderLocation(rendered, presentation.related[relatedIndex], {}, useColors);
        }
        continue;
      }
      renderHeader(rendered, ZC_ASSERT_NONNULL(related.code), related.arguments.asPtr(), useColors);
      renderLocation(rendered, presentation.related[relatedIndex], {}, useColors);
    }
    rendered.write("\n"_zcb);
  }

  if (warningCount != 0 || diagnostics.errorCount() != 0) {
    if (warningCount != 0) {
      rendered.write(
          zc::str(warningCount, " warning", warningCount == 1 ? "" : "s", " generated").asBytes());
      if (diagnostics.errorCount() != 0) rendered.write(" and "_zcb);
    }
    if (diagnostics.errorCount() != 0) {
      rendered.write(zc::str(diagnostics.errorCount(), " error",
                             diagnostics.errorCount() == 1 ? "" : "s", " generated")
                         .asBytes());
    }
    rendered.write(".\n"_zcb);
  }
  if (diagnostics.omittedCount() != 0) {
    rendered.write(zc::str(diagnostics.omittedCount(), " diagnostic",
                           diagnostics.omittedCount() == 1 ? "" : "s", " omitted by policy.\n")
                       .asBytes());
  }
  output.write(rendered.getArray());
  return zc::none;
}

}  // namespace zomlang::compiler::diagnostics
