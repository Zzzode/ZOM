// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/diagnostics/fact/source-diagnostic-draft-buffer.h"

#include "zc/core/debug.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/diagnostics/core/diagnostic-info.h"
#include "zomlang/compiler/diagnostics/core/diagnostic.h"
#include "zomlang/compiler/identity/key/source-key.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::diagnostics {
namespace {

constexpr size_t kSourceErrorBudget = 100;
constexpr uint64_t kMaximumSourceFacts = 4096;
constexpr uint64_t kMaximumEncodedBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumProvenanceEntries = 528384;
constexpr uint64_t kMaximumProvenanceComponentsPerKey = 3;
constexpr uint64_t kMaximumArgumentBytesPerRecord = 64 * 1024 * 1024;
constexpr uint64_t kMaximumSecondaryPerFact = 128;

struct DraftRange final {
  uint64_t byteStart;
  uint64_t byteEnd;
  bool isTokenRange;
};

struct DraftNote final {
  DiagID code;
  uint64_t primaryByteOffset;
  zc::Vector<zc::String> arguments;
};

struct SourceDiagnosticDraft final {
  SourceDiagnosticPhase phase;
  uint64_t primaryByteOffset;
  DiagID code;
  zc::Vector<zc::String> arguments;
  zc::Vector<DraftRange> ranges;
  zc::Vector<DraftNote> notes;
};

zc::Vector<zc::String> retainArguments(zc::ArrayPtr<const DiagnosticArgument> arguments) {
  zc::Vector<zc::String> retained(arguments.size());
  for (const auto& argument : arguments) {
    ZC_SWITCH_ONEOF(argument) {
      ZC_CASE_ONEOF(text, zc::StringPtr) { retained.add(zc::str(text)); }
      ZC_CASE_ONEOF(text, zc::String) { retained.add(zc::str(text)); }
    }
  }
  return retained;
}

template <typename T>
int compareScalar(const T& left, const T& right) {
  if (left < right) { return -1; }
  if (right < left) { return 1; }
  return 0;
}

int compareStrings(zc::ArrayPtr<const zc::String> left, zc::ArrayPtr<const zc::String> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    const int comparison = compareScalar(zc::StringPtr(left[index]), zc::StringPtr(right[index]));
    if (comparison != 0) { return comparison; }
  }
  return compareScalar(left.size(), right.size());
}

int compareRanges(zc::ArrayPtr<const DraftRange> left, zc::ArrayPtr<const DraftRange> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    int comparison = compareScalar(left[index].byteStart, right[index].byteStart);
    if (comparison != 0) { return comparison; }
    comparison = compareScalar(left[index].byteEnd, right[index].byteEnd);
    if (comparison != 0) { return comparison; }
    comparison = compareScalar(left[index].isTokenRange, right[index].isTokenRange);
    if (comparison != 0) { return comparison; }
  }
  return compareScalar(left.size(), right.size());
}

int compareNotes(zc::ArrayPtr<const DraftNote> left, zc::ArrayPtr<const DraftNote> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    int comparison = compareScalar(static_cast<uint32_t>(left[index].code),
                                   static_cast<uint32_t>(right[index].code));
    if (comparison != 0) { return comparison; }
    comparison = compareScalar(left[index].primaryByteOffset, right[index].primaryByteOffset);
    if (comparison != 0) { return comparison; }
    comparison = compareStrings(left[index].arguments.asPtr(), right[index].arguments.asPtr());
    if (comparison != 0) { return comparison; }
  }
  return compareScalar(left.size(), right.size());
}

int compareDrafts(const SourceDiagnosticDraft& left, const SourceDiagnosticDraft& right) {
  int comparison = compareScalar(left.primaryByteOffset, right.primaryByteOffset);
  if (comparison != 0) { return comparison; }
  comparison = compareScalar(static_cast<uint8_t>(left.phase), static_cast<uint8_t>(right.phase));
  if (comparison != 0) { return comparison; }
  comparison = compareScalar(static_cast<uint32_t>(left.code), static_cast<uint32_t>(right.code));
  if (comparison != 0) { return comparison; }
  comparison = compareStrings(left.arguments.asPtr(), right.arguments.asPtr());
  if (comparison != 0) { return comparison; }
  comparison = compareRanges(left.ranges.asPtr(), right.ranges.asPtr());
  if (comparison != 0) { return comparison; }
  return compareNotes(left.notes.asPtr(), right.notes.asPtr());
}

void sortDrafts(zc::Vector<SourceDiagnosticDraft>& drafts) {
  for (size_t index = 0; index < drafts.size(); ++index) {
    size_t smallest = index;
    for (size_t candidate = index + 1; candidate < drafts.size(); ++candidate) {
      if (compareDrafts(drafts[candidate], drafts[smallest]) < 0) { smallest = candidate; }
    }
    if (smallest != index) {
      SourceDiagnosticDraft retained = zc::mv(drafts[index]);
      drafts[index] = zc::mv(drafts[smallest]);
      drafts[smallest] = zc::mv(retained);
    }
  }
}

zc::Vector<uint32_t> primaryPath(uint32_t occurrence) {
  zc::Vector<uint32_t> result(2);
  result.add(occurrence);
  result.add(0);
  return result;
}

zc::Vector<uint32_t> secondaryPath(uint32_t occurrence, uint32_t slot, uint32_t index) {
  zc::Vector<uint32_t> result(3);
  result.add(occurrence);
  result.add(slot);
  result.add(index);
  return result;
}

SourceDiagnosticEmitter emitterFor(SourceDiagnosticPhase phase) {
  return phase == SourceDiagnosticPhase::Lex ? SourceDiagnosticEmitter::Lexer
                                             : SourceDiagnosticEmitter::Parser;
}

bool equalFacts(zc::ArrayPtr<const DiagnosticFact> left, zc::ArrayPtr<const DiagnosticFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (!(left[index] == right[index])) { return false; }
  }
  return true;
}

}  // namespace

struct PublishedSourceDiagnostics::Impl final {
  Impl(zc::Vector<DiagnosticFact>&& facts, SourceDiagnosticProvenanceMap&& provenance)
      : facts(zc::mv(facts)), provenance(zc::mv(provenance)) {}
  zc::Vector<DiagnosticFact> facts;
  SourceDiagnosticProvenanceMap provenance;
};

PublishedSourceDiagnostics::PublishedSourceDiagnostics(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
PublishedSourceDiagnostics::~PublishedSourceDiagnostics() noexcept(false) = default;
PublishedSourceDiagnostics::PublishedSourceDiagnostics(PublishedSourceDiagnostics&&) noexcept =
    default;
PublishedSourceDiagnostics& PublishedSourceDiagnostics::operator=(
    PublishedSourceDiagnostics&&) noexcept = default;
zc::ArrayPtr<const DiagnosticFact> PublishedSourceDiagnostics::facts() const {
  return impl->facts.asPtr();
}
const SourceDiagnosticProvenanceMap& PublishedSourceDiagnostics::provenance() const noexcept {
  return impl->provenance;
}
zc::Vector<DiagnosticFact> PublishedSourceDiagnostics::takeFacts() { return zc::mv(impl->facts); }
SourceDiagnosticProvenanceMap PublishedSourceDiagnostics::takeProvenance() {
  return zc::mv(impl->provenance);
}

struct SourceDiagnosticDraftBuffer::Impl final {
  class LaneEmitter final : public DiagnosticEmitter {
  public:
    LaneEmitter(Impl& owner, SourceDiagnosticPhase phase) : owner(owner), phase(phase) {}
    void emitDiagnostic(const Diagnostic& diagnostic, zc::SourceLocation) override {
      owner.append(phase, diagnostic);
    }

  private:
    Impl& owner;
    SourceDiagnosticPhase phase;
  };

  struct ParserCheckpoint final {
    uint64_t id;
    size_t draftCount;
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

  zc::Maybe<DraftRange> rangeFor(const source::CharSourceRange& range) const {
    auto start = offsetFor(range.getStart());
    auto end = offsetFor(range.getEnd());
    if (start == zc::none || end == zc::none || ZC_ASSERT_NONNULL(start) > ZC_ASSERT_NONNULL(end)) {
      return zc::none;
    }
    return DraftRange{ZC_ASSERT_NONNULL(start), ZC_ASSERT_NONNULL(end), range.getIsTokenRange()};
  }

  zc::Maybe<DraftNote> retainNote(const Diagnostic& diagnostic) const {
    if (diagnostic.getChildDiagnostics().size() != 0 || diagnostic.getRanges().size() != 0 ||
        !isSourceSyntaxDiagnostic(diagnostic.getId())) {
      return zc::none;
    }
    auto primary = offsetFor(diagnostic.getLoc());
    if (primary == zc::none) { return zc::none; }
    return DraftNote{diagnostic.getId(), ZC_ASSERT_NONNULL(primary),
                     retainArguments(diagnostic.getArgs())};
  }

  zc::Maybe<SourceDiagnosticDraft> retainDraft(SourceDiagnosticPhase phase,
                                               const Diagnostic& diagnostic) const {
    auto primary = offsetFor(diagnostic.getLoc());
    if (primary == zc::none || !isSourceSyntaxDiagnostic(diagnostic.getId())) { return zc::none; }
    zc::Vector<DraftRange> ranges(diagnostic.getRanges().size());
    for (const auto& range : diagnostic.getRanges()) {
      auto retained = rangeFor(range);
      if (retained == zc::none) { return zc::none; }
      ranges.add(ZC_ASSERT_NONNULL(retained));
    }
    zc::Vector<DraftNote> notes(diagnostic.getChildDiagnostics().size());
    for (const auto& child : diagnostic.getChildDiagnostics()) {
      auto retained = retainNote(*child);
      if (retained == zc::none) { return zc::none; }
      notes.add(zc::mv(ZC_ASSERT_NONNULL(retained)));
    }
    return SourceDiagnosticDraft{phase,
                                 ZC_ASSERT_NONNULL(primary),
                                 diagnostic.getId(),
                                 retainArguments(diagnostic.getArgs()),
                                 zc::mv(ranges),
                                 zc::mv(notes)};
  }

  void append(SourceDiagnosticPhase phase, const Diagnostic& diagnostic) {
    const bool isError = getDiagnosticInfo(diagnostic.getId()).severity >= DiagSeverity::kError;
    if (isError && errorCount() >= kSourceErrorBudget) { return; }
    auto retained = retainDraft(phase, diagnostic);
    if (retained == zc::none) {
      reportInvariant(zc::str("source diagnostic draft escaped its admitted topology"));
      return;
    }
    if (phase == SourceDiagnosticPhase::Lex) {
      lexDrafts.add(zc::mv(ZC_ASSERT_NONNULL(retained)));
      if (isError) { ++lexErrorCount; }
    } else {
      parserDrafts.add(zc::mv(ZC_ASSERT_NONNULL(retained)));
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
  zc::Vector<SourceDiagnosticDraft> lexDrafts;
  zc::Vector<SourceDiagnosticDraft> parserDrafts;
  zc::Vector<ParserCheckpoint> checkpoints;
  size_t lexErrorCount = 0;
  size_t parserErrorCount = 0;
  uint64_t nextCheckpointId = 1;
  zc::String invariantMessage;
};

SourceDiagnosticDraftBuffer::SourceDiagnosticDraftBuffer(const source::SourceManager& sources,
                                                         const source::BufferId& buffer)
    : impl(zc::heap<Impl>(sources, buffer)) {}
SourceDiagnosticDraftBuffer::~SourceDiagnosticDraftBuffer() noexcept(false) = default;
SourceDiagnosticDraftBuffer::SourceDiagnosticDraftBuffer(SourceDiagnosticDraftBuffer&&) noexcept =
    default;
SourceDiagnosticDraftBuffer& SourceDiagnosticDraftBuffer::operator=(
    SourceDiagnosticDraftBuffer&&) noexcept = default;
DiagnosticEmitter& SourceDiagnosticDraftBuffer::lexerEmitter() { return impl->lexerEmitter; }
DiagnosticEmitter& SourceDiagnosticDraftBuffer::parserEmitter() { return impl->parserEmitter; }

SourceDiagnosticDraftBuffer::Checkpoint SourceDiagnosticDraftBuffer::checkpoint() {
  const uint64_t id = impl->nextCheckpointId++;
  impl->checkpoints.add(
      Impl::ParserCheckpoint{id, impl->parserDrafts.size(), impl->parserErrorCount});
  return Checkpoint{id};
}
void SourceDiagnosticDraftBuffer::commit(Checkpoint checkpoint) {
  ZC_IREQUIRE(impl->checkpoints.size() != 0 && impl->checkpoints.back().id == checkpoint.id,
              "diagnostic checkpoints must commit in stack order");
  impl->checkpoints.removeLast();
}
void SourceDiagnosticDraftBuffer::rollback(Checkpoint checkpoint) {
  ZC_IREQUIRE(impl->checkpoints.size() != 0 && impl->checkpoints.back().id == checkpoint.id,
              "diagnostic checkpoints must roll back in stack order");
  const auto retained = impl->checkpoints.back();
  impl->parserDrafts.truncate(retained.draftCount);
  impl->parserErrorCount = retained.errorCount;
  impl->checkpoints.removeLast();
}
bool SourceDiagnosticDraftBuffer::hasErrors() const noexcept { return impl->errorCount() != 0; }
size_t SourceDiagnosticDraftBuffer::errorCount() const noexcept { return impl->errorCount(); }
bool SourceDiagnosticDraftBuffer::hasInvariantViolation() const noexcept {
  return impl->invariantMessage.size() != 0;
}
zc::StringPtr SourceDiagnosticDraftBuffer::invariantMessage() const {
  return impl->invariantMessage;
}
void SourceDiagnosticDraftBuffer::reportInvariant(zc::String&& message) {
  impl->reportInvariant(zc::mv(message));
}

zc::Maybe<PublishedSourceDiagnostics> SourceDiagnosticDraftBuffer::publish(
    const identity::SourceFileKey& source, uint64_t sourceByteLength) {
  if (impl->checkpoints.size() != 0 || impl->invariantMessage.size() != 0) {
    if (impl->invariantMessage.size() == 0) {
      impl->reportInvariant(zc::str("source diagnostic publication has an open checkpoint"));
    }
    return zc::none;
  }
  zc::Vector<SourceDiagnosticDraft> drafts(impl->lexDrafts.size() + impl->parserDrafts.size());
  for (auto& draft : impl->lexDrafts) { drafts.add(zc::mv(draft)); }
  for (auto& draft : impl->parserDrafts) { drafts.add(zc::mv(draft)); }
  sortDrafts(drafts);

  zc::Vector<DiagnosticFact> lexFacts;
  zc::Vector<DiagnosticFact> parseFacts;
  zc::Vector<SourceDiagnosticProvenanceEntry> lexEntries;
  zc::Vector<SourceDiagnosticProvenanceEntry> parseEntries;
  for (size_t draftIndex = 0; draftIndex < drafts.size(); ++draftIndex) {
    if (draftIndex > static_cast<size_t>(static_cast<uint32_t>(zc::maxValue))) {
      impl->reportInvariant(zc::str("source diagnostic occurrence exceeds uint32 capacity"));
      return zc::none;
    }
    auto& draft = drafts[draftIndex];
    const uint32_t occurrenceIndex = static_cast<uint32_t>(draftIndex);
    const auto emitter = emitterFor(draft.phase);
    auto occurrence =
        DiagnosticOccurrenceKey::from(source.clone(), draft.phase, emitter, occurrenceIndex);
    auto primary = DiagnosticProvenanceKey::from(source.clone(), draft.phase, emitter,
                                                 primaryPath(occurrenceIndex));
    if (occurrence == zc::none || primary == zc::none ||
        draft.primaryByteOffset > sourceByteLength) {
      impl->reportInvariant(zc::str("source diagnostic primary provenance is invalid"));
      return zc::none;
    }
    zc::Vector<DiagnosticSecondary> secondary(draft.ranges.size() + draft.notes.size());
    zc::Vector<SourceDiagnosticProvenanceEntry> entries(1 + draft.ranges.size() +
                                                        draft.notes.size());
    entries.add(SourceDiagnosticProvenanceEntry{
        ZC_ASSERT_NONNULL(primary).clone(),
        DiagnosticSourceRange{draft.primaryByteOffset, draft.primaryByteOffset, false}});
    for (size_t rangeIndex = 0; rangeIndex < draft.ranges.size(); ++rangeIndex) {
      if (rangeIndex > static_cast<size_t>(static_cast<uint32_t>(zc::maxValue))) {
        impl->reportInvariant(zc::str("source diagnostic highlight exceeds uint32 capacity"));
        return zc::none;
      }
      const auto& range = draft.ranges[rangeIndex];
      if (range.byteEnd > sourceByteLength) {
        impl->reportInvariant(zc::str("source diagnostic highlight is outside the source"));
        return zc::none;
      }
      auto key = DiagnosticProvenanceKey::from(
          source.clone(), draft.phase, emitter,
          secondaryPath(occurrenceIndex, 1, static_cast<uint32_t>(rangeIndex)));
      if (key == zc::none) {
        impl->reportInvariant(zc::str("source diagnostic highlight provenance is invalid"));
        return zc::none;
      }
      entries.add(SourceDiagnosticProvenanceEntry{
          ZC_ASSERT_NONNULL(key).clone(),
          DiagnosticSourceRange{range.byteStart, range.byteEnd, range.isTokenRange}});
      auto item = DiagnosticSecondary::highlight(zc::mv(ZC_ASSERT_NONNULL(key)));
      if (item == zc::none) {
        impl->reportInvariant(zc::str("source diagnostic highlight record is invalid"));
        return zc::none;
      }
      secondary.add(zc::mv(ZC_ASSERT_NONNULL(item)));
    }
    for (size_t noteIndex = 0; noteIndex < draft.notes.size(); ++noteIndex) {
      if (noteIndex > static_cast<size_t>(static_cast<uint32_t>(zc::maxValue))) {
        impl->reportInvariant(zc::str("source diagnostic note exceeds uint32 capacity"));
        return zc::none;
      }
      auto& note = draft.notes[noteIndex];
      if (note.primaryByteOffset > sourceByteLength) {
        impl->reportInvariant(zc::str("source diagnostic note is outside the source"));
        return zc::none;
      }
      auto key = DiagnosticProvenanceKey::from(
          source.clone(), draft.phase, emitter,
          secondaryPath(occurrenceIndex, 2, static_cast<uint32_t>(noteIndex)));
      if (key == zc::none) {
        impl->reportInvariant(zc::str("source diagnostic note provenance is invalid"));
        return zc::none;
      }
      entries.add(SourceDiagnosticProvenanceEntry{
          ZC_ASSERT_NONNULL(key).clone(),
          DiagnosticSourceRange{note.primaryByteOffset, note.primaryByteOffset, false}});
      auto item = DiagnosticSecondary::note(note.code, zc::mv(ZC_ASSERT_NONNULL(key)),
                                            zc::mv(note.arguments));
      if (item == zc::none) {
        impl->reportInvariant(zc::str("source diagnostic note record is invalid"));
        return zc::none;
      }
      secondary.add(zc::mv(ZC_ASSERT_NONNULL(item)));
    }
    auto fact = DiagnosticFact::from(zc::mv(ZC_ASSERT_NONNULL(occurrence)), draft.code,
                                     zc::mv(draft.arguments), zc::mv(ZC_ASSERT_NONNULL(primary)),
                                     zc::mv(secondary));
    if (fact == zc::none) {
      impl->reportInvariant(zc::str("source diagnostic fact is invalid"));
      return zc::none;
    }
    auto& targetFacts = draft.phase == SourceDiagnosticPhase::Lex ? lexFacts : parseFacts;
    auto& targetEntries = draft.phase == SourceDiagnosticPhase::Lex ? lexEntries : parseEntries;
    targetFacts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
    for (auto& entry : entries) { targetEntries.add(zc::mv(entry)); }
  }

  zc::Vector<DiagnosticFact> facts(lexFacts.size() + parseFacts.size());
  for (auto& fact : lexFacts) { facts.add(zc::mv(fact)); }
  for (auto& fact : parseFacts) { facts.add(zc::mv(fact)); }
  zc::Vector<SourceDiagnosticProvenanceEntry> entries(lexEntries.size() + parseEntries.size());
  for (auto& entry : lexEntries) { entries.add(zc::mv(entry)); }
  for (auto& entry : parseEntries) { entries.add(zc::mv(entry)); }
  auto provenance = SourceDiagnosticProvenanceMap::from(zc::mv(entries), sourceByteLength);
  if (provenance == zc::none ||
      !validateDiagnosticProvenance(facts.asPtr(), ZC_ASSERT_NONNULL(provenance))) {
    impl->reportInvariant(zc::str("source diagnostic provenance is not bijective"));
    return zc::none;
  }
  const DiagnosticFactCodecLimits factLimits{
      .maximumFacts = kMaximumSourceFacts,
      .maximumEncodedBytes = kMaximumEncodedBytes,
      .maximumProvenanceComponentsPerKey = kMaximumProvenanceComponentsPerKey,
      .maximumArgumentBytesPerRecord = kMaximumArgumentBytesPerRecord,
      .maximumSecondaryPerFact = kMaximumSecondaryPerFact,
  };
  const DiagnosticProvenanceCodecLimits provenanceLimits{
      .maximumEntries = kMaximumProvenanceEntries,
      .maximumEncodedBytes = kMaximumEncodedBytes,
      .maximumProvenanceComponentsPerKey = kMaximumProvenanceComponentsPerKey,
      .maximumSourceByteOffset = sourceByteLength,
  };
  auto factBytes = encodeDiagnosticFacts(zc::none, facts.asPtr(), factLimits);
  auto provenanceBytes =
      encodeSourceDiagnosticProvenance(zc::none, ZC_ASSERT_NONNULL(provenance), provenanceLimits);
  if (factBytes == zc::none || provenanceBytes == zc::none) {
    impl->reportInvariant(zc::str("source diagnostic publication exceeds codec limits"));
    return zc::none;
  }
  auto decodedFacts =
      decodeDiagnosticFacts(zc::none, ZC_ASSERT_NONNULL(factBytes).asPtr(), factLimits);
  auto decodedProvenance = decodeSourceDiagnosticProvenance(
      zc::none, ZC_ASSERT_NONNULL(provenanceBytes).asPtr(), sourceByteLength, provenanceLimits);
  if (decodedFacts == zc::none || decodedProvenance == zc::none ||
      !equalFacts(facts.asPtr(), ZC_ASSERT_NONNULL(decodedFacts).asPtr()) ||
      !(ZC_ASSERT_NONNULL(provenance) == ZC_ASSERT_NONNULL(decodedProvenance))) {
    impl->reportInvariant(zc::str("source diagnostic codec round trip failed"));
    return zc::none;
  }
  impl->lexDrafts.clear();
  impl->parserDrafts.clear();
  impl->lexErrorCount = 0;
  impl->parserErrorCount = 0;
  return PublishedSourceDiagnostics(zc::heap<PublishedSourceDiagnostics::Impl>(
      zc::mv(facts), zc::mv(ZC_ASSERT_NONNULL(provenance))));
}

}  // namespace zomlang::compiler::diagnostics
