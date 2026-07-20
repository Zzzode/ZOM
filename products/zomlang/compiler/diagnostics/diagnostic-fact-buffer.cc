// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/diagnostics/diagnostic-fact-buffer.h"

#include "zc/core/debug.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/lexer/token.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::diagnostics {
namespace {

constexpr size_t kSourceErrorBudget = 100;

zc::String normalizeEmitterFile(const char* fileName) {
  const zc::StringPtr full(fileName == nullptr ? "" : fileName);
  ZC_IF_SOME(position, full.find("products/zomlang/"_zc)) { return zc::str(full.slice(position)); }
  ZC_IF_SOME(position, full.findLast('/')) { return zc::str(full.slice(position + 1)); }
  return zc::str(full);
}

zc::Vector<zc::String> retainArguments(zc::ArrayPtr<const DiagnosticArgument> arguments) {
  zc::Vector<zc::String> retained(arguments.size());
  for (const auto& argument : arguments) {
    ZC_SWITCH_ONEOF(argument) {
      ZC_CASE_ONEOF(text, zc::StringPtr) { retained.add(zc::str(text)); }
      ZC_CASE_ONEOF(text, zc::String) { retained.add(zc::str(text)); }
      ZC_CASE_ONEOF(token, lexer::Token) { retained.add(zc::str(token.getValue())); }
    }
  }
  return retained;
}

}  // namespace

struct DiagnosticFactBuffer::Impl final {
  class LaneEmitter final : public DiagnosticEmitter {
  public:
    LaneEmitter(Impl& owner, SourceDiagnosticPhase phase) : owner(owner), phase(phase) {}

    void emitDiagnostic(const Diagnostic& diagnostic, zc::SourceLocation emitter) override {
      owner.append(phase, diagnostic, emitter);
    }

  private:
    Impl& owner;
    SourceDiagnosticPhase phase;
  };

  struct ParserCheckpoint final {
    uint64_t id;
    size_t factCount;
    size_t errorCount;
  };

  Impl(const source::SourceManager& sources, const source::BufferId& buffer)
      : sources(sources),
        buffer(buffer),
        lexerEmitter(*this, SourceDiagnosticPhase::Lex),
        parserEmitter(*this, SourceDiagnosticPhase::Parse) {}

  zc::Maybe<uint64_t> offsetFor(source::SourceLoc location) const {
    if (location.isInvalid()) { return zc::none; }
    const auto sourceRange = sources.getRangeForBuffer(buffer);
    if (location < sourceRange.getStart() || location > sourceRange.getEnd()) { return zc::none; }
    return static_cast<uint64_t>(sources.getLocOffsetInBuffer(location, buffer));
  }

  zc::Maybe<DiagnosticFactRange> rangeFor(const source::CharSourceRange& range) const {
    auto start = offsetFor(range.getStart());
    auto end = offsetFor(range.getEnd());
    if (start == zc::none || end == zc::none || ZC_ASSERT_NONNULL(start) > ZC_ASSERT_NONNULL(end)) {
      return zc::none;
    }
    return DiagnosticFactRange{ZC_ASSERT_NONNULL(start), ZC_ASSERT_NONNULL(end),
                               range.getIsTokenRange()};
  }

  zc::Maybe<SecondaryDiagnosticFact> retainSecondary(const Diagnostic& diagnostic) const {
    if (diagnostic.getChildDiagnostics().size() != 0 || diagnostic.getFixIts().size() != 0) {
      return zc::none;
    }
    auto primary = offsetFor(diagnostic.getLoc());
    if (primary == zc::none || !isSourceSyntaxDiagnostic(diagnostic.getId())) { return zc::none; }
    zc::Vector<DiagnosticFactRange> ranges(diagnostic.getRanges().size());
    for (const auto& range : diagnostic.getRanges()) {
      auto retained = rangeFor(range);
      if (retained == zc::none) { return zc::none; }
      ranges.add(ZC_ASSERT_NONNULL(retained));
    }
    return SecondaryDiagnosticFact{diagnostic.getId(), ZC_ASSERT_NONNULL(primary),
                                   retainArguments(diagnostic.getArgs()), zc::mv(ranges)};
  }

  zc::Maybe<DiagnosticFact> retainFact(SourceDiagnosticPhase phase, const Diagnostic& diagnostic,
                                       zc::SourceLocation emitter) const {
    auto primary = offsetFor(diagnostic.getLoc());
    if (primary == zc::none || !isSourceSyntaxDiagnostic(diagnostic.getId())) { return zc::none; }

    zc::Vector<DiagnosticFactRange> ranges(diagnostic.getRanges().size());
    for (const auto& range : diagnostic.getRanges()) {
      auto retained = rangeFor(range);
      if (retained == zc::none) { return zc::none; }
      ranges.add(ZC_ASSERT_NONNULL(retained));
    }

    zc::Vector<DiagnosticFixItFact> fixIts(diagnostic.getFixIts().size());
    for (const auto& fixIt : diagnostic.getFixIts()) {
      auto retained = rangeFor(fixIt->range);
      if (retained == zc::none) { return zc::none; }
      fixIts.add(DiagnosticFixItFact{ZC_ASSERT_NONNULL(retained), zc::str(fixIt->replacementText)});
    }

    zc::Vector<SecondaryDiagnosticFact> secondary(diagnostic.getChildDiagnostics().size());
    for (const auto& child : diagnostic.getChildDiagnostics()) {
      auto retained = retainSecondary(*child);
      if (retained == zc::none) { return zc::none; }
      secondary.add(zc::mv(ZC_ASSERT_NONNULL(retained)));
    }

    return DiagnosticFact{phase,
                          normalizeEmitterFile(emitter.fileName),
                          zc::str(emitter.function == nullptr ? "" : emitter.function),
                          emitter.lineNumber,
                          emitter.columnNumber,
                          0,
                          diagnostic.getId(),
                          ZC_ASSERT_NONNULL(primary),
                          retainArguments(diagnostic.getArgs()),
                          zc::mv(ranges),
                          zc::mv(fixIts),
                          zc::mv(secondary)};
  }

  void append(SourceDiagnosticPhase phase, const Diagnostic& diagnostic, zc::SourceLocation emitter) {
    const bool isError = getDiagnosticInfo(diagnostic.getId()).severity >= DiagSeverity::kError;
    if (isError && errorCount() >= kSourceErrorBudget) { return; }
    auto retained = retainFact(phase, diagnostic, emitter);
    if (retained == zc::none) {
      reportInvariant(zc::str("diagnostic fact escaped its source buffer or syntax domain"));
      return;
    }
    if (phase == SourceDiagnosticPhase::Lex) {
      lexFacts.add(zc::mv(ZC_ASSERT_NONNULL(retained)));
      if (isError) { ++lexErrorCount; }
    } else {
      parserFacts.add(zc::mv(ZC_ASSERT_NONNULL(retained)));
      if (isError) { ++parserErrorCount; }
    }
  }

  size_t errorCount() const noexcept { return lexErrorCount + parserErrorCount; }

  void reportInvariant(zc::String&& message) {
    if (invariantMessage.size() == 0) { invariantMessage = zc::mv(message); }
  }

  const source::SourceManager& sources;
  source::BufferId buffer;
  LaneEmitter lexerEmitter;
  LaneEmitter parserEmitter;
  zc::Vector<DiagnosticFact> lexFacts;
  zc::Vector<DiagnosticFact> parserFacts;
  zc::Vector<ParserCheckpoint> checkpoints;
  size_t lexErrorCount = 0;
  size_t parserErrorCount = 0;
  uint64_t nextCheckpointId = 1;
  zc::String invariantMessage;
};

DiagnosticFactBuffer::DiagnosticFactBuffer(const source::SourceManager& sources,
                                           const source::BufferId& buffer)
    : impl(zc::heap<Impl>(sources, buffer)) {}

DiagnosticFactBuffer::~DiagnosticFactBuffer() noexcept(false) = default;
DiagnosticFactBuffer::DiagnosticFactBuffer(DiagnosticFactBuffer&&) noexcept = default;
DiagnosticFactBuffer& DiagnosticFactBuffer::operator=(DiagnosticFactBuffer&&) noexcept = default;

DiagnosticEmitter& DiagnosticFactBuffer::lexerEmitter() { return impl->lexerEmitter; }
DiagnosticEmitter& DiagnosticFactBuffer::parserEmitter() { return impl->parserEmitter; }

DiagnosticFactBuffer::Checkpoint DiagnosticFactBuffer::checkpoint() {
  const uint64_t id = impl->nextCheckpointId++;
  impl->checkpoints.add(Impl::ParserCheckpoint{id, impl->parserFacts.size(),
                                               impl->parserErrorCount});
  return Checkpoint{id};
}

void DiagnosticFactBuffer::commit(Checkpoint checkpoint) {
  ZC_IREQUIRE(impl->checkpoints.size() != 0 && impl->checkpoints.back().id == checkpoint.id,
              "diagnostic checkpoints must commit in stack order");
  impl->checkpoints.removeLast();
}

void DiagnosticFactBuffer::rollback(Checkpoint checkpoint) {
  ZC_IREQUIRE(impl->checkpoints.size() != 0 && impl->checkpoints.back().id == checkpoint.id,
              "diagnostic checkpoints must roll back in stack order");
  const auto retained = impl->checkpoints.back();
  impl->parserFacts.truncate(retained.factCount);
  impl->parserErrorCount = retained.errorCount;
  impl->checkpoints.removeLast();
}

bool DiagnosticFactBuffer::hasErrors() const noexcept { return impl->errorCount() != 0; }
size_t DiagnosticFactBuffer::errorCount() const noexcept { return impl->errorCount(); }
bool DiagnosticFactBuffer::hasInvariantViolation() const noexcept {
  return impl->invariantMessage.size() != 0;
}
zc::StringPtr DiagnosticFactBuffer::invariantMessage() const { return impl->invariantMessage; }
void DiagnosticFactBuffer::reportInvariant(zc::String&& message) {
  impl->reportInvariant(zc::mv(message));
}

zc::Vector<DiagnosticFact> DiagnosticFactBuffer::takeFactsCanonical() {
  ZC_IREQUIRE(impl->checkpoints.size() == 0,
              "diagnostic facts cannot publish with an open parser checkpoint");
  zc::Vector<DiagnosticFact> facts(impl->lexFacts.size() + impl->parserFacts.size());
  for (auto& fact : impl->lexFacts) { facts.add(zc::mv(fact)); }
  for (auto& fact : impl->parserFacts) { facts.add(zc::mv(fact)); }
  impl->lexFacts.clear();
  impl->parserFacts.clear();
  impl->lexErrorCount = 0;
  impl->parserErrorCount = 0;
  return canonicalizeDiagnosticFacts(zc::mv(facts));
}

}  // namespace zomlang::compiler::diagnostics
