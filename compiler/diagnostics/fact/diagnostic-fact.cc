// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/diagnostics/fact/diagnostic-fact.h"

#include "compiler/diagnostics/core/diagnostic-info.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/key/source-key.h"
#include "zc/core/debug.h"
#include "zc/core/one-of.h"

namespace zomlang::compiler::diagnostics {

class DiagnosticFactCodecAccess final {
public:
  static zc::Maybe<DiagnosticOccurrenceKey> binderOccurrence(
      identity::ModuleKey&& module, identity::SourceFileKey&& source,
      BinderDiagnosticProducer producer, zc::Maybe<zc::Array<uint8_t>>&& semanticOwner,
      BinderDiagnosticEmitter emitter, zc::Vector<uint32_t>&& syntaxPath) {
    return DiagnosticOccurrenceKey::binder(zc::mv(module), zc::mv(source), producer,
                                           zc::mv(semanticOwner), emitter, zc::mv(syntaxPath));
  }

  static zc::Maybe<DiagnosticProvenanceKey> binderProvenance(
      identity::ModuleKey&& module, identity::SourceFileKey&& source,
      zc::Maybe<zc::Array<uint8_t>>&& semanticOwner, BinderDiagnosticEmitter emitter,
      zc::Vector<uint32_t>&& syntaxPath) {
    return DiagnosticProvenanceKey::binderModuleSite(
        zc::mv(module), zc::mv(source), zc::mv(semanticOwner), emitter, zc::mv(syntaxPath));
  }
};

namespace {

constexpr zc::StringPtr kDiagnosticFactsDomain = "zom.diagnostic-facts"_zc;
constexpr zc::StringPtr kSourceProvenanceDomain = "zom.source-diagnostic-provenance"_zc;
constexpr uint64_t kMinimumFactBytes = 56;
constexpr uint64_t kMinimumProvenanceEntryBytes = 51;
constexpr uint64_t kMinimumStringBytes = 8;
constexpr uint64_t kMinimumSecondaryBytes = 20;

enum class ModuleDiagnosticKind : uint8_t { IdentityAdmission = 0x01, Binder = 0x02 };

bool validPhaseEmitter(SourceDiagnosticPhase phase, SourceDiagnosticEmitter emitter) {
  return (phase == SourceDiagnosticPhase::Lex && emitter == SourceDiagnosticEmitter::Lexer) ||
         (phase == SourceDiagnosticPhase::Parse && emitter == SourceDiagnosticEmitter::Parser);
}

bool validPath(zc::ArrayPtr<const uint32_t> path) {
  if (path.size() == 2) { return path[1] == 0; }
  return path.size() == 3 && (path[1] == 1 || path[1] == 2);
}

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

template <typename T>
int compareScalar(T left, T right) {
  if (left < right) { return -1; }
  if (right < left) { return 1; }
  return 0;
}

int compareOccurrence(const DiagnosticOccurrenceKey& left, const DiagnosticOccurrenceKey& right) {
  int comparison =
      compareScalar(static_cast<uint8_t>(left.origin()), static_cast<uint8_t>(right.origin()));
  if (comparison != 0) { return comparison; }
  if (left.origin() == DiagnosticFactOrigin::Source) {
    const auto leftSource = left.source().encode();
    const auto rightSource = right.source().encode();
    comparison = compareBytes(leftSource.asPtr(), rightSource.asPtr());
    if (comparison != 0) { return comparison; }
    comparison =
        compareScalar(static_cast<uint8_t>(left.phase()), static_cast<uint8_t>(right.phase()));
    if (comparison != 0) { return comparison; }
    comparison =
        compareScalar(static_cast<uint8_t>(left.emitter()), static_cast<uint8_t>(right.emitter()));
    if (comparison != 0) { return comparison; }
    return compareScalar(left.occurrence(), right.occurrence());
  }
  const auto leftModule = left.module().encode();
  const auto rightModule = right.module().encode();
  comparison = compareBytes(leftModule.asPtr(), rightModule.asPtr());
  if (comparison != 0) { return comparison; }
  comparison = compareScalar(
      static_cast<uint8_t>(left.isIdentityAdmission() ? ModuleDiagnosticKind::IdentityAdmission
                                                      : ModuleDiagnosticKind::Binder),
      static_cast<uint8_t>(right.isIdentityAdmission() ? ModuleDiagnosticKind::IdentityAdmission
                                                       : ModuleDiagnosticKind::Binder));
  if (comparison != 0) { return comparison; }
  if (left.isBinder()) {
    comparison = compareScalar(static_cast<uint8_t>(left.binderProducer()),
                               static_cast<uint8_t>(right.binderProducer()));
    if (comparison != 0) { return comparison; }
    comparison = compareBytes(left.binderSemanticOwnerBytes(), right.binderSemanticOwnerBytes());
    if (comparison != 0) { return comparison; }
    comparison = compareScalar(static_cast<uint8_t>(left.binderEmitter()),
                               static_cast<uint8_t>(right.binderEmitter()));
    if (comparison != 0) { return comparison; }
    const auto leftSource = left.source().encode();
    const auto rightSource = right.source().encode();
    comparison = compareBytes(leftSource.asPtr(), rightSource.asPtr());
    if (comparison != 0) { return comparison; }
    const auto leftPath = left.binderSyntaxPath();
    const auto rightPath = right.binderSyntaxPath();
    const size_t common = leftPath.size() < rightPath.size() ? leftPath.size() : rightPath.size();
    for (size_t index = 0; index < common; ++index) {
      comparison = compareScalar(leftPath[index], rightPath[index]);
      if (comparison != 0) { return comparison; }
    }
    return compareScalar(leftPath.size(), rightPath.size());
  }
  comparison = compareScalar(static_cast<uint8_t>(left.identityPhase()),
                             static_cast<uint8_t>(right.identityPhase()));
  if (comparison != 0) { return comparison; }
  comparison = compareScalar(static_cast<uint8_t>(left.identityEmitter()),
                             static_cast<uint8_t>(right.identityEmitter()));
  if (comparison != 0) { return comparison; }
  const auto leftSource = left.source().encode();
  const auto rightSource = right.source().encode();
  comparison = compareBytes(leftSource.asPtr(), rightSource.asPtr());
  if (comparison != 0) { return comparison; }
  const auto leftPath = left.identitySyntaxPath();
  const auto rightPath = right.identitySyntaxPath();
  const size_t common = leftPath.size() < rightPath.size() ? leftPath.size() : rightPath.size();
  for (size_t index = 0; index < common; ++index) {
    comparison = compareScalar(leftPath[index], rightPath[index]);
    if (comparison != 0) { return comparison; }
  }
  return compareScalar(leftPath.size(), rightPath.size());
}

int compareProvenance(const DiagnosticProvenanceKey& left, const DiagnosticProvenanceKey& right) {
  int comparison =
      compareScalar(static_cast<uint8_t>(left.origin()), static_cast<uint8_t>(right.origin()));
  if (comparison != 0) { return comparison; }
  const auto leftSource = left.source().encode();
  const auto rightSource = right.source().encode();
  comparison = compareBytes(leftSource.asPtr(), rightSource.asPtr());
  if (comparison != 0) { return comparison; }
  if (left.origin() == DiagnosticFactOrigin::Source) {
    comparison =
        compareScalar(static_cast<uint8_t>(left.phase()), static_cast<uint8_t>(right.phase()));
    if (comparison != 0) { return comparison; }
    comparison =
        compareScalar(static_cast<uint8_t>(left.emitter()), static_cast<uint8_t>(right.emitter()));
    if (comparison != 0) { return comparison; }
  } else {
    const auto leftModule = left.module().encode();
    const auto rightModule = right.module().encode();
    comparison = compareBytes(leftModule.asPtr(), rightModule.asPtr());
    if (comparison != 0) { return comparison; }
    comparison = compareScalar(
        static_cast<uint8_t>(left.isIdentitySyntaxSite() ? ModuleDiagnosticKind::IdentityAdmission
                                                         : ModuleDiagnosticKind::Binder),
        static_cast<uint8_t>(right.isIdentitySyntaxSite() ? ModuleDiagnosticKind::IdentityAdmission
                                                          : ModuleDiagnosticKind::Binder));
    if (comparison != 0) { return comparison; }
    if (left.isBinderModuleSite()) {
      comparison = compareBytes(left.binderSemanticOwnerBytes(), right.binderSemanticOwnerBytes());
      if (comparison != 0) { return comparison; }
      comparison = compareScalar(static_cast<uint8_t>(left.binderEmitter()),
                                 static_cast<uint8_t>(right.binderEmitter()));
      if (comparison != 0) { return comparison; }
    }
  }
  const auto leftPath =
      left.origin() == DiagnosticFactOrigin::Source
          ? left.occurrencePath()
          : (left.isIdentitySyntaxSite() ? left.identitySyntaxPath() : left.binderSyntaxPath());
  const auto rightPath =
      right.origin() == DiagnosticFactOrigin::Source
          ? right.occurrencePath()
          : (right.isIdentitySyntaxSite() ? right.identitySyntaxPath() : right.binderSyntaxPath());
  const size_t common = leftPath.size() < rightPath.size() ? leftPath.size() : rightPath.size();
  for (size_t index = 0; index < common; ++index) {
    comparison = compareScalar(leftPath[index], rightPath[index]);
    if (comparison != 0) { return comparison; }
  }
  return compareScalar(leftPath.size(), rightPath.size());
}

zc::Vector<zc::String> cloneStrings(zc::ArrayPtr<const zc::String> strings) {
  zc::Vector<zc::String> result(strings.size());
  for (const auto& string : strings) { result.add(zc::str(string)); }
  return result;
}

template <typename T>
zc::Vector<T> emptyVector(zc::Maybe<zc::MemoryResource&> resource) {
  ZC_IF_SOME(value, resource) { return zc::Vector<T>(value); }
  return zc::Vector<T>();
}

zc::String ownedString(zc::Maybe<zc::MemoryResource&> resource, zc::ArrayPtr<const char> value) {
  ZC_IF_SOME(memory, resource) { return zc::resourceHeapString(memory, value); }
  return zc::heapString(value);
}

bool validSourceArguments(DiagID code, zc::ArrayPtr<const zc::String> arguments) {
  return isSourceSyntaxDiagnostic(code) && isKnownDiagnostic(code) &&
         getDiagnosticInfo(code).argCount == arguments.size();
}

bool validArguments(DiagID code, zc::ArrayPtr<const zc::String> arguments) {
  return isKnownDiagnostic(code) && getDiagnosticInfo(code).argCount == arguments.size();
}

bool argumentsFit(zc::ArrayPtr<const zc::String> arguments, uint64_t maximumBytes) {
  uint64_t consumed = 0;
  for (const auto& argument : arguments) {
    if (argument.size() > maximumBytes - consumed) { return false; }
    consumed += argument.size();
  }
  return true;
}

size_t provenanceComponentCount(const DiagnosticProvenanceKey& provenance) {
  return provenance.origin() == DiagnosticFactOrigin::Source
             ? provenance.occurrencePath().size()
             : (provenance.isIdentitySyntaxSite() ? provenance.identitySyntaxPath().size()
                                                  : provenance.binderSyntaxPath().size());
}

bool sameOccurrence(const DiagnosticOccurrenceKey& occurrence,
                    const DiagnosticProvenanceKey& provenance) {
  if (occurrence.origin() != provenance.origin()) { return false; }
  if (occurrence.origin() == DiagnosticFactOrigin::Module) {
    if (occurrence.module().encode().asPtr() != provenance.module().encode().asPtr() ||
        !occurrence.source().sameAs(provenance.source())) {
      return false;
    }
    if (occurrence.isIdentityAdmission()) {
      return provenance.isIdentitySyntaxSite() &&
             occurrence.identitySyntaxPath() == provenance.identitySyntaxPath();
    }
    return provenance.isBinderModuleSite() &&
           occurrence.binderSemanticOwnerBytes() == provenance.binderSemanticOwnerBytes() &&
           occurrence.binderEmitter() == provenance.binderEmitter() &&
           occurrence.binderSyntaxPath() == provenance.binderSyntaxPath();
  }
  const auto path = provenance.occurrencePath();
  return path.size() >= 2 && occurrence.source().sameAs(provenance.source()) &&
         occurrence.phase() == provenance.phase() && occurrence.emitter() == provenance.emitter() &&
         occurrence.occurrence() == path[0];
}

void encodeOccurrence(identity::CanonicalEncoder& encoder,
                      const DiagnosticOccurrenceKey& occurrence) {
  encoder.encodeUint8(static_cast<uint8_t>(occurrence.origin()));
  if (occurrence.origin() == DiagnosticFactOrigin::Module) {
    encoder.encodeUint8(static_cast<uint8_t>(occurrence.isIdentityAdmission()
                                                 ? ModuleDiagnosticKind::IdentityAdmission
                                                 : ModuleDiagnosticKind::Binder));
    occurrence.module().encode(encoder);
    occurrence.source().encode(encoder);
    if (occurrence.isBinder()) {
      encoder.encodeUint8(static_cast<uint8_t>(occurrence.binderProducer()));
      if (occurrence.hasBinderSemanticOwner()) {
        encoder.encodeSome();
        encoder.encodeByteString(occurrence.binderSemanticOwnerBytes());
      } else {
        encoder.encodeNone();
      }
      encoder.encodeUint8(static_cast<uint8_t>(occurrence.binderEmitter()));
      encoder.encodeSequenceSize(occurrence.binderSyntaxPath().size());
      for (const auto component : occurrence.binderSyntaxPath()) {
        encoder.encodeUint32(component);
      }
      encoder.encodeUint32(0);
      return;
    }
    encoder.encodeUint8(static_cast<uint8_t>(occurrence.identityPhase()));
    encoder.encodeUint8(static_cast<uint8_t>(occurrence.identityEmitter()));
    encoder.encodeSequenceSize(occurrence.identitySyntaxPath().size());
    for (const auto component : occurrence.identitySyntaxPath()) {
      encoder.encodeUint32(component);
    }
    return;
  }
  occurrence.source().encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(occurrence.phase()));
  encoder.encodeUint8(static_cast<uint8_t>(occurrence.emitter()));
  encoder.encodeUint32(occurrence.occurrence());
}

zc::Maybe<DiagnosticOccurrenceKey> decodeOccurrence(identity::CanonicalDecoder& decoder) {
  auto origin = decoder.decodeUint8();
  if (origin == zc::none) { return zc::none; }
  if (static_cast<DiagnosticFactOrigin>(ZC_ASSERT_NONNULL(origin)) ==
      DiagnosticFactOrigin::Module) {
    auto kind = decoder.decodeUint8();
    auto module = identity::ModuleKey::decodeCanonical(decoder);
    auto source = identity::SourceFileKey::decodeCanonical(decoder);
    if (kind == zc::none || module == zc::none || source == zc::none) { return zc::none; }
    if (static_cast<ModuleDiagnosticKind>(ZC_ASSERT_NONNULL(kind)) ==
        ModuleDiagnosticKind::Binder) {
      auto producer = decoder.decodeUint8();
      auto hasOwner = decoder.decodeUint8();
      if (producer == zc::none || hasOwner == zc::none || ZC_ASSERT_NONNULL(hasOwner) > 1) {
        return zc::none;
      }
      zc::Maybe<zc::Array<uint8_t>> owner;
      if (ZC_ASSERT_NONNULL(hasOwner)) {
        auto bytes = decoder.decodeByteString(64 * 1024 * 1024);
        if (bytes == zc::none || ZC_ASSERT_NONNULL(bytes).size() == 0) { return zc::none; }
        owner = zc::heapArray<uint8_t>(ZC_ASSERT_NONNULL(bytes).asPtr());
      }
      auto emitter = decoder.decodeUint8();
      auto count = decoder.decodeSequenceSize(64);
      if (emitter == zc::none || count == zc::none) { return zc::none; }
      zc::Vector<uint32_t> path;
      for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
        auto component = decoder.decodeUint32();
        if (component == zc::none) { return zc::none; }
        path.add(ZC_ASSERT_NONNULL(component));
      }
      auto occurrence = decoder.decodeUint32();
      if (occurrence == zc::none || ZC_ASSERT_NONNULL(occurrence) != 0) { return zc::none; }
      return DiagnosticFactCodecAccess::binderOccurrence(
          zc::mv(ZC_ASSERT_NONNULL(module)), zc::mv(ZC_ASSERT_NONNULL(source)),
          static_cast<BinderDiagnosticProducer>(ZC_ASSERT_NONNULL(producer)), zc::mv(owner),
          static_cast<BinderDiagnosticEmitter>(ZC_ASSERT_NONNULL(emitter)), zc::mv(path));
    }
    if (static_cast<ModuleDiagnosticKind>(ZC_ASSERT_NONNULL(kind)) !=
        ModuleDiagnosticKind::IdentityAdmission) {
      return zc::none;
    }
    auto phase = decoder.decodeUint8();
    auto emitter = decoder.decodeUint8();
    auto count = decoder.decodeSequenceSize(64);
    if (phase == zc::none || emitter == zc::none || count == zc::none) { return zc::none; }
    zc::Vector<uint32_t> path;
    for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
      auto component = decoder.decodeUint32();
      if (component == zc::none) { return zc::none; }
      path.add(ZC_ASSERT_NONNULL(component));
    }
    if (static_cast<IdentityDiagnosticPhase>(ZC_ASSERT_NONNULL(phase)) !=
        IdentityDiagnosticPhase::IdentityAdmission) {
      return zc::none;
    }
    return DiagnosticOccurrenceKey::identityAdmission(
        zc::mv(ZC_ASSERT_NONNULL(module)), zc::mv(ZC_ASSERT_NONNULL(source)), zc::mv(path),
        static_cast<IdentityDiagnosticEmitter>(ZC_ASSERT_NONNULL(emitter)));
  }
  if (static_cast<DiagnosticFactOrigin>(ZC_ASSERT_NONNULL(origin)) !=
      DiagnosticFactOrigin::Source) {
    return zc::none;
  }
  auto source = identity::SourceFileKey::decodeCanonical(decoder);
  auto phase = decoder.decodeUint8();
  auto emitter = decoder.decodeUint8();
  auto occurrence = decoder.decodeUint32();
  if (source == zc::none || phase == zc::none || emitter == zc::none || occurrence == zc::none) {
    return zc::none;
  }
  return DiagnosticOccurrenceKey::from(
      zc::mv(ZC_ASSERT_NONNULL(source)),
      static_cast<SourceDiagnosticPhase>(ZC_ASSERT_NONNULL(phase)),
      static_cast<SourceDiagnosticEmitter>(ZC_ASSERT_NONNULL(emitter)),
      ZC_ASSERT_NONNULL(occurrence));
}

void encodeProvenance(identity::CanonicalEncoder& encoder,
                      const DiagnosticProvenanceKey& provenance) {
  encoder.encodeUint8(static_cast<uint8_t>(provenance.origin()));
  if (provenance.origin() == DiagnosticFactOrigin::Module) {
    encoder.encodeUint8(static_cast<uint8_t>(provenance.isIdentitySyntaxSite()
                                                 ? ModuleDiagnosticKind::IdentityAdmission
                                                 : ModuleDiagnosticKind::Binder));
    provenance.module().encode(encoder);
    provenance.source().encode(encoder);
    if (provenance.isBinderModuleSite()) {
      if (provenance.hasBinderSemanticOwner()) {
        encoder.encodeSome();
        encoder.encodeByteString(provenance.binderSemanticOwnerBytes());
      } else {
        encoder.encodeNone();
      }
      encoder.encodeUint8(static_cast<uint8_t>(provenance.binderEmitter()));
      encoder.encodeSequenceSize(provenance.binderSyntaxPath().size());
      for (const auto component : provenance.binderSyntaxPath()) {
        encoder.encodeUint32(component);
      }
      encoder.encodeUint32(0);
      return;
    }
    encoder.encodeSequenceSize(provenance.identitySyntaxPath().size());
    for (const auto component : provenance.identitySyntaxPath()) {
      encoder.encodeUint32(component);
    }
    return;
  }
  provenance.source().encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(provenance.phase()));
  encoder.encodeUint8(static_cast<uint8_t>(provenance.emitter()));
  encoder.encodeSequenceSize(provenance.occurrencePath().size());
  for (uint32_t component : provenance.occurrencePath()) { encoder.encodeUint32(component); }
}

zc::Maybe<DiagnosticProvenanceKey> decodeProvenance(identity::CanonicalDecoder& decoder,
                                                    uint64_t maximumComponents) {
  auto origin = decoder.decodeUint8();
  if (origin == zc::none) { return zc::none; }
  if (static_cast<DiagnosticFactOrigin>(ZC_ASSERT_NONNULL(origin)) ==
      DiagnosticFactOrigin::Module) {
    auto kind = decoder.decodeUint8();
    auto module = identity::ModuleKey::decodeCanonical(decoder);
    auto source = identity::SourceFileKey::decodeCanonical(decoder);
    if (kind == zc::none || module == zc::none || source == zc::none) { return zc::none; }
    if (static_cast<ModuleDiagnosticKind>(ZC_ASSERT_NONNULL(kind)) ==
        ModuleDiagnosticKind::Binder) {
      auto hasOwner = decoder.decodeUint8();
      if (hasOwner == zc::none || ZC_ASSERT_NONNULL(hasOwner) > 1) { return zc::none; }
      zc::Maybe<zc::Array<uint8_t>> owner;
      if (ZC_ASSERT_NONNULL(hasOwner)) {
        auto bytes = decoder.decodeByteString(64 * 1024 * 1024);
        if (bytes == zc::none || ZC_ASSERT_NONNULL(bytes).size() == 0) { return zc::none; }
        owner = zc::heapArray<uint8_t>(ZC_ASSERT_NONNULL(bytes).asPtr());
      }
      auto emitter = decoder.decodeUint8();
      auto count = decoder.decodeSequenceSize(maximumComponents);
      if (emitter == zc::none || count == zc::none) { return zc::none; }
      zc::Vector<uint32_t> path;
      for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
        auto component = decoder.decodeUint32();
        if (component == zc::none) { return zc::none; }
        path.add(ZC_ASSERT_NONNULL(component));
      }
      auto occurrence = decoder.decodeUint32();
      if (occurrence == zc::none || ZC_ASSERT_NONNULL(occurrence) != 0) { return zc::none; }
      return DiagnosticFactCodecAccess::binderProvenance(
          zc::mv(ZC_ASSERT_NONNULL(module)), zc::mv(ZC_ASSERT_NONNULL(source)), zc::mv(owner),
          static_cast<BinderDiagnosticEmitter>(ZC_ASSERT_NONNULL(emitter)), zc::mv(path));
    }
    if (static_cast<ModuleDiagnosticKind>(ZC_ASSERT_NONNULL(kind)) !=
        ModuleDiagnosticKind::IdentityAdmission) {
      return zc::none;
    }
    auto count = decoder.decodeSequenceSize(maximumComponents);
    if (count == zc::none) { return zc::none; }
    zc::Vector<uint32_t> path;
    for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
      auto component = decoder.decodeUint32();
      if (component == zc::none) { return zc::none; }
      path.add(ZC_ASSERT_NONNULL(component));
    }
    return DiagnosticProvenanceKey::identitySyntaxSite(
        zc::mv(ZC_ASSERT_NONNULL(module)), zc::mv(ZC_ASSERT_NONNULL(source)), zc::mv(path));
  }
  if (static_cast<DiagnosticFactOrigin>(ZC_ASSERT_NONNULL(origin)) !=
      DiagnosticFactOrigin::Source) {
    return zc::none;
  }
  auto source = identity::SourceFileKey::decodeCanonical(decoder);
  auto phase = decoder.decodeUint8();
  auto emitter = decoder.decodeUint8();
  auto count = decoder.decodeSequenceSize(maximumComponents);
  if (source == zc::none || phase == zc::none || emitter == zc::none || count == zc::none ||
      ZC_ASSERT_NONNULL(count) > decoder.remaining() / sizeof(uint32_t)) {
    return zc::none;
  }
  zc::Vector<uint32_t> path;
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto component = decoder.decodeUint32();
    if (component == zc::none) { return zc::none; }
    path.add(ZC_ASSERT_NONNULL(component));
  }
  return DiagnosticProvenanceKey::from(
      zc::mv(ZC_ASSERT_NONNULL(source)),
      static_cast<SourceDiagnosticPhase>(ZC_ASSERT_NONNULL(phase)),
      static_cast<SourceDiagnosticEmitter>(ZC_ASSERT_NONNULL(emitter)), zc::mv(path));
}

void encodeStrings(identity::CanonicalEncoder& encoder, zc::ArrayPtr<const zc::String> values) {
  encoder.encodeSequenceSize(values.size());
  for (const auto& value : values) { encoder.encodeByteString(value.asBytes()); }
}

zc::Maybe<zc::Vector<zc::String>> decodeStrings(identity::CanonicalDecoder& decoder,
                                                zc::Maybe<zc::MemoryResource&> resultResource,
                                                uint64_t maximumBytes) {
  auto count = decoder.decodeSequenceSize(maximumBytes / kMinimumStringBytes);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) > decoder.remaining() / kMinimumStringBytes) {
    return zc::none;
  }
  uint64_t consumedBytes = 0;
  auto result = emptyVector<zc::String>(resultResource);
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto value = decoder.decodeByteString(maximumBytes);
    if (value == zc::none || ZC_ASSERT_NONNULL(value).size() > maximumBytes - consumedBytes) {
      return zc::none;
    }
    consumedBytes += ZC_ASSERT_NONNULL(value).size();
    result.add(ownedString(resultResource, ZC_ASSERT_NONNULL(value).asChars()));
  }
  return zc::mv(result);
}

void encodeSecondary(identity::CanonicalEncoder& encoder, const DiagnosticSecondary& secondary) {
  encoder.encodeUint8(static_cast<uint8_t>(secondary.role()));
  ZC_IF_SOME(code, secondary.code()) {
    encoder.encodeSome();
    encoder.encodeUint32(static_cast<uint32_t>(code));
  } else {
    encoder.encodeNone();
  }
  encodeProvenance(encoder, secondary.provenance());
  encodeStrings(encoder, secondary.arguments());
}

zc::Maybe<DiagnosticSecondary> decodeSecondary(identity::CanonicalDecoder& decoder,
                                               zc::Maybe<zc::MemoryResource&> resultResource,
                                               DiagnosticFactCodecLimits limits) {
  auto role = decoder.decodeUint8();
  auto codeTag = decoder.decodeUint8();
  if (role == zc::none || codeTag == zc::none) { return zc::none; }
  zc::Maybe<DiagID> code;
  if (ZC_ASSERT_NONNULL(codeTag) == 0x01) {
    auto value = decoder.decodeUint32();
    if (value == zc::none) { return zc::none; }
    code = static_cast<DiagID>(ZC_ASSERT_NONNULL(value));
  } else if (ZC_ASSERT_NONNULL(codeTag) != 0x00) {
    return zc::none;
  }
  auto provenance = decodeProvenance(decoder, limits.maximumProvenanceComponentsPerKey);
  auto arguments = decodeStrings(decoder, resultResource, limits.maximumArgumentBytesPerRecord);
  if (provenance == zc::none || arguments == zc::none) { return zc::none; }
  const auto decodedRole = static_cast<DiagnosticSecondaryRole>(ZC_ASSERT_NONNULL(role));
  if (decodedRole == DiagnosticSecondaryRole::PreviousDeclaration &&
      ZC_ASSERT_NONNULL(provenance).origin() == DiagnosticFactOrigin::Module) {
    if (code == zc::none || ZC_ASSERT_NONNULL(arguments).size() != 0) { return zc::none; }
    return DiagnosticSecondary::previousDeclaration(ZC_ASSERT_NONNULL(code),
                                                    zc::mv(ZC_ASSERT_NONNULL(provenance)));
  }
  if (decodedRole == DiagnosticSecondaryRole::Highlight &&
      ZC_ASSERT_NONNULL(provenance).origin() == DiagnosticFactOrigin::Source) {
    if (code != zc::none || ZC_ASSERT_NONNULL(arguments).size() != 0) { return zc::none; }
    return DiagnosticSecondary::highlight(zc::mv(ZC_ASSERT_NONNULL(provenance)));
  }
  if (decodedRole != DiagnosticSecondaryRole::Note ||
      ZC_ASSERT_NONNULL(provenance).origin() != DiagnosticFactOrigin::Source || code == zc::none) {
    return zc::none;
  }
  return DiagnosticSecondary::note(ZC_ASSERT_NONNULL(code), zc::mv(ZC_ASSERT_NONNULL(provenance)),
                                   zc::mv(ZC_ASSERT_NONNULL(arguments)));
}

zc::Maybe<zc::Array<uint8_t>> finishBounded(identity::CanonicalEncoder&& encoder,
                                            zc::Maybe<zc::MemoryResource&> outputResource,
                                            uint64_t maximumBytes) {
  auto bytes = encoder.finish();
  if (bytes.size() > maximumBytes) { return zc::none; }
  ZC_IF_SOME(resource, outputResource) {
    auto result = zc::resourceHeapArray<uint8_t>(resource, bytes.size());
    result.asPtr().copyFrom(bytes.asPtr());
    return zc::mv(result);
  }
  return zc::mv(bytes);
}

}  // namespace

struct SourceDiagnosticOccurrence final {
  identity::SourceFileKey source;
  SourceDiagnosticPhase phase;
  SourceDiagnosticEmitter emitter;
  uint32_t occurrence;
};

struct IdentityDiagnosticOccurrence final {
  identity::ModuleKey module;
  identity::SourceFileKey source;
  IdentityDiagnosticPhase phase;
  IdentityDiagnosticEmitter emitter;
  zc::Vector<uint32_t> syntaxPath;
};

struct BinderDiagnosticOccurrence final {
  identity::ModuleKey module;
  identity::SourceFileKey source;
  BinderDiagnosticProducer producer;
  zc::Maybe<zc::Array<uint8_t>> semanticOwner;
  BinderDiagnosticEmitter emitter;
  zc::Vector<uint32_t> syntaxPath;
};

struct DiagnosticOccurrenceKey::Impl final {
  template <typename Value>
  explicit Impl(Value&& value) : value(zc::fwd<Value>(value)) {}
  zc::OneOf<SourceDiagnosticOccurrence, IdentityDiagnosticOccurrence, BinderDiagnosticOccurrence>
      value;
};

DiagnosticOccurrenceKey::DiagnosticOccurrenceKey(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
DiagnosticOccurrenceKey::~DiagnosticOccurrenceKey() noexcept(false) = default;
DiagnosticOccurrenceKey::DiagnosticOccurrenceKey(DiagnosticOccurrenceKey&&) noexcept = default;
DiagnosticOccurrenceKey& DiagnosticOccurrenceKey::operator=(DiagnosticOccurrenceKey&&) noexcept =
    default;

zc::Maybe<DiagnosticOccurrenceKey> DiagnosticOccurrenceKey::from(identity::SourceFileKey&& source,
                                                                 SourceDiagnosticPhase phase,
                                                                 SourceDiagnosticEmitter emitter,
                                                                 uint32_t occurrence) {
  if (!validPhaseEmitter(phase, emitter)) { return zc::none; }
  return DiagnosticOccurrenceKey(
      zc::heap<Impl>(SourceDiagnosticOccurrence{zc::mv(source), phase, emitter, occurrence}));
}

zc::Maybe<DiagnosticOccurrenceKey> DiagnosticOccurrenceKey::identityAdmission(
    identity::ModuleKey&& module, identity::SourceFileKey&& source,
    zc::Vector<uint32_t>&& syntaxPath, IdentityDiagnosticEmitter emitter) {
  if (!source.belongsTo(module.crate()) || syntaxPath.size() == 0 ||
      emitter < IdentityDiagnosticEmitter::DuplicateBound ||
      emitter > IdentityDiagnosticEmitter::DuplicateGenericParameter) {
    return zc::none;
  }
  return DiagnosticOccurrenceKey(zc::heap<Impl>(IdentityDiagnosticOccurrence{
      zc::mv(module), zc::mv(source), IdentityDiagnosticPhase::IdentityAdmission, emitter,
      zc::mv(syntaxPath)}));
}

zc::Maybe<DiagnosticOccurrenceKey> DiagnosticOccurrenceKey::binder(
    identity::ModuleKey&& module, identity::SourceFileKey&& source,
    BinderDiagnosticProducer producer, zc::Maybe<zc::Array<uint8_t>>&& semanticOwner,
    BinderDiagnosticEmitter emitter, zc::Vector<uint32_t>&& syntaxPath) {
  const bool moduleSkeleton = producer == BinderDiagnosticProducer::BindModuleSkeleton;
  const bool ownerBody = producer == BinderDiagnosticProducer::BindOwnerBody;
  if (!source.belongsTo(module.crate()) || syntaxPath.size() == 0 ||
      (!moduleSkeleton && !ownerBody) || emitter < BinderDiagnosticEmitter::Declaration ||
      emitter > BinderDiagnosticEmitter::ContextualSelf ||
      (moduleSkeleton && semanticOwner != zc::none) ||
      (ownerBody && (semanticOwner == zc::none || ZC_ASSERT_NONNULL(semanticOwner).size() == 0))) {
    return zc::none;
  }
  return DiagnosticOccurrenceKey(zc::heap<Impl>(
      BinderDiagnosticOccurrence{zc::mv(module), zc::mv(source), producer, zc::mv(semanticOwner),
                                 emitter, zc::mv(syntaxPath)}));
}

DiagnosticOccurrenceKey DiagnosticOccurrenceKey::clone() const {
  if (origin() == DiagnosticFactOrigin::Source) {
    const auto& value = impl->value.get<SourceDiagnosticOccurrence>();
    return ZC_ASSERT_NONNULL(
        from(value.source.clone(), value.phase, value.emitter, value.occurrence));
  }
  if (isIdentityAdmission()) {
    const auto& value = impl->value.get<IdentityDiagnosticOccurrence>();
    zc::Vector<uint32_t> path(value.syntaxPath.size());
    path.addAll(value.syntaxPath.asPtr());
    return ZC_ASSERT_NONNULL(
        identityAdmission(value.module.clone(), value.source.clone(), zc::mv(path), value.emitter));
  }
  const auto& value = impl->value.get<BinderDiagnosticOccurrence>();
  zc::Maybe<zc::Array<uint8_t>> owner;
  ZC_IF_SOME(bytes, value.semanticOwner) { owner = zc::heapArray<uint8_t>(bytes.asPtr()); }
  zc::Vector<uint32_t> path(value.syntaxPath.size());
  path.addAll(value.syntaxPath.asPtr());
  return ZC_ASSERT_NONNULL(binder(value.module.clone(), value.source.clone(), value.producer,
                                  zc::mv(owner), value.emitter, zc::mv(path)));
}
DiagnosticFactOrigin DiagnosticOccurrenceKey::origin() const noexcept {
  return impl->value.is<SourceDiagnosticOccurrence>() ? DiagnosticFactOrigin::Source
                                                      : DiagnosticFactOrigin::Module;
}
const identity::SourceFileKey& DiagnosticOccurrenceKey::source() const noexcept {
  if (origin() == DiagnosticFactOrigin::Source) {
    return impl->value.get<SourceDiagnosticOccurrence>().source;
  }
  if (isIdentityAdmission()) { return impl->value.get<IdentityDiagnosticOccurrence>().source; }
  return impl->value.get<BinderDiagnosticOccurrence>().source;
}
SourceDiagnosticPhase DiagnosticOccurrenceKey::phase() const noexcept {
  ZC_IREQUIRE(origin() == DiagnosticFactOrigin::Source,
              "module diagnostic occurrence has no source phase");
  return impl->value.get<SourceDiagnosticOccurrence>().phase;
}
SourceDiagnosticEmitter DiagnosticOccurrenceKey::emitter() const noexcept {
  ZC_IREQUIRE(origin() == DiagnosticFactOrigin::Source,
              "module diagnostic occurrence has no source emitter");
  return impl->value.get<SourceDiagnosticOccurrence>().emitter;
}
uint32_t DiagnosticOccurrenceKey::occurrence() const noexcept {
  ZC_IREQUIRE(origin() == DiagnosticFactOrigin::Source,
              "module diagnostic occurrence has no source occurrence ordinal");
  return impl->value.get<SourceDiagnosticOccurrence>().occurrence;
}
const identity::ModuleKey& DiagnosticOccurrenceKey::module() const noexcept {
  ZC_IREQUIRE(origin() == DiagnosticFactOrigin::Module,
              "source diagnostic occurrence has no module root");
  if (isIdentityAdmission()) { return impl->value.get<IdentityDiagnosticOccurrence>().module; }
  return impl->value.get<BinderDiagnosticOccurrence>().module;
}
bool DiagnosticOccurrenceKey::isIdentityAdmission() const noexcept {
  return impl->value.is<IdentityDiagnosticOccurrence>();
}
IdentityDiagnosticPhase DiagnosticOccurrenceKey::identityPhase() const noexcept {
  ZC_IREQUIRE(isIdentityAdmission(), "diagnostic occurrence has no identity phase");
  return impl->value.get<IdentityDiagnosticOccurrence>().phase;
}
IdentityDiagnosticEmitter DiagnosticOccurrenceKey::identityEmitter() const noexcept {
  ZC_IREQUIRE(isIdentityAdmission(), "diagnostic occurrence has no identity emitter");
  return impl->value.get<IdentityDiagnosticOccurrence>().emitter;
}
zc::ArrayPtr<const uint32_t> DiagnosticOccurrenceKey::identitySyntaxPath() const {
  ZC_IREQUIRE(isIdentityAdmission(), "diagnostic occurrence has no identity syntax path");
  return impl->value.get<IdentityDiagnosticOccurrence>().syntaxPath.asPtr();
}
bool DiagnosticOccurrenceKey::isBinder() const noexcept {
  return impl->value.is<BinderDiagnosticOccurrence>();
}
BinderDiagnosticProducer DiagnosticOccurrenceKey::binderProducer() const noexcept {
  ZC_IREQUIRE(isBinder(), "diagnostic occurrence has no Binder producer");
  return impl->value.get<BinderDiagnosticOccurrence>().producer;
}
BinderDiagnosticEmitter DiagnosticOccurrenceKey::binderEmitter() const noexcept {
  ZC_IREQUIRE(isBinder(), "diagnostic occurrence has no Binder emitter");
  return impl->value.get<BinderDiagnosticOccurrence>().emitter;
}
bool DiagnosticOccurrenceKey::hasBinderSemanticOwner() const noexcept {
  ZC_IREQUIRE(isBinder(), "diagnostic occurrence has no Binder semantic owner");
  return impl->value.get<BinderDiagnosticOccurrence>().semanticOwner != zc::none;
}
zc::ArrayPtr<const uint8_t> DiagnosticOccurrenceKey::binderSemanticOwnerBytes() const {
  ZC_IREQUIRE(isBinder(), "diagnostic occurrence has no Binder semantic owner");
  ZC_IF_SOME(owner, impl->value.get<BinderDiagnosticOccurrence>().semanticOwner) {
    return owner.asPtr();
  }
  return {};
}
zc::ArrayPtr<const uint32_t> DiagnosticOccurrenceKey::binderSyntaxPath() const {
  ZC_IREQUIRE(isBinder(), "diagnostic occurrence has no Binder syntax path");
  return impl->value.get<BinderDiagnosticOccurrence>().syntaxPath.asPtr();
}
bool DiagnosticOccurrenceKey::operator==(const DiagnosticOccurrenceKey& other) const noexcept {
  return compareOccurrence(*this, other) == 0;
}

struct SourceDiagnosticProvenance final {
  identity::SourceFileKey source;
  SourceDiagnosticPhase phase;
  SourceDiagnosticEmitter emitter;
  zc::Vector<uint32_t> path;
};

struct IdentityDiagnosticProvenance final {
  identity::ModuleKey module;
  identity::SourceFileKey source;
  zc::Vector<uint32_t> syntaxPath;
};

struct BinderDiagnosticProvenance final {
  identity::ModuleKey module;
  identity::SourceFileKey source;
  zc::Maybe<zc::Array<uint8_t>> semanticOwner;
  BinderDiagnosticEmitter emitter;
  zc::Vector<uint32_t> syntaxPath;
};

struct DiagnosticProvenanceKey::Impl final {
  template <typename Value>
  explicit Impl(Value&& value) : value(zc::fwd<Value>(value)) {}
  zc::OneOf<SourceDiagnosticProvenance, IdentityDiagnosticProvenance, BinderDiagnosticProvenance>
      value;
};

DiagnosticProvenanceKey::DiagnosticProvenanceKey(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
DiagnosticProvenanceKey::~DiagnosticProvenanceKey() noexcept(false) = default;
DiagnosticProvenanceKey::DiagnosticProvenanceKey(DiagnosticProvenanceKey&&) noexcept = default;
DiagnosticProvenanceKey& DiagnosticProvenanceKey::operator=(DiagnosticProvenanceKey&&) noexcept =
    default;

zc::Maybe<DiagnosticProvenanceKey> DiagnosticProvenanceKey::from(
    identity::SourceFileKey&& source, SourceDiagnosticPhase phase, SourceDiagnosticEmitter emitter,
    zc::Vector<uint32_t>&& occurrencePath) {
  if (!validPhaseEmitter(phase, emitter) || !validPath(occurrencePath.asPtr())) { return zc::none; }
  return DiagnosticProvenanceKey(zc::heap<Impl>(
      SourceDiagnosticProvenance{zc::mv(source), phase, emitter, zc::mv(occurrencePath)}));
}
zc::Maybe<DiagnosticProvenanceKey> DiagnosticProvenanceKey::identitySyntaxSite(
    identity::ModuleKey&& module, identity::SourceFileKey&& source,
    zc::Vector<uint32_t>&& syntaxPath) {
  if (!source.belongsTo(module.crate()) || syntaxPath.size() == 0) { return zc::none; }
  return DiagnosticProvenanceKey(zc::heap<Impl>(
      IdentityDiagnosticProvenance{zc::mv(module), zc::mv(source), zc::mv(syntaxPath)}));
}
zc::Maybe<DiagnosticProvenanceKey> DiagnosticProvenanceKey::binderModuleSite(
    identity::ModuleKey&& module, identity::SourceFileKey&& source,
    zc::Maybe<zc::Array<uint8_t>>&& semanticOwner, BinderDiagnosticEmitter emitter,
    zc::Vector<uint32_t>&& syntaxPath) {
  if (!source.belongsTo(module.crate()) || syntaxPath.size() == 0 ||
      emitter < BinderDiagnosticEmitter::Declaration ||
      emitter > BinderDiagnosticEmitter::ContextualSelf) {
    return zc::none;
  }
  return DiagnosticProvenanceKey(zc::heap<Impl>(BinderDiagnosticProvenance{
      zc::mv(module), zc::mv(source), zc::mv(semanticOwner), emitter, zc::mv(syntaxPath)}));
}
DiagnosticProvenanceKey DiagnosticProvenanceKey::clone() const {
  if (origin() == DiagnosticFactOrigin::Source) {
    const auto& value = impl->value.get<SourceDiagnosticProvenance>();
    zc::Vector<uint32_t> path(value.path.size());
    path.addAll(value.path.asPtr());
    return ZC_ASSERT_NONNULL(from(value.source.clone(), value.phase, value.emitter, zc::mv(path)));
  }
  if (isIdentitySyntaxSite()) {
    const auto& value = impl->value.get<IdentityDiagnosticProvenance>();
    zc::Vector<uint32_t> path(value.syntaxPath.size());
    path.addAll(value.syntaxPath.asPtr());
    return ZC_ASSERT_NONNULL(
        identitySyntaxSite(value.module.clone(), value.source.clone(), zc::mv(path)));
  }
  const auto& value = impl->value.get<BinderDiagnosticProvenance>();
  zc::Maybe<zc::Array<uint8_t>> owner;
  ZC_IF_SOME(bytes, value.semanticOwner) { owner = zc::heapArray<uint8_t>(bytes.asPtr()); }
  zc::Vector<uint32_t> path(value.syntaxPath.size());
  path.addAll(value.syntaxPath.asPtr());
  return ZC_ASSERT_NONNULL(binderModuleSite(value.module.clone(), value.source.clone(),
                                            zc::mv(owner), value.emitter, zc::mv(path)));
}
DiagnosticFactOrigin DiagnosticProvenanceKey::origin() const noexcept {
  return impl->value.is<SourceDiagnosticProvenance>() ? DiagnosticFactOrigin::Source
                                                      : DiagnosticFactOrigin::Module;
}
const identity::SourceFileKey& DiagnosticProvenanceKey::source() const noexcept {
  if (origin() == DiagnosticFactOrigin::Source) {
    return impl->value.get<SourceDiagnosticProvenance>().source;
  }
  if (isIdentitySyntaxSite()) { return impl->value.get<IdentityDiagnosticProvenance>().source; }
  return impl->value.get<BinderDiagnosticProvenance>().source;
}
SourceDiagnosticPhase DiagnosticProvenanceKey::phase() const noexcept {
  ZC_IREQUIRE(origin() == DiagnosticFactOrigin::Source,
              "module diagnostic provenance has no source phase");
  return impl->value.get<SourceDiagnosticProvenance>().phase;
}
SourceDiagnosticEmitter DiagnosticProvenanceKey::emitter() const noexcept {
  ZC_IREQUIRE(origin() == DiagnosticFactOrigin::Source,
              "module diagnostic provenance has no source emitter");
  return impl->value.get<SourceDiagnosticProvenance>().emitter;
}
zc::ArrayPtr<const uint32_t> DiagnosticProvenanceKey::occurrencePath() const {
  ZC_IREQUIRE(origin() == DiagnosticFactOrigin::Source,
              "module diagnostic provenance has no source occurrence path");
  return impl->value.get<SourceDiagnosticProvenance>().path.asPtr();
}
const identity::ModuleKey& DiagnosticProvenanceKey::module() const noexcept {
  ZC_IREQUIRE(origin() == DiagnosticFactOrigin::Module,
              "source diagnostic provenance has no module root");
  if (isIdentitySyntaxSite()) { return impl->value.get<IdentityDiagnosticProvenance>().module; }
  return impl->value.get<BinderDiagnosticProvenance>().module;
}
zc::ArrayPtr<const uint32_t> DiagnosticProvenanceKey::identitySyntaxPath() const {
  ZC_IREQUIRE(isIdentitySyntaxSite(), "diagnostic provenance has no identity syntax path");
  return impl->value.get<IdentityDiagnosticProvenance>().syntaxPath.asPtr();
}
bool DiagnosticProvenanceKey::isIdentitySyntaxSite() const noexcept {
  return impl->value.is<IdentityDiagnosticProvenance>();
}
bool DiagnosticProvenanceKey::isBinderModuleSite() const noexcept {
  return impl->value.is<BinderDiagnosticProvenance>();
}
BinderDiagnosticEmitter DiagnosticProvenanceKey::binderEmitter() const noexcept {
  ZC_IREQUIRE(isBinderModuleSite(), "diagnostic provenance has no Binder emitter");
  return impl->value.get<BinderDiagnosticProvenance>().emitter;
}
bool DiagnosticProvenanceKey::hasBinderSemanticOwner() const noexcept {
  ZC_IREQUIRE(isBinderModuleSite(), "diagnostic provenance has no Binder semantic owner");
  return impl->value.get<BinderDiagnosticProvenance>().semanticOwner != zc::none;
}
zc::ArrayPtr<const uint8_t> DiagnosticProvenanceKey::binderSemanticOwnerBytes() const {
  ZC_IREQUIRE(isBinderModuleSite(), "diagnostic provenance has no Binder semantic owner");
  ZC_IF_SOME(owner, impl->value.get<BinderDiagnosticProvenance>().semanticOwner) {
    return owner.asPtr();
  }
  return {};
}
zc::ArrayPtr<const uint32_t> DiagnosticProvenanceKey::binderSyntaxPath() const {
  ZC_IREQUIRE(isBinderModuleSite(), "diagnostic provenance has no Binder syntax path");
  return impl->value.get<BinderDiagnosticProvenance>().syntaxPath.asPtr();
}
bool DiagnosticProvenanceKey::operator==(const DiagnosticProvenanceKey& other) const noexcept {
  return compareProvenance(*this, other) == 0;
}

struct DiagnosticSecondary::Impl final {
  Impl(DiagnosticSecondaryRole role, zc::Maybe<DiagID> code, DiagnosticProvenanceKey&& provenance,
       zc::Vector<zc::String>&& arguments)
      : role(role), code(code), provenance(zc::mv(provenance)), arguments(zc::mv(arguments)) {}
  DiagnosticSecondaryRole role;
  zc::Maybe<DiagID> code;
  DiagnosticProvenanceKey provenance;
  zc::Vector<zc::String> arguments;
};

DiagnosticSecondary::DiagnosticSecondary(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
DiagnosticSecondary::~DiagnosticSecondary() noexcept(false) = default;
DiagnosticSecondary::DiagnosticSecondary(DiagnosticSecondary&&) noexcept = default;
DiagnosticSecondary& DiagnosticSecondary::operator=(DiagnosticSecondary&&) noexcept = default;

zc::Maybe<DiagnosticSecondary> DiagnosticSecondary::highlight(
    DiagnosticProvenanceKey&& provenance) {
  if (provenance.origin() != DiagnosticFactOrigin::Source) { return zc::none; }
  const auto path = provenance.occurrencePath();
  if (path.size() != 3 || path[1] != 1) { return zc::none; }
  return DiagnosticSecondary(zc::heap<Impl>(DiagnosticSecondaryRole::Highlight, zc::none,
                                            zc::mv(provenance), zc::Vector<zc::String>()));
}
zc::Maybe<DiagnosticSecondary> DiagnosticSecondary::note(DiagID code,
                                                         DiagnosticProvenanceKey&& provenance,
                                                         zc::Vector<zc::String>&& arguments) {
  if (provenance.origin() != DiagnosticFactOrigin::Source) { return zc::none; }
  const auto path = provenance.occurrencePath();
  if (path.size() != 3 || path[1] != 2 || !validSourceArguments(code, arguments.asPtr())) {
    return zc::none;
  }
  return DiagnosticSecondary(
      zc::heap<Impl>(DiagnosticSecondaryRole::Note, code, zc::mv(provenance), zc::mv(arguments)));
}
zc::Maybe<DiagnosticSecondary> DiagnosticSecondary::previousDeclaration(
    DiagID code, DiagnosticProvenanceKey&& provenance) {
  if (provenance.origin() != DiagnosticFactOrigin::Module ||
      code != DiagID::PreviousDeclarationHere) {
    return zc::none;
  }
  return DiagnosticSecondary(zc::heap<Impl>(DiagnosticSecondaryRole::PreviousDeclaration, code,
                                            zc::mv(provenance), zc::Vector<zc::String>()));
}
DiagnosticSecondary DiagnosticSecondary::clone() const {
  return DiagnosticSecondary(zc::heap<Impl>(impl->role, impl->code, impl->provenance.clone(),
                                            cloneStrings(impl->arguments.asPtr())));
}
DiagnosticSecondaryRole DiagnosticSecondary::role() const noexcept { return impl->role; }
zc::Maybe<DiagID> DiagnosticSecondary::code() const noexcept { return impl->code; }
const DiagnosticProvenanceKey& DiagnosticSecondary::provenance() const noexcept {
  return impl->provenance;
}
zc::ArrayPtr<const zc::String> DiagnosticSecondary::arguments() const {
  return impl->arguments.asPtr();
}
bool DiagnosticSecondary::operator==(const DiagnosticSecondary& other) const noexcept {
  return role() == other.role() && code() == other.code() && provenance() == other.provenance() &&
         arguments() == other.arguments();
}

struct DiagnosticFact::Impl final {
  Impl(DiagnosticOccurrenceKey&& occurrence, DiagID code, zc::Vector<zc::String>&& arguments,
       DiagnosticProvenanceKey&& primary, zc::Vector<DiagnosticSecondary>&& secondary)
      : occurrence(zc::mv(occurrence)),
        code(code),
        arguments(zc::mv(arguments)),
        primary(zc::mv(primary)),
        secondary(zc::mv(secondary)) {}
  DiagnosticOccurrenceKey occurrence;
  DiagID code;
  zc::Vector<zc::String> arguments;
  DiagnosticProvenanceKey primary;
  zc::Vector<DiagnosticSecondary> secondary;
};

DiagnosticFact::DiagnosticFact(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
DiagnosticFact::~DiagnosticFact() noexcept(false) = default;
DiagnosticFact::DiagnosticFact(DiagnosticFact&&) noexcept = default;
DiagnosticFact& DiagnosticFact::operator=(DiagnosticFact&&) noexcept = default;

zc::Maybe<DiagnosticFact> DiagnosticFact::from(DiagnosticOccurrenceKey&& occurrence, DiagID code,
                                               zc::Vector<zc::String>&& arguments,
                                               DiagnosticProvenanceKey&& primary,
                                               zc::Vector<DiagnosticSecondary>&& secondary) {
  if (occurrence.origin() == DiagnosticFactOrigin::Module) {
    if (!validArguments(code, arguments.asPtr()) || !sameOccurrence(occurrence, primary) ||
        primary.origin() != DiagnosticFactOrigin::Module) {
      return zc::none;
    }
    if (occurrence.isBinder() &&
        (occurrence.binderEmitter() != BinderDiagnosticEmitter::Lookup ||
         (code != DiagID::UndefinedIdentifier && code != DiagID::SymbolNamespaceMismatch &&
          code != DiagID::AmbiguousIdentifier) ||
         secondary.size() != 0)) {
      return zc::none;
    }
    if (occurrence.isIdentityAdmission()) {
      if (occurrence.identityEmitter() == IdentityDiagnosticEmitter::ConstantExpressionNotAllowed &&
          (code != DiagID::ConstantExpressionNotAllowed || arguments.size() != 0 ||
           secondary.size() != 0)) {
        return zc::none;
      }
      if (occurrence.identityEmitter() == IdentityDiagnosticEmitter::DuplicateGenericParameter &&
          (code != DiagID::DuplicateIdentifier || arguments.size() != 1 || secondary.size() != 1)) {
        return zc::none;
      }
    }
    for (const auto& item : secondary) {
      if (item.role() != DiagnosticSecondaryRole::PreviousDeclaration ||
          item.provenance().origin() != DiagnosticFactOrigin::Module ||
          item.code() != DiagID::PreviousDeclarationHere || item.arguments().size() != 0 ||
          item.provenance().module().encode().asPtr() != occurrence.module().encode().asPtr()) {
        return zc::none;
      }
    }
    return DiagnosticFact(zc::heap<Impl>(zc::mv(occurrence), code, zc::mv(arguments),
                                         zc::mv(primary), zc::mv(secondary)));
  }
  const auto primaryPath = primary.occurrencePath();
  if (!validSourceArguments(code, arguments.asPtr()) || !sameOccurrence(occurrence, primary) ||
      primaryPath.size() != 2 || primaryPath[1] != 0) {
    return zc::none;
  }
  uint32_t expectedHighlight = 0;
  uint32_t expectedNote = 0;
  bool sawNote = false;
  for (const auto& item : secondary) {
    const auto path = item.provenance().occurrencePath();
    if (!sameOccurrence(occurrence, item.provenance())) { return zc::none; }
    if (item.role() == DiagnosticSecondaryRole::Highlight) {
      if (sawNote || path[2] != expectedHighlight++) { return zc::none; }
    } else if (item.role() == DiagnosticSecondaryRole::Note) {
      sawNote = true;
      if (path[2] != expectedNote++) { return zc::none; }
    } else {
      return zc::none;
    }
  }
  return DiagnosticFact(zc::heap<Impl>(zc::mv(occurrence), code, zc::mv(arguments), zc::mv(primary),
                                       zc::mv(secondary)));
}
DiagnosticFact DiagnosticFact::clone() const {
  zc::Vector<DiagnosticSecondary> secondary(impl->secondary.size());
  for (const auto& item : impl->secondary) { secondary.add(item.clone()); }
  return DiagnosticFact(zc::heap<Impl>(impl->occurrence.clone(), impl->code,
                                       cloneStrings(impl->arguments.asPtr()), impl->primary.clone(),
                                       zc::mv(secondary)));
}
const DiagnosticOccurrenceKey& DiagnosticFact::occurrence() const noexcept {
  return impl->occurrence;
}
DiagID DiagnosticFact::code() const noexcept { return impl->code; }
zc::ArrayPtr<const zc::String> DiagnosticFact::arguments() const { return impl->arguments.asPtr(); }
const DiagnosticProvenanceKey& DiagnosticFact::primary() const noexcept { return impl->primary; }
zc::ArrayPtr<const DiagnosticSecondary> DiagnosticFact::secondary() const {
  return impl->secondary.asPtr();
}
bool DiagnosticFact::operator==(const DiagnosticFact& other) const noexcept {
  return occurrence() == other.occurrence() && code() == other.code() &&
         arguments() == other.arguments() && primary() == other.primary() &&
         secondary() == other.secondary();
}

SourceDiagnosticProvenanceEntry SourceDiagnosticProvenanceEntry::clone() const {
  return SourceDiagnosticProvenanceEntry{key.clone(), range};
}
bool SourceDiagnosticProvenanceEntry::operator==(
    const SourceDiagnosticProvenanceEntry& other) const noexcept {
  return key == other.key && range == other.range;
}

struct SourceDiagnosticProvenanceMap::Impl final {
  Impl(zc::Vector<SourceDiagnosticProvenanceEntry>&& entries, uint64_t sourceByteLength)
      : entries(zc::mv(entries)), sourceByteLength(sourceByteLength) {}
  zc::Vector<SourceDiagnosticProvenanceEntry> entries;
  uint64_t sourceByteLength;
};

SourceDiagnosticProvenanceMap::SourceDiagnosticProvenanceMap(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
SourceDiagnosticProvenanceMap::~SourceDiagnosticProvenanceMap() noexcept(false) = default;
SourceDiagnosticProvenanceMap::SourceDiagnosticProvenanceMap(
    SourceDiagnosticProvenanceMap&&) noexcept = default;
SourceDiagnosticProvenanceMap& SourceDiagnosticProvenanceMap::operator=(
    SourceDiagnosticProvenanceMap&&) noexcept = default;

zc::Maybe<SourceDiagnosticProvenanceMap> SourceDiagnosticProvenanceMap::from(
    zc::Vector<SourceDiagnosticProvenanceEntry>&& entries, uint64_t sourceByteLength) {
  for (size_t index = 0; index < entries.size(); ++index) {
    const auto& entry = entries[index];
    if (entry.key.origin() != DiagnosticFactOrigin::Source ||
        entry.range.byteStart > entry.range.byteEnd || entry.range.byteEnd > sourceByteLength ||
        (index != 0 && !entries[0].key.source().sameAs(entry.key.source())) ||
        (index != 0 && compareProvenance(entries[index - 1].key, entry.key) >= 0)) {
      return zc::none;
    }
  }
  return SourceDiagnosticProvenanceMap(zc::heap<Impl>(zc::mv(entries), sourceByteLength));
}
SourceDiagnosticProvenanceMap SourceDiagnosticProvenanceMap::clone() const {
  zc::Vector<SourceDiagnosticProvenanceEntry> entries(impl->entries.size());
  for (const auto& entry : impl->entries) { entries.add(entry.clone()); }
  return SourceDiagnosticProvenanceMap(zc::heap<Impl>(zc::mv(entries), impl->sourceByteLength));
}
zc::ArrayPtr<const SourceDiagnosticProvenanceEntry> SourceDiagnosticProvenanceMap::entries() const {
  return impl->entries.asPtr();
}
uint64_t SourceDiagnosticProvenanceMap::sourceByteLength() const noexcept {
  return impl->sourceByteLength;
}
zc::Maybe<const DiagnosticSourceRange&> SourceDiagnosticProvenanceMap::find(
    const DiagnosticProvenanceKey& key) const noexcept {
  for (const auto& entry : impl->entries) {
    if (entry.key == key) { return entry.range; }
  }
  return zc::none;
}
bool SourceDiagnosticProvenanceMap::operator==(
    const SourceDiagnosticProvenanceMap& other) const noexcept {
  return sourceByteLength() == other.sourceByteLength() && entries() == other.entries();
}

bool isSourceSyntaxDiagnostic(DiagID code) noexcept {
  switch (code) {
#define DIAG(Code, Name, ...) \
  case DiagID::Name:          \
    return true;
#include "compiler/diagnostics/defs/diagnostics-parse.def"
#undef DIAG
    default:
      return false;
  }
}

zc::Maybe<DiagnosticFactId> computeDiagnosticFactId(const DiagnosticFact& fact) {
  const DiagnosticFactCodecLimits limits{
      .maximumFacts = 1,
      .maximumEncodedBytes = 64 * 1024 * 1024,
      .maximumProvenanceComponentsPerKey = 8,
      .maximumArgumentBytesPerRecord = 64 * 1024 * 1024,
      .maximumSecondaryPerFact = 128,
  };
  auto encoded = encodeDiagnosticFacts(zc::none, {&fact, 1}, limits);
  if (encoded == zc::none) return zc::none;
  auto digest = identity::sha256(ZC_ASSERT_NONNULL(encoded).asPtr());
  if (digest == zc::none) return zc::none;
  return DiagnosticFactId::fromDigest(ZC_ASSERT_NONNULL(digest));
}

zc::Maybe<zc::Array<uint8_t>> encodeDiagnosticFacts(zc::Maybe<zc::MemoryResource&> outputResource,
                                                    zc::ArrayPtr<const DiagnosticFact> facts,
                                                    DiagnosticFactCodecLimits limits) {
  if (facts.size() > limits.maximumFacts) { return zc::none; }
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kDiagnosticFactsDomain.asBytes());
  encoder.encodeSequenceSize(facts.size());
  for (size_t index = 0; index < facts.size(); ++index) {
    const auto& fact = facts[index];
    if ((index != 0 && compareOccurrence(facts[index - 1].occurrence(), fact.occurrence()) >= 0) ||
        provenanceComponentCount(fact.primary()) > limits.maximumProvenanceComponentsPerKey ||
        fact.secondary().size() > limits.maximumSecondaryPerFact ||
        !argumentsFit(fact.arguments(), limits.maximumArgumentBytesPerRecord)) {
      return zc::none;
    }
    for (const auto& secondary : fact.secondary()) {
      if (provenanceComponentCount(secondary.provenance()) >
              limits.maximumProvenanceComponentsPerKey ||
          !argumentsFit(secondary.arguments(), limits.maximumArgumentBytesPerRecord)) {
        return zc::none;
      }
    }
    encodeOccurrence(encoder, fact.occurrence());
    encoder.encodeUint32(static_cast<uint32_t>(fact.code()));
    encodeStrings(encoder, fact.arguments());
    encodeProvenance(encoder, fact.primary());
    encoder.encodeSequenceSize(fact.secondary().size());
    for (const auto& secondary : fact.secondary()) { encodeSecondary(encoder, secondary); }
  }
  return finishBounded(zc::mv(encoder), outputResource, limits.maximumEncodedBytes);
}

zc::Maybe<zc::Vector<DiagnosticFact>> decodeDiagnosticFacts(
    zc::Maybe<zc::MemoryResource&> resultResource, zc::ArrayPtr<const uint8_t> encoded,
    DiagnosticFactCodecLimits limits) {
  if (encoded.size() > limits.maximumEncodedBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(encoded);
  auto domain = decoder.decodeByteString(kDiagnosticFactsDomain.size());
  auto count = decoder.decodeSequenceSize(limits.maximumFacts);
  if (domain == zc::none || ZC_ASSERT_NONNULL(domain).asPtr() != kDiagnosticFactsDomain.asBytes() ||
      count == zc::none || ZC_ASSERT_NONNULL(count) > decoder.remaining() / kMinimumFactBytes) {
    return zc::none;
  }
  auto facts = emptyVector<DiagnosticFact>(resultResource);
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto occurrence = decodeOccurrence(decoder);
    auto code = decoder.decodeUint32();
    auto arguments = decodeStrings(decoder, resultResource, limits.maximumArgumentBytesPerRecord);
    auto primary = decodeProvenance(decoder, limits.maximumProvenanceComponentsPerKey);
    auto secondaryCount = decoder.decodeSequenceSize(limits.maximumSecondaryPerFact);
    if (occurrence == zc::none || code == zc::none || arguments == zc::none ||
        primary == zc::none || secondaryCount == zc::none ||
        ZC_ASSERT_NONNULL(secondaryCount) > decoder.remaining() / kMinimumSecondaryBytes) {
      return zc::none;
    }
    auto secondary = emptyVector<DiagnosticSecondary>(resultResource);
    for (uint64_t child = 0; child < ZC_ASSERT_NONNULL(secondaryCount); ++child) {
      auto item = decodeSecondary(decoder, resultResource, limits);
      if (item == zc::none) { return zc::none; }
      secondary.add(zc::mv(ZC_ASSERT_NONNULL(item)));
    }
    auto fact = DiagnosticFact::from(zc::mv(ZC_ASSERT_NONNULL(occurrence)),
                                     static_cast<DiagID>(ZC_ASSERT_NONNULL(code)),
                                     zc::mv(ZC_ASSERT_NONNULL(arguments)),
                                     zc::mv(ZC_ASSERT_NONNULL(primary)), zc::mv(secondary));
    if (fact == zc::none ||
        (facts.size() != 0 &&
         compareOccurrence(facts.back().occurrence(), ZC_ASSERT_NONNULL(fact).occurrence()) >= 0)) {
      return zc::none;
    }
    facts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  if (!decoder.finished()) { return zc::none; }
  auto canonical = encodeDiagnosticFacts(zc::none, facts.asPtr(), limits);
  if (canonical == zc::none || ZC_ASSERT_NONNULL(canonical).asPtr() != encoded) { return zc::none; }
  return zc::mv(facts);
}

zc::Maybe<zc::Array<uint8_t>> encodeSourceDiagnosticProvenance(
    zc::Maybe<zc::MemoryResource&> outputResource, const SourceDiagnosticProvenanceMap& provenance,
    DiagnosticProvenanceCodecLimits limits) {
  if (provenance.entries().size() > limits.maximumEntries ||
      provenance.sourceByteLength() > limits.maximumSourceByteOffset) {
    return zc::none;
  }
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kSourceProvenanceDomain.asBytes());
  encoder.encodeSequenceSize(provenance.entries().size());
  for (const auto& entry : provenance.entries()) {
    if (entry.key.origin() != DiagnosticFactOrigin::Source ||
        entry.key.occurrencePath().size() > limits.maximumProvenanceComponentsPerKey) {
      return zc::none;
    }
    encodeProvenance(encoder, entry.key);
    encoder.encodeUint64(entry.range.byteStart);
    encoder.encodeUint64(entry.range.byteEnd);
    encoder.encodeBool(entry.range.isTokenRange);
  }
  return finishBounded(zc::mv(encoder), outputResource, limits.maximumEncodedBytes);
}

zc::Maybe<SourceDiagnosticProvenanceMap> decodeSourceDiagnosticProvenance(
    zc::Maybe<zc::MemoryResource&> resultResource, zc::ArrayPtr<const uint8_t> encoded,
    uint64_t sourceByteLength, DiagnosticProvenanceCodecLimits limits) {
  if (encoded.size() > limits.maximumEncodedBytes ||
      sourceByteLength > limits.maximumSourceByteOffset) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(encoded);
  auto domain = decoder.decodeByteString(kSourceProvenanceDomain.size());
  auto count = decoder.decodeSequenceSize(limits.maximumEntries);
  if (domain == zc::none ||
      ZC_ASSERT_NONNULL(domain).asPtr() != kSourceProvenanceDomain.asBytes() || count == zc::none ||
      ZC_ASSERT_NONNULL(count) > decoder.remaining() / kMinimumProvenanceEntryBytes) {
    return zc::none;
  }
  auto entries = emptyVector<SourceDiagnosticProvenanceEntry>(resultResource);
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto key = decodeProvenance(decoder, limits.maximumProvenanceComponentsPerKey);
    auto start = decoder.decodeUint64();
    auto end = decoder.decodeUint64();
    auto isTokenRange = decoder.decodeBool();
    if (key == zc::none || start == zc::none || end == zc::none || isTokenRange == zc::none ||
        ZC_ASSERT_NONNULL(key).origin() != DiagnosticFactOrigin::Source ||
        ZC_ASSERT_NONNULL(start) > ZC_ASSERT_NONNULL(end) ||
        ZC_ASSERT_NONNULL(end) > sourceByteLength) {
      return zc::none;
    }
    entries.add(SourceDiagnosticProvenanceEntry{
        zc::mv(ZC_ASSERT_NONNULL(key)),
        DiagnosticSourceRange{ZC_ASSERT_NONNULL(start), ZC_ASSERT_NONNULL(end),
                              ZC_ASSERT_NONNULL(isTokenRange)}});
  }
  if (!decoder.finished()) { return zc::none; }
  auto result = SourceDiagnosticProvenanceMap::from(zc::mv(entries), sourceByteLength);
  if (result == zc::none) { return zc::none; }
  auto canonical = encodeSourceDiagnosticProvenance(zc::none, ZC_ASSERT_NONNULL(result), limits);
  if (canonical == zc::none || ZC_ASSERT_NONNULL(canonical).asPtr() != encoded) { return zc::none; }
  return zc::mv(result);
}

bool validateDiagnosticProvenance(zc::ArrayPtr<const DiagnosticFact> facts,
                                  const SourceDiagnosticProvenanceMap& provenance) noexcept {
  size_t expected = 0;
  for (const auto& fact : facts) { expected += 1 + fact.secondary().size(); }
  if (expected != provenance.entries().size()) { return false; }
  size_t entryIndex = 0;
  for (const auto& fact : facts) {
    if (fact.occurrence().origin() != DiagnosticFactOrigin::Source ||
        fact.primary().origin() != DiagnosticFactOrigin::Source) {
      return false;
    }
    const auto& primaryEntry = provenance.entries()[entryIndex++];
    const auto& primary = primaryEntry.range;
    if (primaryEntry.key != fact.primary() || primary.byteStart != primary.byteEnd ||
        primary.isTokenRange) {
      return false;
    }
    for (const auto& secondary : fact.secondary()) {
      if (secondary.provenance().origin() != DiagnosticFactOrigin::Source) { return false; }
      const auto& secondaryEntry = provenance.entries()[entryIndex++];
      const auto& range = secondaryEntry.range;
      if (secondaryEntry.key != secondary.provenance() ||
          (secondary.role() == DiagnosticSecondaryRole::Note &&
           (range.byteStart != range.byteEnd || range.isTokenRange))) {
        return false;
      }
    }
  }
  return entryIndex == provenance.entries().size();
}

}  // namespace zomlang::compiler::diagnostics
