// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/diagnostics/fact/source-diagnostic-draft-buffer.h"

#include "compiler/diagnostics/core/diagnostic-info.h"
#include "compiler/identity/key/source-key.h"
#include "compiler/source/manager.h"
#include "zc/core/debug.h"

namespace zomlang::compiler::diagnostics {
namespace {

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
  uint64_t handleToken;
  SourceDiagnosticPhase phase;
  uint64_t primaryByteOffset;
  DiagID code;
  zc::Vector<zc::String> arguments;
  zc::Vector<DraftRange> ranges;
  zc::Vector<DraftNote> notes;
};

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

void swapDrafts(SourceDiagnosticDraft& left, SourceDiagnosticDraft& right) {
  SourceDiagnosticDraft retained = zc::mv(left);
  left = zc::mv(right);
  right = zc::mv(retained);
}

void siftDownDrafts(zc::Vector<SourceDiagnosticDraft>& drafts, size_t root, size_t count) {
  while (root < count / 2) {
    size_t child = root * 2 + 1;
    if (child + 1 < count && compareDrafts(drafts[child], drafts[child + 1]) < 0) { ++child; }
    if (compareDrafts(drafts[root], drafts[child]) >= 0) { return; }
    swapDrafts(drafts[root], drafts[child]);
    root = child;
  }
}

void sortDrafts(zc::Vector<SourceDiagnosticDraft>& drafts) {
  if (drafts.size() < 2) { return; }
  for (size_t root = drafts.size() / 2; root != 0; --root) {
    siftDownDrafts(drafts, root - 1, drafts.size());
  }
  for (size_t remaining = drafts.size(); remaining > 1; --remaining) {
    swapDrafts(drafts[0], drafts[remaining - 1]);
    siftDownDrafts(drafts, 0, remaining - 1);
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
  class LaneSink final : public SourceDiagnosticSink {
  public:
    LaneSink(Impl& owner, SourceDiagnosticPhase phase) : owner(owner), phase(phase) {}
    ZC_NODISCARD static SourceDiagnosticDraftHandle createHandle(uint64_t token) noexcept {
      return makeHandle(token);
    }
    ZC_NODISCARD static uint64_t token(SourceDiagnosticDraftHandle handle) noexcept {
      return handleToken(handle);
    }

    void addHighlight(SourceDiagnosticDraftHandle draft,
                      const source::CharSourceRange& range) override {
      owner.addHighlight(phase, draft, range);
    }

  private:
    SourceDiagnosticDraftHandle append(DiagID code, source::SourceLoc primary,
                                       zc::Vector<zc::String>&& arguments) override {
      return owner.append(phase, code, primary, zc::mv(arguments));
    }

    void appendNote(SourceDiagnosticDraftHandle draft, DiagID code, source::SourceLoc primary,
                    zc::Vector<zc::String>&& arguments) override {
      owner.addNote(phase, draft, code, primary, zc::mv(arguments));
    }

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
        lexerSink(*this, SourceDiagnosticPhase::Lex),
        parserSink(*this, SourceDiagnosticPhase::Parse) {}

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

  zc::Vector<SourceDiagnosticDraft>& lane(SourceDiagnosticPhase phase) {
    return phase == SourceDiagnosticPhase::Lex ? lexDrafts : parserDrafts;
  }

  static constexpr uint64_t kParserLaneBit = uint64_t{1} << 63;
  static constexpr uint64_t kGenerationMask = (uint64_t{1} << 31) - 1;
  static constexpr uint64_t kIndexMask = (uint64_t{1} << 32) - 1;

  SourceDiagnosticDraftHandle handleFor(SourceDiagnosticPhase phase, size_t index) {
    if (index >= kIndexMask || nextHandleGeneration > kGenerationMask) { return {}; }
    const uint64_t laneBit = phase == SourceDiagnosticPhase::Parse ? kParserLaneBit : 0;
    const uint64_t generation = nextHandleGeneration++;
    return LaneSink::createHandle(laneBit | (generation << 32) | static_cast<uint64_t>(index + 1));
  }

  zc::Maybe<size_t> indexFor(SourceDiagnosticPhase phase, SourceDiagnosticDraftHandle handle) {
    if (!handle.isValid()) { return zc::none; }
    const uint64_t token = LaneSink::token(handle);
    const bool parserLane = (token & kParserLaneBit) != 0;
    if (parserLane != (phase == SourceDiagnosticPhase::Parse)) { return zc::none; }
    const uint64_t encodedIndex = token & kIndexMask;
    if (encodedIndex == 0 || encodedIndex - 1 >= lane(phase).size()) { return zc::none; }
    const size_t index = static_cast<size_t>(encodedIndex - 1);
    if (lane(phase)[index].handleToken != token) { return zc::none; }
    return index;
  }

  SourceDiagnosticDraftHandle append(SourceDiagnosticPhase phase, DiagID code,
                                     source::SourceLoc location,
                                     zc::Vector<zc::String>&& arguments) {
    const bool validCode = isSourceSyntaxDiagnostic(code) && isKnownDiagnostic(code);
    if (!validCode || getDiagnosticInfo(code).argCount != arguments.size()) {
      reportInvariant(zc::str("source diagnostic draft escaped its admitted topology"));
      return {};
    }
    const bool isError = getDiagnosticInfo(code).severity >= DiagSeverity::kError;
    auto primary = offsetFor(location);
    if (primary == zc::none) {
      reportInvariant(zc::str("source diagnostic primary is outside the source buffer"));
      return {};
    }
    auto& drafts = lane(phase);
    const auto handle = handleFor(phase, drafts.size());
    if (!handle.isValid()) {
      reportInvariant(zc::str("source diagnostic draft capacity is exhausted"));
      return {};
    }
    drafts.add(SourceDiagnosticDraft{LaneSink::token(handle), phase, ZC_ASSERT_NONNULL(primary),
                                     code, zc::mv(arguments), zc::Vector<DraftRange>(),
                                     zc::Vector<DraftNote>()});
    if (isError) {
      if (phase == SourceDiagnosticPhase::Lex) {
        ++lexErrorCount;
      } else {
        ++parserErrorCount;
      }
    }
    return handle;
  }

  void addHighlight(SourceDiagnosticPhase phase, SourceDiagnosticDraftHandle handle,
                    const source::CharSourceRange& range) {
    auto index = indexFor(phase, handle);
    auto retained = rangeFor(range);
    if (index == zc::none || retained == zc::none) {
      reportInvariant(zc::str("source diagnostic highlight is invalid"));
      return;
    }
    lane(phase)[ZC_ASSERT_NONNULL(index)].ranges.add(ZC_ASSERT_NONNULL(retained));
  }

  void addNote(SourceDiagnosticPhase phase, SourceDiagnosticDraftHandle handle, DiagID code,
               source::SourceLoc location, zc::Vector<zc::String>&& arguments) {
    auto index = indexFor(phase, handle);
    auto primary = offsetFor(location);
    if (index == zc::none || primary == zc::none || !isSourceSyntaxDiagnostic(code) ||
        !isKnownDiagnostic(code) || getDiagnosticInfo(code).argCount != arguments.size()) {
      reportInvariant(zc::str("source diagnostic note is invalid"));
      return;
    }
    lane(phase)[ZC_ASSERT_NONNULL(index)].notes.add(
        DraftNote{code, ZC_ASSERT_NONNULL(primary), zc::mv(arguments)});
  }

  size_t errorCount() const noexcept { return lexErrorCount + parserErrorCount; }
  void reportInvariant(zc::String&& message) {
    if (invariantMessage.size() == 0) { invariantMessage = zc::mv(message); }
  }

  const source::SourceManager& sources;
  source::BufferId buffer;
  LaneSink lexerSink;
  LaneSink parserSink;
  zc::Vector<SourceDiagnosticDraft> lexDrafts;
  zc::Vector<SourceDiagnosticDraft> parserDrafts;
  zc::Vector<ParserCheckpoint> checkpoints;
  size_t lexErrorCount = 0;
  size_t parserErrorCount = 0;
  uint64_t nextCheckpointId = 1;
  uint64_t nextHandleGeneration = 1;
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
SourceDiagnosticSink& SourceDiagnosticDraftBuffer::lexerSink() { return impl->lexerSink; }
SourceDiagnosticSink& SourceDiagnosticDraftBuffer::parserSink() { return impl->parserSink; }

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
zc::Vector<ParserRecoveryDiagnostic> SourceDiagnosticDraftBuffer::copyParserRecoveryDiagnostics()
    const {
  zc::Vector<ParserRecoveryDiagnostic> result(impl->parserDrafts.size());
  for (const auto& draft : impl->parserDrafts) {
    zc::Vector<zc::String> arguments(draft.arguments.size());
    for (const auto& argument : draft.arguments) { arguments.add(zc::str(argument)); }
    result.add(ParserRecoveryDiagnostic{draft.code, draft.primaryByteOffset, zc::mv(arguments)});
  }
  return result;
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
