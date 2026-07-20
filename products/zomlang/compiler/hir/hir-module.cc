// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/hir/hir-module.h"

#include <cstdint>

#include "zc/core/encoding.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/binder/definition-site.h"
#include "zomlang/compiler/binder/frozen-definition-inventory.h"
#include "zomlang/compiler/checker/signature-facts.h"
#include "zomlang/compiler/identity/definition-key.h"

namespace zomlang::compiler::hir {
namespace {

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.source().sameAs(right.source()) && left.byteStart() == right.byteStart() &&
         left.byteEnd() == right.byteEnd();
}

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) return left[index] < right[index];
  }
  return left.size() < right.size();
}

identity::IdentityInvariant invalidIdentity(identity::IdentityAllocationPhase phase,
                                            uint32_t ordinal) {
  zc::Maybe<zc::Array<uint8_t>> noKey;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto invariant = identity::IdentityInvariant::from(
      identity::IdentityInvariantKind::InvalidHandle, phase, zc::mv(noKey), zc::mv(noRange),
      identity::IdentityApiSite::HandleLookup, ordinal);
  ZC_IF_SOME(value, invariant) { return zc::mv(value); }
  ZC_UNREACHABLE
}

class RegistryIdentityResolver final : public ir::IrFailureIdentityResolver {
public:
  explicit RegistryIdentityResolver(
      const identity::SemanticIdentityRegistrySet& registries) noexcept
      : registries(registries) {}

  ir::ExpandedIrIdentityResult expand(identity::ModuleId module) const override {
    auto key = registries.modules().lookup(module);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Module, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(identity::DefId definition) const override {
    auto key = registries.definitions().lookup(definition);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(ir::InstanceId) const override {
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
  }

private:
  const identity::SemanticIdentityRegistrySet& registries;
};

template <typename VerifiedValue>
ir::IrOperationResult<VerifiedValue> rejectHir(
    ir::IrFailurePhase phase, ir::IrFailureKind kind, identity::ModuleId module,
    const identity::SemanticIdentityRegistrySet& registries, uint32_t ordinal,
    zc::Vector<uint32_t>&& fieldPath = zc::Vector<uint32_t>()) {
  RegistryIdentityResolver identities(registries);
  auto fallback = ir::IrFailureFallbackContext::from(phase, ir::IrFailureOwner::module(module));
  ZC_IREQUIRE(fallback != zc::none, "HIR failure fallback must be legal");
  zc::Maybe<ir::IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  auto descriptor = ir::IrFailureDescriptor::decoded(
      ir::IrRejectedBranch::IrInvariantRejected, phase, kind, ir::IrFailureOwner::module(module),
      zc::mv(noSite), ir::IrFailureDetail::none(), zc::mv(noSpan), zc::mv(fieldPath), ordinal);
  ZC_IF_SOME(fallbackValue, fallback) {
    auto admitted = ir::IrFailureFactory::admit(zc::mv(descriptor), fallbackValue, identities);
    if (admitted.is<ir::IdentityRejectedIrFailureDescriptor>()) {
      zc::Vector<identity::IdentityInvariant> failures;
      failures.add(zc::mv(admitted).get<ir::IdentityRejectedIrFailureDescriptor>().failure);
      auto sorted = ir::SortedIdentityInvariantFacts::from(zc::mv(failures));
      ZC_IF_SOME(values, sorted) {
        return ir::IrOperationResult<VerifiedValue>::identityInvariantRejected(zc::mv(values));
      }
      ZC_UNREACHABLE
    }
    zc::Vector<ir::IrFailureFact> failures;
    if (admitted.is<ir::AcceptedIrFailureDescriptor>()) {
      failures.add(zc::mv(admitted).get<ir::AcceptedIrFailureDescriptor>().fact);
    } else {
      failures.add(zc::mv(admitted).get<ir::FallbackIrFailureDescriptor>().fact);
    }
    auto sorted = ir::SortedIrInvariantFailureFacts::from(zc::mv(failures));
    ZC_IF_SOME(values, sorted) {
      return ir::IrOperationResult<VerifiedValue>::irInvariantRejected(zc::mv(values));
    }
  }
  ZC_UNREACHABLE
}

bool isScalarLiteral(ast::SyntaxKind kind) noexcept {
  switch (kind) {
    case ast::SyntaxKind::NullLiteral:
    case ast::SyntaxKind::BoolLiteral:
    case ast::SyntaxKind::IntLiteral:
    case ast::SyntaxKind::FloatLiteralExpr:
    case ast::SyntaxKind::BigIntLiteral:
    case ast::SyntaxKind::StringLiteralExpr:
    case ast::SyntaxKind::UnitLiteral:
    case ast::SyntaxKind::CharacterLiteralExpr:
    case ast::SyntaxKind::NoSubstitutionTemplateLiteralExpr:
      return true;
    default:
      return false;
  }
}

template <typename Map, typename Key>
zc::Maybe<size_t> factIndex(const Map& map, const Key& key) {
  const auto entries = map.entries();
  for (size_t index = 0; index < entries.size(); ++index) {
    if (entries[index].key == key) return index;
  }
  return zc::none;
}

zc::Maybe<size_t> definitionIndex(const binder::FrozenDefinitionInventoryView& definitions,
                                  identity::DefId definition) {
  const auto entries = definitions.definitions();
  for (size_t index = 0; index < entries.size(); ++index) {
    if (entries[index].definition == definition) return index;
  }
  return zc::none;
}

bool sameConstant(const checker::checked::CanonicalConstValue& left,
                  const checker::checked::CanonicalConstValue& right, identity::ModuleId module,
                  const identity::SemanticIdentityRegistrySet& registries,
                  const type::SemanticTypeStore& semanticTypes) {
  auto leftBytes = checker::signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValue(
      left, module, registries, semanticTypes);
  auto rightBytes = checker::signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValue(
      right, module, registries, semanticTypes);
  if (leftBytes == zc::none || rightBytes == zc::none) return false;
  bool equal = false;
  ZC_IF_SOME(leftValue, leftBytes) {
    ZC_IF_SOME(rightValue, rightBytes) { equal = leftValue.asPtr() == rightValue.asPtr(); }
  }
  return equal;
}

zc::Maybe<HirLinkage> linkage(const checker::signature::ValueSignature& signature) {
  if (signature.abi == zc::none) return HirLinkage::Internal;
  ZC_IF_SOME(abi, signature.abi) {
    switch (abi) {
      case checker::signature::ExternAbi::Cdecl:
        return HirLinkage::ExternalCdecl;
      case checker::signature::ExternAbi::Stdcall:
        return HirLinkage::ExternalStdcall;
      case checker::signature::ExternAbi::ZomNative:
        return HirLinkage::ExternalZomNative;
    }
  }
  return zc::none;
}

zc::Maybe<HirVisibility> visibility(const binder::VisibilityEnvelope& source) {
  if (source.value().is<binder::ModuleVisibility>()) {
    return HirVisibility::module(source.value().get<binder::ModuleVisibility>().module);
  }
  if (source.value().is<binder::ExternalVisibility>()) return HirVisibility::external();
  return zc::none;
}

bool sameVisibility(const HirVisibility& left, const HirVisibility& right) {
  if (left.kind() != right.kind()) return false;
  if (left.kind() == HirVisibilityKind::External) return true;
  return left.visibleModule() == right.visibleModule();
}

HirNodeId hirId(uint32_t ordinal) {
  auto value = HirNodeId::fromOrdinal(ordinal);
  ZC_IF_SOME(id, value) { return id; }
  ZC_UNREACHABLE
}

struct PendingValueDeclaration final {
  identity::DefId definition;
  identity::DefinitionKind definitionKind;
  identity::SemanticTypeId declaredType;
  identity::SemanticTypeId inferredType;
  type::semantic::Mutability mutability;
  HirVisibility visibility;
  HirLinkage linkage;
  identity::SourceSpan declarationSpan;
  identity::SourceSpan patternSpan;
  identity::SourceSpan initializerSpan;
  checker::checked::CanonicalConstValue literal;
  zc::Maybe<checker::checked::CanonicalConstValue> constant;
  zc::Array<uint8_t> orderingKey;
};

void sortPendingDeclarations(zc::Vector<PendingValueDeclaration>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion != 0) {
      const auto& previous = values[insertion - 1];
      const bool less =
          current.declarationSpan.byteStart() < previous.declarationSpan.byteStart() ||
          (current.declarationSpan.byteStart() == previous.declarationSpan.byteStart() &&
           lessBytes(current.orderingKey.asPtr(), previous.orderingKey.asPtr()));
      if (!less) break;
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

bool noUnsupportedFacts(const checker::checked::VerifiedCheckedFacts& facts) {
  return facts.aggregates().size() == 0 && facts.places().size() == 0 &&
         facts.coercions().size() == 0 && facts.casts().size() == 0 && facts.calls().size() == 0 &&
         facts.compoundAssignments().size() == 0 && facts.members().size() == 0 &&
         facts.indexes().size() == 0 && facts.observedOperations().size() == 0 &&
         facts.captures().size() == 0 && facts.markerObligations().size() == 0 &&
         facts.exhaustiveness().size() == 0 && facts.unsafeOperations().size() == 0 &&
         facts.projections().size() == 0 && facts.obligations().size() == 0 &&
         facts.errorUnionShapes().size() == 0 && facts.errorOperators().size() == 0;
}

zc::Maybe<size_t> signatureIndex(
    zc::ArrayPtr<const checker::signature::SemanticSignature> signatures,
    identity::DefId definition) {
  zc::Maybe<size_t> result;
  for (size_t index = 0; index < signatures.size(); ++index) {
    if (signatures[index].definition != definition) continue;
    if (result != zc::none) return zc::none;
    result = index;
  }
  return result;
}

zc::Maybe<size_t> signatureRootIndex(
    zc::ArrayPtr<const module_interface::SignatureRootAuthorization> roots,
    identity::DefId definition) {
  zc::Maybe<size_t> result;
  for (size_t index = 0; index < roots.size(); ++index) {
    if (roots[index].canonicalDefinition != definition) continue;
    if (result != zc::none) return zc::none;
    result = index;
  }
  return result;
}

bool definitionBelongsToModule(identity::DefId definition, identity::ModuleId module,
                               const identity::SemanticIdentityRegistrySet& registries) {
  auto definitionRecord = registries.definitions().lookupRecord(definition);
  auto moduleKey = registries.modules().lookup(module);
  if (definitionRecord == zc::none || moduleKey == zc::none) return false;
  bool same = false;
  ZC_IF_SOME(definitionValue, definitionRecord) {
    ZC_IF_SOME(moduleValue, moduleKey) {
      const auto definitionModule = definitionValue.module().encode();
      const auto expectedModule = moduleValue.encode();
      same = definitionModule.asPtr() == expectedModule.asPtr();
    }
  }
  return same;
}

bool typeExists(identity::SemanticTypeId semanticType,
                const type::SemanticTypeStore& semanticTypes) {
  return semanticTypes.get(semanticType).is<type::SemanticTypeLookup>();
}

void append(zc::Vector<char>& output, zc::StringPtr text) { output.addAll(text); }

void appendDigest(zc::Vector<char>& output, const identity::Sha256Digest& digest) {
  append(output, zc::encodeHex(digest.bytes()));
}

}  // namespace

HirVisibility HirVisibility::module(identity::ModuleId module) noexcept {
  return HirVisibility(ModuleHirVisibility{module});
}

HirVisibility HirVisibility::external() noexcept { return HirVisibility(ExternalHirVisibility{}); }

HirVisibility HirVisibility::clone() const noexcept {
  if (value.is<ModuleHirVisibility>()) { return module(value.get<ModuleHirVisibility>().module); }
  return external();
}

HirVisibilityKind HirVisibility::kind() const noexcept {
  return value.is<ModuleHirVisibility>() ? HirVisibilityKind::Module : HirVisibilityKind::External;
}

zc::Maybe<identity::ModuleId> HirVisibility::visibleModule() const noexcept {
  if (!value.is<ModuleHirVisibility>()) return zc::none;
  return value.get<ModuleHirVisibility>().module;
}

struct HirModuleCandidate::Impl final {
  Impl(VerifiedCheckedModule&& checkedModule, zc::Vector<HirValueDeclaration>&& declarations,
       zc::Vector<HirBindingPattern>&& patterns,
       zc::Vector<HirScalarLiteralExpression>&& expressions) noexcept
      : checkedModule(zc::mv(checkedModule)),
        declarations(zc::mv(declarations)),
        patterns(zc::mv(patterns)),
        expressions(zc::mv(expressions)) {}

  VerifiedCheckedModule checkedModule;
  zc::Vector<HirValueDeclaration> declarations;
  zc::Vector<HirBindingPattern> patterns;
  zc::Vector<HirScalarLiteralExpression> expressions;
};

HirModuleCandidate::HirModuleCandidate(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
HirModuleCandidate::~HirModuleCandidate() noexcept(false) = default;
HirModuleCandidate::HirModuleCandidate(HirModuleCandidate&&) noexcept = default;
HirModuleCandidate& HirModuleCandidate::operator=(HirModuleCandidate&&) noexcept = default;

struct VerifiedHirModule::Impl final {
  Impl(identity::SemanticContextBrand semanticContext,
       identity::SemanticContextFingerprint&& contextFingerprint, identity::PackageId package,
       identity::CrateId crate, identity::ModuleId module,
       const identity::Sha256Digest& sourceContentDigest,
       const binder::ParsedModuleReceipt& parsedModuleReceipt,
       const checker::checked::CheckedFactsRevision& checkedFactsRevision,
       const checker::dispatch::DispatchFactsRevision& dispatchFactsRevision,
       const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision,
       ModuleInterfaceLineage&& ownInterface,
       zc::Vector<ModuleInterfaceLineage>&& visibleImportedInterfaces,
       checker::checked::CheckedEvidenceLease&& checkedEvidenceLease,
       driver::borrow_evidence::VerifiedBorrowEvidenceLease&& borrowEvidenceLease,
       const checker::checked::CheckedFactsRepository& checkedRepository,
       const driver::borrow_evidence::BorrowEvidenceRepository& borrowEvidenceRepository,
       const identity::SemanticIdentityRegistrySet& registries,
       const type::SemanticTypeStore& semanticTypes, zc::Vector<HirValueDeclaration>&& declarations,
       zc::Vector<HirBindingPattern>&& patterns,
       zc::Vector<HirScalarLiteralExpression>&& expressions) noexcept
      : semanticContext(semanticContext),
        contextFingerprint(zc::mv(contextFingerprint)),
        package(package),
        crate(crate),
        module(module),
        sourceContentDigest(sourceContentDigest),
        parsedModuleReceipt(parsedModuleReceipt.digest()),
        checkedFactsRevision(checkedFactsRevision),
        dispatchFactsRevision(dispatchFactsRevision),
        borrowEvidenceRevision(borrowEvidenceRevision),
        ownInterface(zc::mv(ownInterface)),
        visibleImportedInterfaces(zc::mv(visibleImportedInterfaces)),
        checkedEvidenceLease(zc::mv(checkedEvidenceLease)),
        borrowEvidenceLease(zc::mv(borrowEvidenceLease)),
        checkedRepository(checkedRepository),
        borrowEvidenceRepository(borrowEvidenceRepository),
        registries(registries),
        semanticTypes(semanticTypes),
        declarations(zc::mv(declarations)),
        patterns(zc::mv(patterns)),
        expressions(zc::mv(expressions)) {}

  identity::SemanticContextBrand semanticContext;
  identity::SemanticContextFingerprint contextFingerprint;
  identity::PackageId package;
  identity::CrateId crate;
  identity::ModuleId module;
  identity::Sha256Digest sourceContentDigest;
  identity::Sha256Digest parsedModuleReceipt;
  checker::checked::CheckedFactsRevision checkedFactsRevision;
  checker::dispatch::DispatchFactsRevision dispatchFactsRevision;
  driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision;
  ModuleInterfaceLineage ownInterface;
  zc::Vector<ModuleInterfaceLineage> visibleImportedInterfaces;
  checker::checked::CheckedEvidenceLease checkedEvidenceLease;
  driver::borrow_evidence::VerifiedBorrowEvidenceLease borrowEvidenceLease;
  const checker::checked::CheckedFactsRepository& checkedRepository;
  const driver::borrow_evidence::BorrowEvidenceRepository& borrowEvidenceRepository;
  const identity::SemanticIdentityRegistrySet& registries;
  const type::SemanticTypeStore& semanticTypes;
  zc::Vector<HirValueDeclaration> declarations;
  zc::Vector<HirBindingPattern> patterns;
  zc::Vector<HirScalarLiteralExpression> expressions;
};

VerifiedHirModule::VerifiedHirModule(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedHirModule::~VerifiedHirModule() noexcept(false) = default;
VerifiedHirModule::VerifiedHirModule(VerifiedHirModule&&) noexcept = default;
VerifiedHirModule& VerifiedHirModule::operator=(VerifiedHirModule&&) noexcept = default;

identity::SemanticContextBrand VerifiedHirModule::semanticContext() const noexcept {
  return impl->semanticContext;
}

const identity::SemanticContextFingerprint& VerifiedHirModule::contextFingerprint() const noexcept {
  return impl->contextFingerprint;
}

identity::PackageId VerifiedHirModule::package() const noexcept { return impl->package; }
identity::CrateId VerifiedHirModule::crate() const noexcept { return impl->crate; }
identity::ModuleId VerifiedHirModule::module() const noexcept { return impl->module; }

const identity::Sha256Digest& VerifiedHirModule::sourceContentDigest() const noexcept {
  return impl->sourceContentDigest;
}

const identity::Sha256Digest& VerifiedHirModule::parsedModuleReceiptDigest() const noexcept {
  return impl->parsedModuleReceipt;
}

const checker::checked::CheckedFactsRevision& VerifiedHirModule::checkedFactsRevision()
    const noexcept {
  return impl->checkedFactsRevision;
}

const checker::dispatch::DispatchFactsRevision& VerifiedHirModule::dispatchFactsRevision()
    const noexcept {
  return impl->dispatchFactsRevision;
}

const driver::borrow_evidence::BorrowEvidenceRevision& VerifiedHirModule::borrowEvidenceRevision()
    const noexcept {
  return impl->borrowEvidenceRevision;
}

const ModuleInterfaceLineage& VerifiedHirModule::ownInterface() const noexcept {
  return impl->ownInterface;
}

zc::ArrayPtr<const ModuleInterfaceLineage> VerifiedHirModule::visibleImportedInterfaces()
    const noexcept {
  return impl->visibleImportedInterfaces.asPtr();
}

const checker::checked::CheckedEvidenceLease& VerifiedHirModule::checkedEvidenceLease()
    const noexcept {
  return impl->checkedEvidenceLease;
}

const driver::borrow_evidence::VerifiedBorrowEvidenceLease& VerifiedHirModule::borrowEvidenceLease()
    const noexcept {
  return impl->borrowEvidenceLease;
}

const driver::borrow_evidence::BorrowEvidenceRepository&
VerifiedHirModule::borrowEvidenceRepository() const noexcept {
  return impl->borrowEvidenceRepository;
}

const identity::SemanticIdentityRegistrySet& VerifiedHirModule::registries() const noexcept {
  return impl->registries;
}

const type::SemanticTypeStore& VerifiedHirModule::semanticTypes() const noexcept {
  return impl->semanticTypes;
}

zc::ArrayPtr<const HirValueDeclaration> VerifiedHirModule::declarations() const noexcept {
  return impl->declarations.asPtr();
}

zc::ArrayPtr<const HirBindingPattern> VerifiedHirModule::patterns() const noexcept {
  return impl->patterns.asPtr();
}

zc::ArrayPtr<const HirScalarLiteralExpression> VerifiedHirModule::expressions() const noexcept {
  return impl->expressions.asPtr();
}

zc::Maybe<zc::String> VerifiedHirModule::dump() const {
  auto moduleKey = impl->registries.modules().lookup(impl->module);
  const auto borrowEvidence = impl->borrowEvidenceRepository.lookup(impl->borrowEvidenceLease);
  if (moduleKey == zc::none ||
      impl->checkedRepository.lookup(impl->checkedEvidenceLease) == zc::none ||
      !borrowEvidence.isResolved() ||
      borrowEvidence.evidence().revision().digest() != impl->borrowEvidenceRevision.digest() ||
      impl->borrowEvidenceLease.key().revision.digest() != impl->borrowEvidenceRevision.digest()) {
    return zc::none;
  }
  zc::Vector<char> output;
  append(output, "zom.hir.v0\nmodule "_zc);
  ZC_IF_SOME(key, moduleKey) { append(output, zc::encodeHex(key.encode().asPtr())); }
  append(output, "\ncontext "_zc);
  appendDigest(output, impl->contextFingerprint.digest());
  append(output, "\nchecked "_zc);
  appendDigest(output, impl->checkedFactsRevision.digest());
  append(output, "\nsource "_zc);
  appendDigest(output, impl->sourceContentDigest);
  append(output, "\nparsed "_zc);
  appendDigest(output, impl->parsedModuleReceipt);
  append(output, "\ndispatch "_zc);
  appendDigest(output, impl->dispatchFactsRevision.digest());
  append(output, "\nborrow-evidence "_zc);
  appendDigest(output, impl->borrowEvidenceRevision.digest());
  append(output, "\ninterface "_zc);
  appendDigest(output, impl->ownInterface.revision);
  append(output, "\n"_zc);
  for (const auto& imported : impl->visibleImportedInterfaces) {
    auto importedModule = impl->registries.modules().lookup(imported.module);
    if (importedModule == zc::none) { return zc::none; }
    append(output, "import-interface "_zc);
    ZC_IF_SOME(key, importedModule) { append(output, zc::encodeHex(key.encode().asPtr())); }
    append(output, " "_zc);
    appendDigest(output, imported.revision);
    append(output, "\n"_zc);
  }

  for (const auto& declaration : impl->declarations) {
    auto definition = impl->registries.definitions().lookup(declaration.definition);
    auto semanticType = impl->semanticTypes.get(declaration.inferredType);
    if (definition == zc::none || !semanticType.is<type::SemanticTypeLookup>()) return zc::none;
    append(output, "decl h"_zc);
    append(output, zc::str(declaration.node.ordinal()));
    append(output, " def="_zc);
    ZC_IF_SOME(key, definition) { append(output, zc::encodeHex(key.encode().asPtr())); }
    append(output, " type="_zc);
    append(output, zc::encodeHex(semanticType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, " pattern=h"_zc);
    append(output, zc::str(declaration.pattern.ordinal()));
    append(output, " initializer=h"_zc);
    append(output, zc::str(declaration.initializer.ordinal()));
    append(output, "\n"_zc);
  }
  for (const auto& pattern : impl->patterns) {
    append(output, "pattern h"_zc);
    append(output, zc::str(pattern.node.ordinal()));
    append(output, " binding="_zc);
    auto definition = impl->registries.definitions().lookup(pattern.binding);
    if (definition == zc::none) return zc::none;
    ZC_IF_SOME(key, definition) { append(output, zc::encodeHex(key.encode().asPtr())); }
    append(output, "\n"_zc);
  }
  for (const auto& expression : impl->expressions) {
    append(output, "literal h"_zc);
    append(output, zc::str(expression.node.ordinal()));
    append(output, " value="_zc);
    auto encoded = checker::signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValue(
        expression.value, impl->module, impl->registries, impl->semanticTypes);
    if (encoded == zc::none) return zc::none;
    ZC_IF_SOME(bytes, encoded) { append(output, zc::encodeHex(bytes.asPtr())); }
    append(output, "\n"_zc);
  }
  return zc::str(output.releaseAsArray());
}

ir::IrOperationResult<HirModuleCandidate> HirBuilder::build(VerifiedCheckedModule&& checkedModule) {
  const auto module = checkedModule.module();
  const auto& registries = checkedModule.registries();
  const auto& facts = checkedModule.checkedFacts();
  const auto& bound = checkedModule.boundModule();
  const auto borrowEvidence =
      checkedModule.borrowEvidenceRepository().lookup(checkedModule.borrowEvidenceLease());
  if (checkedModule.checkedRepository().lookup(checkedModule.checkedEvidenceLease()) == zc::none ||
      !borrowEvidence.isResolved() ||
      borrowEvidence.evidence().revision().digest() !=
          checkedModule.borrowEvidenceRevision().digest() ||
      checkedModule.borrowEvidenceLease().key().revision.digest() !=
          checkedModule.borrowEvidenceRevision().digest() ||
      checkedModule.dispatchFacts().facts().size() != 0 || !noUnsupportedFacts(facts)) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 0);
  }

  const auto definitions = bound.definitions().definitions();
  if (definitions.size() > UINT32_MAX / 3) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::InvalidFact, module, registries, 1);
  }
  zc::Vector<PendingValueDeclaration> pending;
  const auto& ownInterface = checkedModule.ownModuleInterface();
  const auto& signatures = ownInterface.signatures();
  for (size_t definitionIndex = 0; definitionIndex < definitions.size(); ++definitionIndex) {
    const auto ordinal = static_cast<uint32_t>(definitionIndex);
    const auto& definition = definitions[ordinal];
    if (definition.record.kind() != identity::DefinitionKind::Static &&
        definition.record.kind() != identity::DefinitionKind::Constant) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::MissingRequiredFact, module,
                                           registries, ordinal + 2);
    }
    const auto& site = definition.site.value();
    if (!site.is<binder::PatternBindingSite>()) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    const auto& bindingSite = site.get<binder::PatternBindingSite>();
    const auto& tree = bound.tree();
    if (bindingSite.patternPath.size() != 0 || !tree.contains(bindingSite.introducer) ||
        tree.node(bindingSite.introducer).kind != ast::SyntaxKind::VariableDeclarator ||
        !tree.contains(definition.node) ||
        tree.node(definition.node).kind != ast::SyntaxKind::IdentifierPattern) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    const auto& declarator = tree.node(bindingSite.introducer);
    const ast::NodeId annotation(declarator.payload.words[ast::kVariableDeclaratorTyWord]);
    const ast::NodeId initializer(declarator.payload.words[ast::kVariableDeclaratorInitWord]);
    if (tree.contains(annotation) || !tree.contains(initializer) ||
        !isScalarLiteral(tree.node(initializer).kind)) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::MissingRequiredFact, module,
                                           registries, ordinal + 2);
    }

    auto definitionTypeIndex = factIndex(facts.definitionTypes(), definition.definition);
    auto patternIndex = factIndex(facts.patterns(), definition.node);
    auto nodeTypeIndex = factIndex(facts.nodeTypes(), initializer);
    auto literalIndex = factIndex(facts.literals(), initializer);
    auto signaturePosition = signatureIndex(signatures.definitions.asPtr(), definition.definition);
    auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), definition.definition);
    auto declarationSpan = bound.parsedModule().spanFor(declarator.range);
    auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
    if (definitionTypeIndex == zc::none || patternIndex == zc::none || nodeTypeIndex == zc::none ||
        literalIndex == zc::none || signaturePosition == zc::none || rootPosition == zc::none ||
        declarationSpan == zc::none || initializerSpan == zc::none) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::MissingRequiredFact, module,
                                           registries, ordinal + 2);
    }

    size_t definitionTypeSlot = 0;
    size_t patternSlot = 0;
    size_t nodeTypeSlot = 0;
    size_t literalSlot = 0;
    size_t signatureSlot = 0;
    size_t rootSlot = 0;
    ZC_IF_SOME(value, definitionTypeIndex) { definitionTypeSlot = value; }
    ZC_IF_SOME(value, patternIndex) { patternSlot = value; }
    ZC_IF_SOME(value, nodeTypeIndex) { nodeTypeSlot = value; }
    ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
    ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
    ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
    const auto& definitionType = facts.definitionTypes().entries()[definitionTypeSlot];
    const auto& pattern = facts.patterns().entries()[patternSlot].value;
    const auto& nodeType = facts.nodeTypes().entries()[nodeTypeSlot];
    const auto& literal = facts.literals().entries()[literalSlot].value;
    const auto& signature = signatures.definitions[signatureSlot];
    const auto& root = signatures.roots[rootSlot];
    if (!signature.payload.variant().is<checker::signature::ValueSignature>() ||
        !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    const auto& valueSignature =
        signature.payload.variant().get<checker::signature::ValueSignature>();
    auto declarationLinkage = linkage(valueSignature);
    auto declarationVisibility = visibility(root.visibility);
    if (declarationLinkage == zc::none || declarationVisibility == zc::none ||
        signature.definitionKind != definition.record.kind() || root.sourceModule != module ||
        root.canonicalDefinition != definition.definition || !valueSignature.hasInitializer ||
        valueSignature.type != definitionType.value || definitionType.value != nodeType.value ||
        literal.type != nodeType.value || pattern.scrutineeType != definitionType.value ||
        pattern.bindings.size() != 1 || pattern.bindings[0].binding != definition.definition ||
        pattern.bindings[0].type != definitionType.value || pattern.refinements.size() != 0 ||
        !pattern.constructor.variant().is<checker::checked::WildcardPattern>() ||
        !pattern.reachable || pattern.guardMayRaise != zc::none ||
        !sameSpan(signature.declarationSpan, definition.source)) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }

    auto constantIndex = factIndex(facts.constants(), definition.definition);
    zc::Maybe<checker::checked::CanonicalConstValue> constant;
    if (definition.record.kind() == identity::DefinitionKind::Constant) {
      if (constantIndex == zc::none || valueSignature.constantValue == zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      size_t constantSlot = 0;
      ZC_IF_SOME(value, constantIndex) { constantSlot = value; }
      const auto& evaluated = facts.constants().entries()[constantSlot].value;
      bool signatureMatches = false;
      ZC_IF_SOME(signatureValue, valueSignature.constantValue) {
        signatureMatches = sameConstant(signatureValue, evaluated.value, module, registries,
                                        checkedModule.semanticTypes());
      }
      if (evaluated.expression != initializer || evaluated.type != definitionType.value ||
          evaluated.dependencies.size() != 0 ||
          !sameConstant(evaluated.value, literal.literal, module, registries,
                        checkedModule.semanticTypes()) ||
          !signatureMatches) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      constant = evaluated.value.clone();
    } else if (constantIndex != zc::none || valueSignature.constantValue != zc::none) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::AdditionalFact, module, registries,
                                           ordinal + 2);
    }

    auto definitionKey = registries.definitions().lookup(definition.definition);
    if (definitionKey == zc::none ||
        !typeExists(definitionType.value, checkedModule.semanticTypes())) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    zc::Array<uint8_t> orderingKey;
    ZC_IF_SOME(key, definitionKey) { orderingKey = key.encode(); }
    HirVisibility visibilityValue = HirVisibility::external();
    HirLinkage linkageValue = HirLinkage::Internal;
    identity::SourceSpan declarationSpanValue = definition.source.clone();
    identity::SourceSpan initializerSpanValue = literal.sourceSpan.clone();
    ZC_IF_SOME(value, declarationVisibility) { visibilityValue = zc::mv(value); }
    ZC_IF_SOME(value, declarationLinkage) { linkageValue = value; }
    ZC_IF_SOME(value, declarationSpan) { declarationSpanValue = value.clone(); }
    ZC_IF_SOME(value, initializerSpan) { initializerSpanValue = value.clone(); }
    if (!sameSpan(literal.sourceSpan, initializerSpanValue)) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    pending.add(PendingValueDeclaration{
        definition.definition, definition.record.kind(), valueSignature.type, definitionType.value,
        valueSignature.mutability, zc::mv(visibilityValue), linkageValue,
        declarationSpanValue.clone(), definition.source.clone(), literal.sourceSpan.clone(),
        literal.literal.clone(), zc::mv(constant), zc::mv(orderingKey)});
  }

  if (facts.nodeTypes().size() != pending.size() ||
      facts.definitionTypes().size() != pending.size() ||
      facts.literals().size() != pending.size() || facts.patterns().size() != pending.size()) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 1);
  }
  size_t expectedConstants = 0;
  for (const auto& value : pending) {
    if (value.definitionKind == identity::DefinitionKind::Constant) ++expectedConstants;
  }
  if (facts.constants().size() != expectedConstants) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 2);
  }

  sortPendingDeclarations(pending);
  zc::Vector<HirValueDeclaration> declarations;
  zc::Vector<HirBindingPattern> patterns;
  zc::Vector<HirScalarLiteralExpression> expressions;
  uint32_t next = 1;
  for (auto& value : pending) {
    const auto declarationId = hirId(next++);
    const auto patternId = hirId(next++);
    const auto initializerId = hirId(next++);
    zc::Maybe<checker::checked::CanonicalConstValue> constant;
    ZC_IF_SOME(constantValue, value.constant) { constant = constantValue.clone(); }
    declarations.add(HirValueDeclaration{
        declarationId, value.definition, value.definitionKind, value.declaredType,
        value.inferredType, value.mutability, value.visibility.clone(), value.linkage,
        value.declarationSpan.clone(), patternId, initializerId, zc::mv(constant)});
    patterns.add(HirBindingPattern{patternId, value.definition, value.inferredType,
                                   value.inferredType, true, value.patternSpan.clone()});
    expressions.add(HirScalarLiteralExpression{initializerId, value.inferredType,
                                               value.literal.clone(), HirValueCategory::Value,
                                               value.initializerSpan.clone()});
  }

  auto impl = zc::heap<HirModuleCandidate::Impl>(zc::mv(checkedModule), zc::mv(declarations),
                                                 zc::mv(patterns), zc::mv(expressions));
  return ir::IrOperationResult<HirModuleCandidate>::verified(HirModuleCandidate(zc::mv(impl)));
}

ir::IrOperationResult<VerifiedHirModule> HirVerifier::verify(HirModuleCandidate&& candidate) {
  const auto module = candidate.impl->checkedModule.module();
  const auto& registries = candidate.impl->checkedModule.registries();
  const auto& semanticTypes = candidate.impl->checkedModule.semanticTypes();
  const auto& facts = candidate.impl->checkedModule.checkedFacts();
  const auto& definitions = candidate.impl->checkedModule.boundModule().definitions();
  const auto& signatures = candidate.impl->checkedModule.ownModuleInterface().signatures();
  const auto declarationCount = candidate.impl->declarations.size();
  const auto borrowEvidence = candidate.impl->checkedModule.borrowEvidenceRepository().lookup(
      candidate.impl->checkedModule.borrowEvidenceLease());
  if (candidate.impl->checkedModule.checkedRepository().lookup(
          candidate.impl->checkedModule.checkedEvidenceLease()) == zc::none ||
      !borrowEvidence.isResolved() ||
      borrowEvidence.evidence().revision().digest() !=
          candidate.impl->checkedModule.borrowEvidenceRevision().digest() ||
      candidate.impl->checkedModule.borrowEvidenceLease().key().revision.digest() !=
          candidate.impl->checkedModule.borrowEvidenceRevision().digest() ||
      candidate.impl->checkedModule.dispatchFacts().facts().size() != 0 ||
      !noUnsupportedFacts(facts) || candidate.impl->patterns.size() != declarationCount ||
      candidate.impl->expressions.size() != declarationCount ||
      definitions.definitions().size() != declarationCount ||
      facts.definitionTypes().size() != declarationCount ||
      facts.nodeTypes().size() != declarationCount || facts.literals().size() != declarationCount ||
      facts.patterns().size() != declarationCount) {
    return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                        ir::IrFailureKind::InputRevisionMismatch, module,
                                        registries, 0);
  }

  size_t expectedConstantCount = 0;
  for (const auto& declaration : candidate.impl->declarations) {
    if (declaration.definitionKind == identity::DefinitionKind::Constant) {
      ++expectedConstantCount;
    }
  }
  if (facts.constants().size() != expectedConstantCount) {
    return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                        ir::IrFailureKind::AdditionalFact, module, registries, 0);
  }

  for (size_t sourceIndex = 0; sourceIndex < declarationCount; ++sourceIndex) {
    const auto index = static_cast<uint32_t>(sourceIndex);
    const auto& declaration = candidate.impl->declarations[index];
    const auto& pattern = candidate.impl->patterns[index];
    const auto& expression = candidate.impl->expressions[index];
    const uint32_t expectedDeclaration = index * 3 + 1;
    if (declaration.node.ordinal() != expectedDeclaration ||
        pattern.node.ordinal() != expectedDeclaration + 1 ||
        expression.node.ordinal() != expectedDeclaration + 2 ||
        declaration.pattern != pattern.node || declaration.initializer != expression.node ||
        pattern.binding != declaration.definition || declaration.inferredType != pattern.type ||
        pattern.type != pattern.scrutineeType || declaration.inferredType != expression.type ||
        expression.category != HirValueCategory::Value || !pattern.reachable ||
        !definitionBelongsToModule(declaration.definition, module, registries) ||
        !typeExists(declaration.declaredType, semanticTypes) ||
        !typeExists(declaration.inferredType, semanticTypes) ||
        (index != 0 && declaration.sourceSpan.byteStart() <
                           candidate.impl->declarations[index - 1].sourceSpan.byteStart())) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }

    auto sourceDefinitionIndex = definitionIndex(definitions, declaration.definition);
    if (sourceDefinitionIndex == zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::AdditionalFact, module, registries,
                                          index + 1);
    }
    size_t definitionSlot = 0;
    ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
    const auto& sourceDefinition = definitions.definitions()[definitionSlot];
    if (!sourceDefinition.site.value().is<binder::PatternBindingSite>() ||
        sourceDefinition.record.kind() != declaration.definitionKind) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto& bindingSite = sourceDefinition.site.value().get<binder::PatternBindingSite>();
    const auto& tree = candidate.impl->checkedModule.boundModule().tree();
    if (bindingSite.patternPath.size() != 0 || !tree.contains(bindingSite.introducer) ||
        tree.node(bindingSite.introducer).kind != ast::SyntaxKind::VariableDeclarator ||
        !tree.contains(sourceDefinition.node) ||
        tree.node(sourceDefinition.node).kind != ast::SyntaxKind::IdentifierPattern) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto& declarator = tree.node(bindingSite.introducer);
    const ast::NodeId annotation(declarator.payload.words[ast::kVariableDeclaratorTyWord]);
    const ast::NodeId initializer(declarator.payload.words[ast::kVariableDeclaratorInitWord]);
    if (tree.contains(annotation) || !tree.contains(initializer) ||
        !isScalarLiteral(tree.node(initializer).kind)) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    auto declarationSourceSpan =
        candidate.impl->checkedModule.boundModule().parsedModule().spanFor(declarator.range);
    auto initializerSourceSpan = candidate.impl->checkedModule.boundModule().parsedModule().spanFor(
        tree.node(initializer).range);
    auto definitionTypeIndex = factIndex(facts.definitionTypes(), declaration.definition);
    auto patternFactIndex = factIndex(facts.patterns(), sourceDefinition.node);
    auto nodeTypeIndex = factIndex(facts.nodeTypes(), initializer);
    auto literalIndex = factIndex(facts.literals(), initializer);
    auto signaturePosition = signatureIndex(signatures.definitions.asPtr(), declaration.definition);
    auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), declaration.definition);
    if (definitionTypeIndex == zc::none || patternFactIndex == zc::none ||
        nodeTypeIndex == zc::none || literalIndex == zc::none || signaturePosition == zc::none ||
        rootPosition == zc::none || declarationSourceSpan == zc::none ||
        initializerSourceSpan == zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::MissingRequiredFact, module,
                                          registries, index + 1);
    }
    size_t definitionTypeSlot = 0;
    size_t patternFactSlot = 0;
    size_t nodeTypeSlot = 0;
    size_t literalSlot = 0;
    size_t signatureSlot = 0;
    size_t rootSlot = 0;
    ZC_IF_SOME(value, definitionTypeIndex) { definitionTypeSlot = value; }
    ZC_IF_SOME(value, patternFactIndex) { patternFactSlot = value; }
    ZC_IF_SOME(value, nodeTypeIndex) { nodeTypeSlot = value; }
    ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
    ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
    ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
    const auto& definitionType = facts.definitionTypes().entries()[definitionTypeSlot].value;
    const auto& patternFact = facts.patterns().entries()[patternFactSlot].value;
    const auto& nodeType = facts.nodeTypes().entries()[nodeTypeSlot].value;
    const auto& literalFact = facts.literals().entries()[literalSlot].value;
    const auto& signature = signatures.definitions[signatureSlot];
    const auto& root = signatures.roots[rootSlot];
    if (!signature.payload.variant().is<checker::signature::ValueSignature>() ||
        !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto& valueSignature =
        signature.payload.variant().get<checker::signature::ValueSignature>();
    auto expectedVisibility = visibility(root.visibility);
    auto expectedLinkage = linkage(valueSignature);
    bool visibilityMatches = false;
    bool linkageMatches = false;
    bool declarationSpanMatches = false;
    bool initializerSpanMatches = false;
    ZC_IF_SOME(value, expectedVisibility) {
      visibilityMatches = sameVisibility(declaration.visibility, value);
    }
    ZC_IF_SOME(value, expectedLinkage) { linkageMatches = declaration.linkage == value; }
    ZC_IF_SOME(value, declarationSourceSpan) {
      declarationSpanMatches = sameSpan(declaration.sourceSpan, value);
    }
    ZC_IF_SOME(value, initializerSourceSpan) {
      initializerSpanMatches = sameSpan(expression.sourceSpan, value);
    }
    if (declaration.inferredType != definitionType || expression.type != nodeType ||
        pattern.type != patternFact.scrutineeType || patternFact.bindings.size() != 1 ||
        patternFact.bindings[0].binding != declaration.definition ||
        patternFact.bindings[0].type != pattern.type ||
        !sameConstant(expression.value, literalFact.literal, module, registries, semanticTypes) ||
        !sameSpan(expression.sourceSpan, literalFact.sourceSpan) ||
        !sameSpan(pattern.sourceSpan, sourceDefinition.source) || !declarationSpanMatches ||
        !initializerSpanMatches || !visibilityMatches || !linkageMatches ||
        signature.definition != declaration.definition ||
        signature.definitionKind != declaration.definitionKind ||
        !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
        root.canonicalDefinition != declaration.definition || root.sourceModule != module ||
        valueSignature.type != declaration.declaredType ||
        declaration.declaredType != declaration.inferredType ||
        valueSignature.mutability != declaration.mutability || !valueSignature.hasInitializer ||
        patternFact.refinements.size() != 0 ||
        !patternFact.constructor.variant().is<checker::checked::WildcardPattern>() ||
        !patternFact.reachable || patternFact.guardMayRaise != zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }

    auto constantIndex = factIndex(facts.constants(), declaration.definition);
    if (declaration.definitionKind == identity::DefinitionKind::Constant) {
      if (constantIndex == zc::none || declaration.constantValue == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t constantSlot = 0;
      ZC_IF_SOME(value, constantIndex) { constantSlot = value; }
      bool same = false;
      ZC_IF_SOME(value, declaration.constantValue) {
        same = sameConstant(value, facts.constants().entries()[constantSlot].value.value, module,
                            registries, semanticTypes);
      }
      bool signatureConstantMatches = false;
      ZC_IF_SOME(value, valueSignature.constantValue) {
        signatureConstantMatches =
            sameConstant(value, facts.constants().entries()[constantSlot].value.value, module,
                         registries, semanticTypes);
      }
      const auto& constantFact = facts.constants().entries()[constantSlot].value;
      if (!same || !signatureConstantMatches || constantFact.expression != initializer ||
          constantFact.type != declaration.inferredType || constantFact.dependencies.size() != 0) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
    } else if (constantIndex != zc::none || declaration.constantValue != zc::none ||
               valueSignature.constantValue != zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::AdditionalFact, module, registries,
                                          index + 1);
    }
  }

  auto& checked = candidate.impl->checkedModule;
  zc::Vector<ModuleInterfaceLineage> imported;
  for (const auto& interface : checked.visibleImportedInterfaces()) {
    imported.add(ModuleInterfaceLineage{interface.module, interface.revision});
  }
  const ModuleInterfaceLineage ownInterface{checked.ownInterface().module,
                                            checked.ownInterface().revision};
  auto impl = zc::heap<VerifiedHirModule::Impl>(
      checked.semanticContext(), checked.contextFingerprint().clone(), checked.package(),
      checked.crate(), checked.module(), checked.sourceContentDigest(),
      checked.parsedModuleReceipt(), checked.checkedFactsRevision(),
      checked.dispatchFactsRevision(), checked.borrowEvidenceRevision(),
      ModuleInterfaceLineage{ownInterface.module, ownInterface.revision}, zc::mv(imported),
      checked.releaseCheckedEvidenceLease(), checked.releaseBorrowEvidenceLease(),
      checked.checkedRepository(), checked.borrowEvidenceRepository(), checked.registries(),
      checked.semanticTypes(), zc::mv(candidate.impl->declarations),
      zc::mv(candidate.impl->patterns), zc::mv(candidate.impl->expressions));
  return ir::IrOperationResult<VerifiedHirModule>::verified(VerifiedHirModule(zc::mv(impl)));
}

}  // namespace zomlang::compiler::hir
