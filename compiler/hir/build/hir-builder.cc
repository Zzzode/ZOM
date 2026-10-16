// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include <cstdint>

#include "compiler/ast/generated/node-payload.h"
#include "compiler/ast/generated/node-traverse.h"
#include "compiler/binder/metadata/definition-inventory.h"
#include "compiler/binder/metadata/definition-site.h"
#include "compiler/binder/metadata/immutable-binding-metadata.h"
#include "compiler/binder/metadata/immutable-definition-inventory.h"
#include "compiler/checker/facts/signature-facts.h"
#include "compiler/hir/build/hir-fn-builder.h"
#include "compiler/hir/build/hir-pending.h"
#include "compiler/hir/build/lower-expr-aggregate.h"
#include "compiler/hir/build/lower-expr-binary.h"
#include "compiler/hir/build/lower-expr-borrow.h"
#include "compiler/hir/build/lower-expr-call.h"
#include "compiler/hir/build/lower-expr-field-arithmetic.h"
#include "compiler/hir/build/lower-generic.h"
#include "compiler/hir/build/lower-stmt-control.h"
#include "compiler/hir/build/lower-stmt-discarded-call.h"
#include "compiler/hir/build/lower-stmt-write.h"
#include "compiler/hir/hir-candidate-impl.h"
#include "compiler/hir/hir-internal.h"
#include "compiler/hir/hir-shape.h"
#include "compiler/identity/key/definition-key.h"
#include "compiler/ownership/admission/surface-admission.h"
#include "compiler/type/semantic-type-data.h"
#include "compiler/type/semantic-type-store.h"

namespace zomlang::compiler::hir {
namespace detail {
namespace {

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) return left[index] < right[index];
  }
  return left.size() < right.size();
}

/// Compiler-generated conjunction: every Maybe argument is zc::none.
/// Replaces hand-written 10+ field `== zc::none` chains in shape guards.
template <typename... Ts>
constexpr bool allNone(const Ts&... maybes) {
  return (... && (maybes == zc::none));
}

/// Compiler-generated conjunction: every Vector argument is empty.
template <typename... Ts>
constexpr bool allEmpty(const Ts&... vectors) {
  return (... && (vectors.size() == 0));
}

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

void sortPendingFunctions(zc::Vector<PendingFunctionDeclaration>& values) {
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

// Returns true when the interned semantic type is the canonical Unit primitive,
// the success type of a result-less method such as a mutating `set` body.
bool isUnitSemanticType(const type::SemanticTypeStore& store, identity::SemanticTypeId id) {
  auto lookup = store.get(id);
  return lookup.is<type::SemanticTypeLookup>() &&
         lookup.get<type::SemanticTypeLookup>().data().is<type::semantic::PrimitiveTypeData>() &&
         lookup.get<type::SemanticTypeLookup>()
                 .data()
                 .get<type::semantic::PrimitiveTypeData>()
                 .kind == type::semantic::PrimitiveKind::Unit;
}

// Returns the primitive kind of an interned semantic type, or none when the
// type is not a primitive. Used by the unary-return desugaring to pick the
// synthetic constant operand that matches the operand type.
zc::Maybe<type::semantic::PrimitiveKind> primitiveKindOf(const type::SemanticTypeStore& store,
                                                         identity::SemanticTypeId id) {
  auto lookup = store.get(id);
  if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  if (!data.is<type::semantic::PrimitiveTypeData>()) return zc::none;
  return data.get<type::semantic::PrimitiveTypeData>().kind;
}

// Returns true when the interned semantic type is an existential (dyn) type.
// The HIR/MIR erasure carrier (existential locals, vtable construction) is not
// built yet, so a function whose return type is existential is rejected as an
// unsupported construct (ZOM4099), never silently scalar-lowered.
bool isExistentialSemanticType(const type::SemanticTypeStore& store, identity::SemanticTypeId id) {
  auto lookup = store.get(id);
  return lookup.is<type::SemanticTypeLookup>() &&
         lookup.get<type::SemanticTypeLookup>().data().is<type::semantic::ExistentialTypeData>();
}

// Interns a primitive type and returns its semantic id, or none when
// canonicalization or interning fails.  Used to resolve the i32 tag and bool
// comparison-result types for the forced-unwrap (`!!`) CFG.
zc::Maybe<identity::SemanticTypeId> internPrimitiveType(type::SemanticTypeStore& store,
                                                        type::semantic::PrimitiveKind kind) {
  auto canonical =
      store.canonicalizeClosed(type::semantic::TypeData(type::semantic::PrimitiveTypeData{kind}));
  if (!canonical.is<type::semantic::CanonicalTypeData>()) return zc::none;
  auto interned = store.intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
  if (!interned.is<type::SemanticTypeInterned>()) return zc::none;
  return interned.get<type::SemanticTypeInterned>().id;
}

// Desugars a primitive unary operation to an equivalent binary operation with
// a synthetic constant operand. Returns the binary operation, whether the
// synthetic operand is on the left, and the synthetic constant value.
struct PrimitiveUnaryDesugar {
  checker::PrimitiveOperation binaryOperation;
  bool syntheticOnLeft;
  checker::checked::CanonicalConstValue syntheticValue;
};

zc::Maybe<PrimitiveUnaryDesugar> desugarPrimitiveUnary(checker::PrimitiveOperation unaryOperation,
                                                       type::semantic::PrimitiveKind kind) {
  checker::PrimitiveOperation binaryOperation;
  bool syntheticOnLeft = false;
  switch (unaryOperation) {
    case checker::PrimitiveOperation::Neg:
      // -x == 0 - x: synthetic zero on the left.
      binaryOperation = checker::PrimitiveOperation::Sub;
      syntheticOnLeft = true;
      break;
    case checker::PrimitiveOperation::UnaryPlus:
      // +x == x + 0: synthetic zero on the right.
      binaryOperation = checker::PrimitiveOperation::Add;
      break;
    case checker::PrimitiveOperation::BitNot:
      // ~x == x ^ -1: synthetic -1 on the right.
      binaryOperation = checker::PrimitiveOperation::BitXor;
      break;
    case checker::PrimitiveOperation::LogicalNot:
      // !x == x == false: synthetic false on the right.
      binaryOperation = checker::PrimitiveOperation::Eq;
      break;
    default:
      return zc::none;
  }
  zc::Maybe<checker::checked::CanonicalConstValue> syntheticValue;
  switch (unaryOperation) {
    case checker::PrimitiveOperation::Neg:
    case checker::PrimitiveOperation::UnaryPlus: {
      // Zero of the operand type: integer zero or float zero.
      if (kind == type::semantic::PrimitiveKind::F32) {
        syntheticValue = checker::checked::CanonicalConstValue::float32(0);
      } else if (kind == type::semantic::PrimitiveKind::F64) {
        syntheticValue = checker::checked::CanonicalConstValue::float64(0);
      } else {
        syntheticValue =
            checker::checked::CanonicalConstValue::integer(checker::signature::CanonicalInteger{
                checker::signature::IntegerSign::NonNegative, zc::Array<uint8_t>{}});
      }
      break;
    }
    case checker::PrimitiveOperation::BitNot: {
      // -1: Negative sign with magnitude {1}.
      auto magnitude = zc::heapArray<uint8_t>(1);
      magnitude[0] = 1;
      syntheticValue =
          checker::checked::CanonicalConstValue::integer(checker::signature::CanonicalInteger{
              checker::signature::IntegerSign::Negative, zc::mv(magnitude)});
      break;
    }
    case checker::PrimitiveOperation::LogicalNot:
      syntheticValue = checker::checked::CanonicalConstValue::boolean(false);
      break;
    default:
      break;
  }
  if (syntheticValue == zc::none) return zc::none;
  return PrimitiveUnaryDesugar{binaryOperation, syntheticOnLeft,
                               zc::mv(ZC_ASSERT_NONNULL(syntheticValue))};
}

// Builds the implicit `this` receiver parameter of an admitted inherent method.
// The receiver is a borrow of the enclosing nominal (`&Owner` shared,
// `&mut Owner` mutable); the owner nominal comes from the verified member scope
// rather than the bare-`Self` source annotation, which is contextual and does
// not resolve through the ordinary source-type builder. Returns none for every
// shape outside the admitted slice (missing binder fact, generic owner, failed
// canonicalization), keeping the method on the capability drain.
zc::Maybe<HirParameter> buildMethodReceiverParameter(
    const binder::MaterializedDefinitionInventoryEntry& definition, identity::DefId owner,
    const checker::signature::ReceiverSignature& receiver,
    const ownership::AdmittedBoundModule& bound,
    const checker::CheckerIdentityAuthority& identities, type::SemanticTypeStore& semanticTypes) {
  const auto& tree = bound.tree();
  const binder::MaterializedCallableParameterInventoryEntry* receiverEntry = nullptr;
  for (const auto& entry : bound.definitions().callableParameters()) {
    if (entry.record.owner() != definition.key ||
        entry.record.position().kind() != identity::CallableParameterPositionKind::Receiver) {
      continue;
    }
    if (receiverEntry != nullptr) return zc::none;
    receiverEntry = &entry;
  }
  if (receiverEntry == nullptr) return zc::none;
  auto authority = identities.callableParameter(receiverEntry->parameter);
  if (authority == zc::none) return zc::none;
  if (ZC_ASSERT_NONNULL(authority).key() != receiver.parameter) return zc::none;
  if (!tree.contains(receiverEntry->node) ||
      tree.node(receiverEntry->node).kind != ast::SyntaxKind::FunctionParameterDecl) {
    return zc::none;
  }
  if (receiver.mode != checker::signature::ReceiverMode::Shared &&
      receiver.mode != checker::signature::ReceiverMode::Mutable) {
    return zc::none;
  }
  zc::Vector<identity::SemanticTypeId> noArguments;
  auto admittedOwner = semanticTypes.canonicalizeClosed(
      type::semantic::TypeData(type::semantic::NominalTypeData{owner, zc::mv(noArguments)}));
  if (!admittedOwner.is<type::semantic::CanonicalTypeData>()) return zc::none;
  auto internedOwner =
      semanticTypes.intern(zc::mv(admittedOwner).get<type::semantic::CanonicalTypeData>());
  if (!internedOwner.is<type::SemanticTypeInterned>()) return zc::none;
  const auto mutability = receiver.mode == checker::signature::ReceiverMode::Mutable
                              ? type::semantic::Mutability::Mutable
                              : type::semantic::Mutability::Const;
  auto admittedReference =
      semanticTypes.canonicalizeClosed(type::semantic::TypeData(type::semantic::ReferenceTypeData{
          mutability, internedOwner.get<type::SemanticTypeInterned>().id}));
  if (!admittedReference.is<type::semantic::CanonicalTypeData>()) return zc::none;
  auto internedReference =
      semanticTypes.intern(zc::mv(admittedReference).get<type::semantic::CanonicalTypeData>());
  if (!internedReference.is<type::SemanticTypeInterned>()) return zc::none;
  return HirParameter{receiver.parameter.clone(),
                      internedReference.get<type::SemanticTypeInterned>().id,
                      receiverEntry->source.clone()};
}

}  // namespace
}  // namespace detail

using namespace detail;

HirModuleCandidate::HirModuleCandidate(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
HirModuleCandidate::~HirModuleCandidate() noexcept(false) = default;
HirModuleCandidate::HirModuleCandidate(HirModuleCandidate&&) noexcept = default;
HirModuleCandidate& HirModuleCandidate::operator=(HirModuleCandidate&&) noexcept = default;

ir::IrOperationResult<HirModuleCandidate> HirBuilder::build(
    VerifiedCheckedModule&& checkedModule, type::SemanticTypeStore& semanticTypes) {
  const auto module = checkedModule.module();
  const auto registries = checkedModule.retainIdentityAuthority();
  const auto& facts = checkedModule.checkedFacts();
  const auto bound = checkedModule.retainAdmittedBoundModule();
  const auto borrowCapability = checkedModule.borrowEvidenceCapability();
  const auto borrowEvidence = borrowCapability.lookup(checkedModule.borrowEvidenceLease());
  if (registries.semanticContext() != checkedModule.semanticContext() ||
      registries.fingerprint().digest() != checkedModule.contextFingerprint().digest() ||
      registries.boundModule(module) == zc::none ||
      checkedModule.checkedRepository().lookup(checkedModule.checkedEvidenceLease()) == zc::none ||
      !borrowEvidence.isResolved() ||
      borrowEvidence.evidence().revision().digest() !=
          checkedModule.borrowEvidenceRevision().digest() ||
      checkedModule.borrowEvidenceLease().key().revision.digest() !=
          checkedModule.borrowEvidenceRevision().digest() ||
      checkedModule.dispatchFacts().facts().size() != facts.calls().size() ||
      !unsupportedNonErasureFacts(facts)) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 0);
  }
  // Concrete-to-dyn erasure is accepted and verified by the checker but the
  // HIR/MIR erasure carrier (existential locals, vtable construction) is not
  // built yet. Fail closed as a per-definition capability rejection projected
  // to ZOM4099, never as an invariant and never silently scalar-lowered. An
  // AnnotatedInitializer coercion whose erased local is never read (dead
  // erase) is admitted: the binding is skipped during sequential-local-return
  // lowering, so no existential carrier is needed. Any other coercion shape
  // stays an invariant rejection.
  zc::Vector<ast::NodeId> deadEraseInitializers;
  // Devirtualized-erase initializers are live (the erased local is read) but
  // every read is a receiver of a devirtualized method call, so no existential
  // carrier is needed. These are kept (not filtered as dead) but the builder
  // uses the concrete type for the local and lowers the call as a direct impl
  // method call.
  zc::Vector<ast::NodeId> devirtualizedEraseInitializers;
  // Call-argument-erase initializers are live (the erased local is read) but
  // every read is a plain argument of a direct call. The binding stores the
  // existential type and carries an erasure record so the MIR builder emits
  // an Erase rvalue to construct the existential carrier.
  zc::Vector<ast::NodeId> callArgumentEraseInitializers;
  if (facts.coercions().size() != 0) {
    // Validate every coercion before admitting any capability failure, so a
    // malformed entry can never be masked by an earlier well-formed one.
    for (const auto& entry : facts.coercions().entries()) {
      if (!isSingleDynEraseAdjustment(entry.value)) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::AdditionalFact, module, registries,
                                             2);
      }
      if (enclosingExecutableDefinition(bound.tree(), bound.definitions(), entry.key) == zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries, 3);
      }
    }
    // All coercions are well-formed single DynErase steps. Admit a dead-erase
    // AnnotatedInitializer coercion; reject the first non-admitted one's
    // enclosing definition as an unsupported construct.
    for (const auto& entry : facts.coercions().entries()) {
      if (entry.value.site == checker::checked::CoercionSite::AnnotatedInitializer &&
          isDeadErasedInitializer(bound.tree(), bound.definitions(), entry.key)) {
        deadEraseInitializers.add(entry.key);
        continue;
      }
      if (entry.value.site == checker::checked::CoercionSite::AnnotatedInitializer &&
          isDevirtualizedEraseInitializer(bound.tree(), bound.definitions(), facts, entry.key)) {
        devirtualizedEraseInitializers.add(entry.key);
        continue;
      }
      if (entry.value.site == checker::checked::CoercionSite::AnnotatedInitializer &&
          isCallArgumentEraseInitializer(bound.tree(), bound.definitions(), entry.key)) {
        callArgumentEraseInitializers.add(entry.key);
        continue;
      }
      if (entry.value.site == checker::checked::CoercionSite::Return &&
          bound.tree().contains(entry.key) &&
          bound.tree().node(entry.key).kind == ast::SyntaxKind::StructLiteralExpr) {
        // Aggregate construction returns are admitted: the HIR builder
        // attaches a HirErasureRecord to the function declaration so the
        // MIR builder emits an Erase rvalue and the LIR builder constructs
        // the fat-pointer carrier. Other return erasure shapes (parameter
        // references, etc.) fall through to the capability rejection below
        // because the existential lowering carrier is not built for them.
        continue;
      }
      const auto owner =
          enclosingExecutableDefinition(bound.tree(), bound.definitions(), entry.key);
      return rejectHirCapability<HirModuleCandidate>(ZC_ASSERT_NONNULL(owner), registries,
                                                     ir::IrFailureKind::UnsupportedSourceConstruct,
                                                     entry.value.sourceSpan.clone());
    }
  }
  // Enum tuple-variant construction calls (`Result::Ok(41)`) produce no
  // coercion fact; their dead-erase seeds are identified directly from the
  // AST. A construction whose binding is never read is filtered before MIR;
  // a live construction lowers to a nominal aggregate.
  for (const auto node : enumConstructionInitializerNodes(bound.tree())) {
    if (isDeadErasedInitializer(bound.tree(), bound.definitions(), node)) {
      deadEraseInitializers.add(node);
    }
  }
  // Argument-position concrete-to-dyn erasures ride inside a direct call's
  // CheckedArgumentFact adjustment rather than the top-level coercion map.
  // A well-formed single DynErase argument adjustment is admitted: the
  // argument is recorded as an owner-local reference, and the MIR builder
  // emits the Erase rvalue to construct the existential carrier. Any other
  // adjustment shape stays an invariant rejection.
  for (const auto& callEntry : facts.calls().entries()) {
    for (const auto& argument : callEntry.value.invocation.arguments) {
      if (argument.adjustment == zc::none) { continue; }
      const auto& adjustment = ZC_ASSERT_NONNULL(argument.adjustment);
      if (!isSingleDynEraseAdjustment(adjustment)) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::AdditionalFact, module, registries,
                                             4);
      }
    }
  }
  // Error operators (`?!`, `!!`) are validated and verified by the checker.
  // The `?!` (error-propagate) operator is admitted: it is a codegen no-op
  // when the enclosing raises clause accepts the residual (the error-union
  // aggregate passes through unchanged), so the builder lowers the operand
  // call through the direct-call return carrier. The `!!` (forced-unwrap)
  // operator in a return position lowers through a multi-block CFG with a
  // trap block. The ErrorUnionShapeFact alone (a raising call with no
  // operator) is admitted: the union type is a closed scalar type that flows
  // through the existing lowering paths.
  for (const auto& entry : facts.errorOperators().entries()) {
    if (entry.value.kind == checker::checked::ErrorOperatorKind::Propagate) { continue; }
    if (entry.value.kind == checker::checked::ErrorOperatorKind::ForcedUnwrap) { continue; }
    const auto owner = enclosingExecutableDefinition(bound.tree(), bound.definitions(), entry.key);
    if (owner == zc::none) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries, 6);
    }
    return rejectHirCapability<HirModuleCandidate>(ZC_ASSERT_NONNULL(owner), registries,
                                                   ir::IrFailureKind::UnsupportedSourceConstruct,
                                                   entry.value.sourceSpan.clone());
  }

  const auto definitions = bound.definitions().definitions();
  if (definitions.size() > UINT32_MAX / 4) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::InvalidFact, module, registries, 1);
  }
  zc::Vector<PendingValueDeclaration> pending;
  zc::Vector<PendingFunctionDeclaration> pendingFunctions;
  const auto& ownInterface = checkedModule.ownModuleInterface();
  const auto& signatures = ownInterface.signatures();
  const auto& localSignatures = checkedModule.localSignatureFacts().signatures();
  // Impl method signatures are published in VerifiedSignatureFacts but are not
  // part of the module interface's AuthorizedSignatureBundle.definitions. Look
  // up both sources so impl method bodies can be lowered.
  const auto lookupSignature =
      [&](identity::DefId defId) -> const checker::signature::SemanticSignature* {
    for (const auto& sig : signatures.definitions) {
      if (sig.definition == defId) return &sig;
    }
    for (const auto& sig : localSignatures) {
      if (sig.definition == defId) return &sig;
    }
    return nullptr;
  };
  const auto definitionInventory = binder::DefinitionInventory::collect(bound.tree());
  // A bodied inherent method is admitted through semantic HIR only when it is a
  // shared/mutable-receiver method whose body is the single flat scalar-literal
  // return; that body never observes `this`. Every other method body shape
  // (`this` reads, parameters, calls, locals, control flow, move/static
  // receivers) stays a per-definition capability rejection (ZOM4099). Receiver
  // calls from a caller keep their more specific drains, so only bodies the
  // checker accepts reach this rail.
  const auto methodReceiverIsBorrowed = [&](identity::DefId method) -> bool {
    const auto* signature = lookupSignature(method);
    if (signature == nullptr) return false;
    if (!signature->payload.variant().is<checker::signature::CallableSignature>()) return false;
    const auto& callable =
        signature->payload.variant().get<checker::signature::CallableSignature>();
    if (callable.receiver == zc::none || callable.raises != zc::none || callable.abi != zc::none) {
      return false;
    }
    const auto mode = ZC_ASSERT_NONNULL(callable.receiver).mode;
    return mode == checker::signature::ReceiverMode::Shared ||
           mode == checker::signature::ReceiverMode::Mutable;
  };
  // An impl method's signature is published in the signature facts (checked by
  // the body checker's impl-method gate) but not in the module interface, so the
  // signature lookup above cannot find it. The body checker gate already
  // verified the receiver is borrowed, so the HIR builder admits impl methods
  // by owner kind instead.
  const auto isImplMethod = [](const binder::MaterializedDefinitionInventoryEntry& definition) {
    for (const auto& owner : definition.record.owners()) {
      if (owner.kind() == identity::EnclosingStableOwnerKind::Implementation) { return true; }
    }
    return false;
  };
  for (const auto& definition : definitions) {
    const bool isMethod = definition.record.kind() == identity::DefinitionKind::Method;
    const bool isDestructor = definition.record.kind() == identity::DefinitionKind::Destructor;
    if (!isMethod && !isDestructor) { continue; }
    if (!hasExecutableBody(definition, bound.definitions())) { continue; }
    const auto& tree = bound.tree();
    // A bodied method (inherent or impl-supplied) is admitted through semantic
    // HIR only when it is a shared/mutable-receiver method whose body is a
    // recognized return shape. The signature is published upstream so
    // coherence observes the complete impl; only the body is capability-gated
    // here. Every other method body shape stays a per-definition capability
    // rejection (ZOM4099). A destructor is admitted when its body is the
    // canonical empty shape (`deinit() { }`); it has no receiver and no
    // ordinary parameters, so the receiver-borrowed gate does not apply.
    const auto expectedNodeKind =
        isDestructor ? ast::SyntaxKind::DestructorDecl : ast::SyntaxKind::MethodDecl;
    const bool admitted = tree.contains(definition.node) &&
                          tree.node(definition.node).kind == expectedNodeKind &&
                          definition.site.value().is<binder::DeclarationDefinitionSite>() &&
                          functionReturnShape(tree, tree.node(definition.node)) != zc::none &&
                          (isDestructor || methodReceiverIsBorrowed(definition.definition) ||
                           isImplMethod(definition));
    if (!admitted) {
      return rejectHirCapability<HirModuleCandidate>(definition.definition, registries,
                                                     ir::IrFailureKind::UnsupportedSourceConstruct,
                                                     definition.source.clone());
    }
  }
  for (size_t definitionIndex = 0; definitionIndex < definitions.size(); ++definitionIndex) {
    const auto ordinal = static_cast<uint32_t>(definitionIndex);
    const auto& definition = definitions[ordinal];
    if (!hasExecutableBody(definition, bound.definitions())) { continue; }
    if (definition.record.kind() != identity::DefinitionKind::Function &&
        definition.record.kind() != identity::DefinitionKind::Method &&
        definition.record.kind() != identity::DefinitionKind::Static &&
        definition.record.kind() != identity::DefinitionKind::Constant &&
        definition.record.kind() != identity::DefinitionKind::Destructor) {
      continue;
    }
    if (definition.record.kind() == identity::DefinitionKind::Destructor) {
      // Destructor body shapes: `deinit() { }` (zero statements) or
      // `deinit() { let x: T = <scalar-literal>; }` (one let statement). The
      // callable result is Unit; there is no receiver, no ordinary parameters,
      // no return statement, and no return value. The function exists in the
      // binary for future drop elaboration but is not called until the
      // discharge inventory gains a backend consumer.
      const auto& tree = bound.tree();
      if (!tree.contains(definition.node) ||
          tree.node(definition.node).kind != ast::SyntaxKind::DestructorDecl ||
          !definition.site.value().is<binder::DeclarationDefinitionSite>()) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      auto bodyShape = functionReturnShape(tree, tree.node(definition.node));
      if (bodyShape == zc::none || (!ZC_ASSERT_NONNULL(bodyShape).isEmptyBody &&
                                    !ZC_ASSERT_NONNULL(bodyShape).isVoidLetBody &&
                                    !ZC_ASSERT_NONNULL(bodyShape).isDestructorFieldWriteBody &&
                                    !ZC_ASSERT_NONNULL(bodyShape).isDestructorMethodCallBody)) {
        return rejectHirCapability<HirModuleCandidate>(
            definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
            definition.source.clone());
      }
      const auto* signaturePtr = lookupSignature(definition.definition);
      if (signaturePtr == nullptr) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      if (!signaturePtr->payload.variant().is<checker::signature::CallableSignature>() ||
          (!signaturePtr->scope.variant().is<checker::signature::EnclosedSignatureScope>() &&
           !signaturePtr->scope.variant().is<checker::signature::MemberSignatureScope>() &&
           !signaturePtr->scope.variant()
                .is<checker::signature::ModuleDefinitionSignatureScope>())) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      const auto& callable =
          signaturePtr->payload.variant().get<checker::signature::CallableSignature>();
      // A raises clause on a destructor is well-formed source the current
      // lowering does not emit; drain the owning definition with the
      // capability code rather than an invariant.
      if (callable.raises != zc::none) {
        return rejectHirCapability<HirModuleCandidate>(
            definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
            definition.source.clone());
      }
      if (signaturePtr->definitionKind != identity::DefinitionKind::Destructor ||
          callable.receiver != zc::none || callable.parameters.size() != 0 ||
          !sameSpan(signaturePtr->declarationSpan, definition.source)) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      if (!typeExists(callable.success, checkedModule.semanticTypes())) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      // A destructor is an enclosed member of its nominal owner; it has no
      // module-scope signature root, so the visibility is the enclosed-member
      // default (external) rather than a root visibility.
      HirVisibility visibilityValue = HirVisibility::external();
      auto functionLinkage = linkage(callable);
      if (functionLinkage == zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      const ast::NodeId bodyNode(
          tree.node(definition.node).payload.words[ast::kDestructorDeclBodyWord]);
      auto bodySpan = bound.parsedModule().spanFor(tree.node(bodyNode).range);
      if (bodySpan == zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      HirLinkage linkageValue = HirLinkage::Internal;
      identity::SourceSpan bodySpanValue = definition.source.clone();
      ZC_IF_SOME(value, functionLinkage) { linkageValue = value; }
      ZC_IF_SOME(value, bodySpan) { bodySpanValue = value.clone(); }
      zc::Array<uint8_t> orderingKey = definition.key.encode();
      FunctionReturnShape shape = zc::mv(bodyShape).orDefault(FunctionReturnShape{});
      // For the void-let-body shape, resolve each binding's pattern type,
      // spans, and literal fact so the materialization pass can lower the
      // bindings.
      zc::Vector<PendingVoidLetBinding> voidLetBindings;
      if (shape.isVoidLetBody) {
        for (const auto& binding : shape.voidLetBindings) {
          auto patternSpan = bound.parsedModule().spanFor(tree.node(binding.pattern).range);
          auto initializerSpan = bound.parsedModule().spanFor(tree.node(binding.initializer).range);
          auto literalIndex = factIndex(facts.literals(), binding.initializer);
          if (patternSpan == zc::none || initializerSpan == zc::none || literalIndex == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t literalSlot = 0;
          ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
          const auto& literalFact = facts.literals().entries()[literalSlot].value;
          if (!sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          // The body checker produces no NodeType fact for an owner-local
          // IdentifierPattern; the literal fact's type is the binding's type
          // because the checker verified the declared/initializer match.
          const identity::SemanticTypeId patternType = literalFact.type;
          voidLetBindings.add(PendingVoidLetBinding{
              HirLocalBinding{HirNodeId(), HirLocalId(), patternType, HirNodeId(),
                              ZC_ASSERT_NONNULL(patternSpan).clone(),
                              ZC_ASSERT_NONNULL(initializerSpan).clone()},
              literalFact.literal.clone()});
        }
      }
      PendingFunctionShape destructorShape;
      if (shape.isDestructorFieldWriteBody) {
        destructorShape = DestructorFieldWriteBody{};
      } else if (shape.isDestructorMethodCallBody) {
        destructorShape = DestructorMethodCallBody{};
      } else if (voidLetBindings.size() != 0) {
        destructorShape = VoidLetBody{zc::mv(voidLetBindings)};
      } else {
        destructorShape = EmptyVoidBody{};
      }
      pendingFunctions.add(PendingFunctionDeclaration{definition.definition,
                                                      definition.node,
                                                      callable.success,
                                                      {},
                                                      zc::none,
                                                      zc::mv(visibilityValue),
                                                      linkageValue,
                                                      definition.source.clone(),
                                                      bodySpanValue.clone(),
                                                      definition.source.clone(),
                                                      definition.source.clone(),
                                                      zc::mv(orderingKey),
                                                      shape.body,
                                                      ast::NodeId(),
                                                      ast::NodeId(),
                                                      zc::none,
                                                      zc::mv(destructorShape)});
      continue;
    }
    if (definition.record.kind() == identity::DefinitionKind::Function ||
        definition.record.kind() == identity::DefinitionKind::Method) {
      const bool isMethod = definition.record.kind() == identity::DefinitionKind::Method;
      const auto& tree = bound.tree();
      const auto expectedNodeKind =
          isMethod ? ast::SyntaxKind::MethodDecl : ast::SyntaxKind::FunctionDecl;
      if (!tree.contains(definition.node) || tree.node(definition.node).kind != expectedNodeKind ||
          !definition.site.value().is<binder::DeclarationDefinitionSite>()) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      auto bodyShape = functionReturnShape(tree, tree.node(definition.node));
      auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), definition.definition);
      // An inherent member has no module-scope signature root (its root
      // authorization belongs to the enclosing nominal), so only an ordinary
      // function requires one.
      // A body the checker accepted but no lowering shape recognizes is an
      // admission/lowering drift: drain it as a user capability rejection
      // (ZOM4099) instead of an internal missing-fact incident. A missing
      // signature or root remains an internal invariant.
      if (bodyShape == zc::none) {
        return rejectHirCapability<HirModuleCandidate>(
            definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
            definition.source.clone());
      }
      const auto* signaturePtr = lookupSignature(definition.definition);
      if (signaturePtr == nullptr || (!isMethod && rootPosition == zc::none)) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      size_t rootSlot = 0;
      FunctionReturnShape shape = zc::mv(bodyShape).orDefault(FunctionReturnShape{});
      ZC_IF_SOME(index, rootPosition) { rootSlot = index; }
      if (shape.isConditional) {
        // Conditional shape: if/else where both branches return either a scalar
        // literal or a bare parameter reference. Arms may differ in kind.
        auto conditionTypeIndex = factIndex(facts.nodeTypes(), shape.condition);
        auto thenTypeIndex = factIndex(facts.nodeTypes(), shape.thenReturnValue);
        auto elseTypeIndex = factIndex(facts.nodeTypes(), shape.elseReturnValue);
        auto bodySpan = bound.parsedModule().spanFor(tree.node(shape.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(shape.returnStatement).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(shape.value).range);
        auto conditionSpan = bound.parsedModule().spanFor(tree.node(shape.condition).range);
        auto thenSpan = bound.parsedModule().spanFor(tree.node(shape.thenReturnValue).range);
        auto elseSpan = bound.parsedModule().spanFor(tree.node(shape.elseReturnValue).range);
        if (conditionTypeIndex == zc::none || thenTypeIndex == zc::none ||
            elseTypeIndex == zc::none || bodySpan == zc::none || returnSpan == zc::none ||
            valueSpan == zc::none || conditionSpan == zc::none || thenSpan == zc::none ||
            elseSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const auto& signature = *signaturePtr;
        checker::signature::MemberSignatureScope conditionalMemberScopeValue;
        zc::Maybe<checker::signature::MemberSignatureScope> conditionalMemberScope;
        if (isMethod) {
          if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
              !signature.scope.variant().is<checker::signature::MemberSignatureScope>()) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          conditionalMemberScope =
              signature.scope.variant().get<checker::signature::MemberSignatureScope>();
          conditionalMemberScopeValue = ZC_ASSERT_NONNULL(conditionalMemberScope);
        } else if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
                   !signature.scope.variant()
                        .is<checker::signature::ModuleDefinitionSignatureScope>()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        zc::Maybe<HirVisibility> functionVisibility;
        if (isMethod) {
          functionVisibility = memberVisibility(conditionalMemberScopeValue.visibility, module);
        } else {
          const auto& root = signatures.roots[rootSlot];
          functionVisibility = visibility(root.visibility);
        }
        auto functionLinkage = linkage(callable);
        const bool conditionalModuleMembershipValid =
            isMethod ? definitionBelongsToModule(definition, bound.definitions())
                     : (signatures.roots[rootSlot].sourceModule == module &&
                        signatures.roots[rootSlot].canonicalDefinition == definition.definition);
        if (functionVisibility == zc::none || functionLinkage == zc::none ||
            signature.definitionKind != (isMethod ? identity::DefinitionKind::Method
                                                  : identity::DefinitionKind::Function) ||
            !conditionalModuleMembershipValid || callable.raises != zc::none ||
            (!isMethod && callable.receiver != zc::none) ||
            (isMethod && callable.receiver == zc::none) ||
            !sameSpan(signature.declarationSpan, definition.source)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const ast::NodeId parameterListNode(
            tree.node(definition.node)
                .payload
                .words[isMethod ? ast::kMethodDeclParamsIdWord : ast::kFunctionDeclParamsIdWord]);
        if (!tree.contains(parameterListNode) ||
            tree.node(parameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& parameterList = tree.node(parameterListNode);
        const ast::NodeList parameterNodes{
            parameterList.payload.words[ast::kFunctionParameterListParamsFirstWord],
            parameterList.payload.words[ast::kFunctionParameterListParamsSizeWord]};
        // A method's AST parameter list leads with the implicit `this`
        // receiver, which is carried separately and never enters `parameters`.
        if (!tree.contains(parameterNodes) || parameterNodes.size < callable.parameters.size()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const size_t conditionalReceiverCount = parameterNodes.size - callable.parameters.size();
        if ((!isMethod && conditionalReceiverCount != 0) ||
            (isMethod && conditionalReceiverCount > 1)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        zc::Vector<HirParameter> parameters(callable.parameters.size());
        for (size_t index = 0; index < callable.parameters.size(); ++index) {
          const auto parameterNode = tree.list(parameterNodes)[conditionalReceiverCount + index];
          const auto& parameter = callable.parameters[index];
          if (!tree.contains(parameterNode) ||
              tree.node(parameterNode).kind != ast::SyntaxKind::FunctionParameterDecl ||
              parameter.hasDefault || !typeExists(parameter.type, checkedModule.semanticTypes())) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto parameterSpan = bound.parsedModule().spanFor(tree.node(parameterNode).range);
          if (parameterSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(span, parameterSpan) {
            parameters.add(HirParameter{parameter.parameter.clone(), parameter.type, span.clone()});
          }
        }
        zc::Maybe<HirParameter> conditionalReceiver;
        if (isMethod) {
          ZC_IF_SOME(receiver, callable.receiver) {
            if (conditionalMemberScope == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto owner = ZC_ASSERT_NONNULL(conditionalMemberScope).owner;
            if (owner == definition.definition) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            auto built = buildMethodReceiverParameter(definition, owner, receiver, bound,
                                                      registries, semanticTypes);
            if (built == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            conditionalReceiver = zc::mv(built);
          }
          if (conditionalReceiver == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
        }
        if (!typeExists(callable.success, checkedModule.semanticTypes()) ||
            (conditionalReceiver != zc::none &&
             !typeExists(ZC_ASSERT_NONNULL(conditionalReceiver).type,
                         checkedModule.semanticTypes()))) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        if (isExistentialSemanticType(checkedModule.semanticTypes(), callable.success)) {
          return rejectHirCapability<HirModuleCandidate>(
              definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
              definition.source.clone());
        }
        size_t thenTypeSlot = 0;
        size_t elseTypeSlot = 0;
        ZC_IF_SOME(index, thenTypeIndex) { thenTypeSlot = index; }
        ZC_IF_SOME(index, elseTypeIndex) { elseTypeSlot = index; }
        const auto thenType = facts.nodeTypes().entries()[thenTypeSlot].value;
        const auto elseType = facts.nodeTypes().entries()[elseTypeSlot].value;
        if (thenType != callable.success || elseType != callable.success) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        // Resolve the condition: either a bare bool parameter reference or an
        // `a == b` equality comparison of two same-typed scalar parameters. The
        // equality form consumes the checker Eq call fact.
        HirVisibility visibilityValue = HirVisibility::external();
        HirLinkage linkageValue = HirLinkage::Internal;
        ZC_IF_SOME(value, functionVisibility) { visibilityValue = zc::mv(value); }
        ZC_IF_SOME(value, functionLinkage) { linkageValue = value; }
        identity::SourceSpan bodySpanValue = definition.source.clone();
        identity::SourceSpan returnSpanValue = definition.source.clone();
        identity::SourceSpan valueSpanValue = definition.source.clone();
        ZC_IF_SOME(value, bodySpan) { bodySpanValue = value.clone(); }
        ZC_IF_SOME(value, returnSpan) { returnSpanValue = value.clone(); }
        ZC_IF_SOME(value, valueSpan) { valueSpanValue = value.clone(); }
        zc::Array<uint8_t> orderingKey = definition.key.encode();
        size_t conditionTypeSlot = 0;
        ZC_IF_SOME(index, conditionTypeIndex) { conditionTypeSlot = index; }
        const auto conditionType = facts.nodeTypes().entries()[conditionTypeSlot].value;
        if (shape.isLeadingLocalConditional) {
          // K leading scalar-local bindings followed by one comparison
          // conditional with two literal arms. The bindings reuse the
          // sequential-local binding classification; each comparison operand
          // resolves to a leading local, a parameter, or a scalar literal.
          // A sole-if whose comparison operand is a nested arithmetic
          // expression has no source let bindings; the builder synthesizes
          // one arithmetic temp binding instead.
          auto leadingShapeMaybe = leadingLocalConditionalShape(tree, shape.body);
          const bool synthesizedArithmetic =
              shape.conditionLeftIsNestedArithmetic || shape.conditionRightIsNestedArithmetic;
          if (leadingShapeMaybe == zc::none && !synthesizedArithmetic) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          LeadingLocalConditionalShape leadingShape{};
          ZC_IF_SOME(value, leadingShapeMaybe) { leadingShape = zc::mv(value); }
          if (isMethod || conditionalReceiver != zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          zc::Vector<PendingLeadingLocalBinding> pendingBindings;
          zc::Vector<binder::OwnerLocalBindingId> localBindingIds;
          bool leadingRejected = false;
          if (synthesizedArithmetic && leadingShapeMaybe == zc::none) {
            // Synthesize one arithmetic temp binding from the nested
            // arithmetic comparison operand. The arithmetic result is
            // computed into the temp, then the comparison reads it.
            const ast::NodeId arithNode =
                shape.conditionLeftIsNestedArithmetic ? shape.conditionLeft : shape.conditionRight;
            auto arithTypeIndex = factIndex(facts.nodeTypes(), arithNode);
            auto arithCallIndex = factIndex(facts.calls(), arithNode);
            auto arithSpan = bound.parsedModule().spanFor(tree.node(arithNode).range);
            if (arithTypeIndex == zc::none || arithCallIndex == zc::none || arithSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t arithTypeSlot = 0;
            size_t arithCallSlot = 0;
            ZC_IF_SOME(index, arithTypeIndex) { arithTypeSlot = index; }
            ZC_IF_SOME(index, arithCallIndex) { arithCallSlot = index; }
            const auto arithType = facts.nodeTypes().entries()[arithTypeSlot].value;
            const auto& arithCallFact = facts.calls().entries()[arithCallSlot].value;
            const auto& arithCall = arithCallFact.invocation;
            const auto& arithSelected = arithCall.selected.variant();
            if (arithCallFact.node != arithNode ||
                !arithSelected.is<checker::checked::PrimitiveCallable>() ||
                !isScalarArithmeticOperation(
                    arithSelected.get<checker::checked::PrimitiveCallable>().operation) ||
                arithCall.calleeType != arithType || arithCall.receiver != zc::none ||
                arithCall.receiverMode != zc::none || arithCall.receiverAdjustment != zc::none ||
                arithCall.arguments.size() != 2 ||
                arithCall.arguments[0].sourceNode != shape.nestedArithmeticLeft ||
                arithCall.arguments[0].sourceType != arithType ||
                arithCall.arguments[1].sourceNode != shape.nestedArithmeticRight ||
                arithCall.arguments[1].sourceType != arithType ||
                arithCall.successType != arithType || arithCall.resultType != arithType ||
                arithCall.substitutions != zc::none || arithCall.witnesses != zc::none ||
                arithCall.raises != zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto arithOperation =
                arithSelected.get<checker::checked::PrimitiveCallable>().operation;
            // Resolve one arithmetic leaf operand to a parameter or a literal.
            auto resolveArithmeticLeaf = [&](ast::NodeId leafNode, bool isLiteral,
                                             const identity::SourceSpan& leafSpan)
                -> zc::Maybe<PendingLeadingConditionOperand> {
              if (isLiteral) {
                auto leafLiteralIndex = factIndex(facts.literals(), leafNode);
                if (leafLiteralIndex == zc::none) return zc::none;
                size_t leafLiteralSlot = 0;
                ZC_IF_SOME(index, leafLiteralIndex) { leafLiteralSlot = index; }
                const auto& leafLiteralFact = facts.literals().entries()[leafLiteralSlot].value;
                if (leafLiteralFact.type != arithType) return zc::none;
                return PendingLeadingConditionOperand{
                    arithType, leafSpan.clone(), leafLiteralFact.literal.clone(), zc::none, 0,
                    false,     leafNode};
              }
              auto parameterHandle = resolvedCallableParameter(bound.bindings(), leafNode);
              if (parameterHandle == zc::none) return zc::none;
              zc::Maybe<identity::CallableParameterKey> resolvedKey;
              ZC_IF_SOME(handle, parameterHandle) {
                auto authority = registries.callableParameter(handle);
                ZC_IF_SOME(entry, authority) {
                  for (const auto& parameter : parameters) {
                    if (parameter.key == entry.key() && parameter.type == arithType) {
                      resolvedKey = entry.key().clone();
                    }
                  }
                }
              }
              if (resolvedKey == zc::none) return zc::none;
              return PendingLeadingConditionOperand{
                  arithType, leafSpan.clone(), zc::none, zc::mv(resolvedKey), 0, false, leafNode};
            };
            auto arithLeftSpan =
                bound.parsedModule().spanFor(tree.node(shape.nestedArithmeticLeft).range);
            auto arithRightSpan =
                bound.parsedModule().spanFor(tree.node(shape.nestedArithmeticRight).range);
            if (arithLeftSpan == zc::none || arithRightSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            auto arithLeft = resolveArithmeticLeaf(shape.nestedArithmeticLeft,
                                                   shape.nestedArithmeticLeftIsLiteral,
                                                   ZC_ASSERT_NONNULL(arithLeftSpan));
            auto arithRight = resolveArithmeticLeaf(shape.nestedArithmeticRight,
                                                    shape.nestedArithmeticRightIsLiteral,
                                                    ZC_ASSERT_NONNULL(arithRightSpan));
            if (arithLeft == zc::none || arithRight == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            pendingBindings.add(PendingLeadingLocalBinding{
                arithType, ZC_ASSERT_NONNULL(arithSpan).clone(),
                ZC_ASSERT_NONNULL(arithSpan).clone(), SequentialInitializerKind::PrimitiveBinary,
                zc::none, zc::none, 0, arithOperation, zc::mv(arithLeft), zc::mv(arithRight),
                ast::NodeId{}, arithNode});
          }
          if (leadingShapeMaybe != zc::none) {
            for (size_t bindingIndex = 0; bindingIndex < leadingShape.bindings.size();
                 ++bindingIndex) {
              const auto& binding = leadingShape.bindings[bindingIndex];
              auto ownerBinding =
                  ownerLocalBindingForPattern(bound.definitions(), binding.pattern, tree);
              auto patternSpan = bound.parsedModule().spanFor(tree.node(binding.pattern).range);
              auto initializerSpan =
                  bound.parsedModule().spanFor(tree.node(binding.initializer).range);
              auto initializerTypeIndex = factIndex(facts.nodeTypes(), binding.initializer);
              if (ownerBinding == zc::none || patternSpan == zc::none ||
                  initializerSpan == zc::none || initializerTypeIndex == zc::none ||
                  !ownerLocalMatches(bound.definitions(), ZC_ASSERT_NONNULL(ownerBinding),
                                     binding.pattern, tree)) {
                break;
              }
              size_t initializerTypeSlot = 0;
              ZC_IF_SOME(index, initializerTypeIndex) { initializerTypeSlot = index; }
              const identity::SemanticTypeId bindingType =
                  facts.nodeTypes().entries()[initializerTypeSlot].value;
              if (!typeExists(bindingType, checkedModule.semanticTypes())) { break; }
              for (const auto existing : localBindingIds) {
                if (existing == ZC_ASSERT_NONNULL(ownerBinding)) leadingRejected = true;
              }
              if (leadingRejected) break;
              localBindingIds.add(ZC_ASSERT_NONNULL(ownerBinding));
              zc::Maybe<checker::checked::CanonicalConstValue> bindingLiteral;
              zc::Maybe<identity::CallableParameterKey> bindingParameter;
              size_t referencedLocal = 0;
              zc::Maybe<checker::PrimitiveOperation> bindingArithmeticOperation;
              zc::Maybe<PendingLeadingConditionOperand> bindingArithmeticLeft;
              zc::Maybe<PendingLeadingConditionOperand> bindingArithmeticRight;
              if (binding.initializerKind == SequentialInitializerKind::Literal) {
                auto literalIndex = factIndex(facts.literals(), binding.initializer);
                if (literalIndex == zc::none) { break; }
                size_t literalSlot = 0;
                ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
                const auto& literalFact = facts.literals().entries()[literalSlot].value;
                if (literalFact.type != bindingType ||
                    !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
                  break;
                }
                bindingLiteral = literalFact.literal.clone();
              } else if (binding.initializerKind == SequentialInitializerKind::EnumVariant ||
                         binding.initializerKind == SequentialInitializerKind::FoldedStringLength ||
                         binding.initializerKind == SequentialInitializerKind::FoldedStringConcat) {
                // A qualified enum variant access lowers to an integer constant
                // (the variant discriminant), a string-length fold lowers to the
                // string's byte length, and a string-concat fold lowers to the
                // concatenated string constant. The body-checker emits a literal
                // fact with the folded value.
                auto literalIndex = factIndex(facts.literals(), binding.initializer);
                if (literalIndex == zc::none) { break; }
                size_t literalSlot = 0;
                ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
                const auto& literalFact = facts.literals().entries()[literalSlot].value;
                if (literalFact.type != bindingType ||
                    !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
                  break;
                }
                bindingLiteral = literalFact.literal.clone();
              } else if (binding.initializerKind == SequentialInitializerKind::LocalReference) {
                auto referenceBinding = resolvedOwnerLocal(bound.bindings(), binding.initializer);
                if (referenceBinding == zc::none ||
                    binding.referencedLocal >= localBindingIds.size() - 1 ||
                    ZC_ASSERT_NONNULL(referenceBinding) !=
                        localBindingIds[binding.referencedLocal]) {
                  break;
                }
                referencedLocal = binding.referencedLocal;
              } else if (binding.initializerKind == SequentialInitializerKind::PrimitiveBinary) {
                // A primitive binary initializer: validate its checked call fact
                // and resolve each leaf operand to a literal, a parameter, or an
                // earlier local.
                auto callIndex = factIndex(facts.calls(), binding.initializer);
                if (callIndex == zc::none || binding.leftOperand == zc::none ||
                    binding.rightOperand == zc::none) {
                  break;
                }
                size_t callSlot = 0;
                ZC_IF_SOME(index, callIndex) { callSlot = index; }
                const auto& callFact = facts.calls().entries()[callSlot].value;
                const auto& call = callFact.invocation;
                const auto& selected = call.selected.variant();
                if (!selected.is<checker::checked::PrimitiveCallable>()) { break; }
                const auto binaryOperation =
                    selected.get<checker::checked::PrimitiveCallable>().operation;
                const bool comparison = isScalarComparisonOperation(binaryOperation);
                const bool arithmetic = isScalarArithmeticOperation(binaryOperation);
                // The operand type is the shared argument type; a comparison
                // yields a bool result while an arithmetic or logical operator
                // yields its operand type.
                const auto binaryOperandType =
                    call.arguments.size() == 2 ? call.arguments[0].sourceType : bindingType;
                const bool operationSupported =
                    comparison || (arithmetic && bindingType == binaryOperandType);
                const ast::NodeId binaryLeft(
                    tree.node(binding.initializer).payload.words[ast::kBinaryExprLhsWord]);
                const ast::NodeId binaryRight(
                    tree.node(binding.initializer).payload.words[ast::kBinaryExprRhsWord]);
                if (!operationSupported || callFact.node != binding.initializer ||
                    call.calleeType != binaryOperandType || call.receiver != zc::none ||
                    call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
                    call.arguments.size() != 2 || call.arguments[0].sourceNode != binaryLeft ||
                    call.arguments[0].sourceType != binaryOperandType ||
                    call.arguments[1].sourceNode != binaryRight ||
                    call.arguments[1].sourceType != binaryOperandType ||
                    call.successType != bindingType || call.resultType != bindingType ||
                    call.substitutions != zc::none || call.witnesses != zc::none ||
                    call.raises != zc::none) {
                  break;
                }
                // Resolve each classified leaf operand to a materializable
                // condition operand.
                auto resolveLeaf = [&](const SequentialBinaryOperand& leaf)
                    -> zc::Maybe<PendingLeadingConditionOperand> {
                  auto leafSpan = bound.parsedModule().spanFor(tree.node(leaf.node).range);
                  if (leafSpan == zc::none) return zc::none;
                  if (leaf.kind == SequentialBinaryOperandKind::Literal) {
                    auto leafLiteral = factIndex(facts.literals(), leaf.node);
                    if (leafLiteral == zc::none) return zc::none;
                    size_t leafLiteralSlot = 0;
                    ZC_IF_SOME(index, leafLiteral) { leafLiteralSlot = index; }
                    const auto& literalFact = facts.literals().entries()[leafLiteralSlot].value;
                    if (literalFact.type != binaryOperandType) return zc::none;
                    return PendingLeadingConditionOperand{binaryOperandType,
                                                          ZC_ASSERT_NONNULL(leafSpan).clone(),
                                                          literalFact.literal.clone(),
                                                          zc::none,
                                                          0,
                                                          false,
                                                          leaf.node};
                  }
                  if (leaf.kind == SequentialBinaryOperandKind::LocalReference) {
                    if (leaf.referencedLocal >= localBindingIds.size()) return zc::none;
                    auto referenceBinding = resolvedOwnerLocal(bound.bindings(), leaf.node);
                    if (referenceBinding == zc::none || ZC_ASSERT_NONNULL(referenceBinding) !=
                                                            localBindingIds[leaf.referencedLocal]) {
                      return zc::none;
                    }
                    return PendingLeadingConditionOperand{binaryOperandType,
                                                          ZC_ASSERT_NONNULL(leafSpan).clone(),
                                                          zc::none,
                                                          zc::none,
                                                          leaf.referencedLocal,
                                                          true,
                                                          leaf.node};
                  }
                  auto parameterHandle = resolvedCallableParameter(bound.bindings(), leaf.node);
                  if (parameterHandle == zc::none) return zc::none;
                  zc::Maybe<identity::CallableParameterKey> resolvedKey;
                  ZC_IF_SOME(handle, parameterHandle) {
                    auto authority = registries.callableParameter(handle);
                    ZC_IF_SOME(entry, authority) {
                      for (const auto& parameter : parameters) {
                        if (parameter.key == entry.key() && parameter.type == binaryOperandType) {
                          resolvedKey = entry.key().clone();
                        }
                      }
                    }
                  }
                  if (resolvedKey == zc::none) return zc::none;
                  return PendingLeadingConditionOperand{binaryOperandType,
                                                        ZC_ASSERT_NONNULL(leafSpan).clone(),
                                                        zc::none,
                                                        zc::mv(resolvedKey),
                                                        0,
                                                        false,
                                                        leaf.node};
                };
                auto leftResolved = resolveLeaf(ZC_ASSERT_NONNULL(binding.leftOperand));
                auto rightResolved = resolveLeaf(ZC_ASSERT_NONNULL(binding.rightOperand));
                if (leftResolved == zc::none || rightResolved == zc::none) break;
                bindingArithmeticOperation = binaryOperation;
                bindingArithmeticLeft = zc::mv(leftResolved);
                bindingArithmeticRight = zc::mv(rightResolved);
              } else {
                auto parameterHandle =
                    resolvedCallableParameter(bound.bindings(), binding.initializer);
                if (parameterHandle == zc::none) { break; }
                zc::Maybe<identity::CallableParameterKey> resolvedKey;
                ZC_IF_SOME(handle, parameterHandle) {
                  auto authority = registries.callableParameter(handle);
                  ZC_IF_SOME(entry, authority) {
                    for (const auto& parameter : parameters) {
                      if (parameter.key == entry.key() && parameter.type == bindingType) {
                        resolvedKey = entry.key().clone();
                      }
                    }
                  }
                }
                if (resolvedKey == zc::none) { break; }
                bindingParameter = zc::mv(resolvedKey);
              }
              pendingBindings.add(PendingLeadingLocalBinding{
                  bindingType, ZC_ASSERT_NONNULL(patternSpan).clone(),
                  ZC_ASSERT_NONNULL(initializerSpan).clone(), binding.initializerKind,
                  zc::mv(bindingLiteral), zc::mv(bindingParameter), referencedLocal,
                  zc::mv(bindingArithmeticOperation), zc::mv(bindingArithmeticLeft),
                  zc::mv(bindingArithmeticRight), binding.declarator, binding.initializer});
            }
          }
          if (synthesizedArithmetic && leadingShapeMaybe != zc::none) {
            // Synthesize one arithmetic temp binding from the nested
            // arithmetic comparison operand. The arithmetic result is
            // computed into the temp, then the comparison reads it.
            // The leaf operands may reference leading locals, parameters,
            // or scalar literals.
            const ast::NodeId arithNode =
                shape.conditionLeftIsNestedArithmetic ? shape.conditionLeft : shape.conditionRight;
            auto arithTypeIndex = factIndex(facts.nodeTypes(), arithNode);
            auto arithCallIndex = factIndex(facts.calls(), arithNode);
            auto arithSpan = bound.parsedModule().spanFor(tree.node(arithNode).range);
            if (arithTypeIndex == zc::none || arithCallIndex == zc::none || arithSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t arithTypeSlot = 0;
            size_t arithCallSlot = 0;
            ZC_IF_SOME(index, arithTypeIndex) { arithTypeSlot = index; }
            ZC_IF_SOME(index, arithCallIndex) { arithCallSlot = index; }
            const auto arithType = facts.nodeTypes().entries()[arithTypeSlot].value;
            const auto& arithCallFact = facts.calls().entries()[arithCallSlot].value;
            const auto& arithCall = arithCallFact.invocation;
            const auto& arithSelected = arithCall.selected.variant();
            if (arithCallFact.node != arithNode ||
                !arithSelected.is<checker::checked::PrimitiveCallable>() ||
                !isScalarArithmeticOperation(
                    arithSelected.get<checker::checked::PrimitiveCallable>().operation) ||
                arithCall.calleeType != arithType || arithCall.receiver != zc::none ||
                arithCall.receiverMode != zc::none || arithCall.receiverAdjustment != zc::none ||
                arithCall.arguments.size() != 2 ||
                arithCall.arguments[0].sourceNode != shape.nestedArithmeticLeft ||
                arithCall.arguments[0].sourceType != arithType ||
                arithCall.arguments[1].sourceNode != shape.nestedArithmeticRight ||
                arithCall.arguments[1].sourceType != arithType ||
                arithCall.successType != arithType || arithCall.resultType != arithType ||
                arithCall.substitutions != zc::none || arithCall.witnesses != zc::none ||
                arithCall.raises != zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto arithOperation =
                arithSelected.get<checker::checked::PrimitiveCallable>().operation;
            auto resolveArithmeticLeaf = [&](ast::NodeId leafNode, bool isLiteral,
                                             const identity::SourceSpan& leafSpan)
                -> zc::Maybe<PendingLeadingConditionOperand> {
              if (isLiteral) {
                auto leafLiteralIndex = factIndex(facts.literals(), leafNode);
                if (leafLiteralIndex == zc::none) return zc::none;
                size_t leafLiteralSlot = 0;
                ZC_IF_SOME(index, leafLiteralIndex) { leafLiteralSlot = index; }
                const auto& leafLiteralFact = facts.literals().entries()[leafLiteralSlot].value;
                if (leafLiteralFact.type != arithType) return zc::none;
                return PendingLeadingConditionOperand{
                    arithType, leafSpan.clone(), leafLiteralFact.literal.clone(), zc::none, 0,
                    false,     leafNode};
              }
              auto ownerBinding = resolvedOwnerLocal(bound.bindings(), leafNode);
              if (ownerBinding != zc::none) {
                for (size_t bindingIndex = 0; bindingIndex < localBindingIds.size();
                     ++bindingIndex) {
                  if (ZC_ASSERT_NONNULL(ownerBinding) == localBindingIds[bindingIndex]) {
                    return PendingLeadingConditionOperand{arithType, leafSpan.clone(), zc::none,
                                                          zc::none,  bindingIndex,     true,
                                                          leafNode};
                  }
                }
              }
              auto parameterHandle = resolvedCallableParameter(bound.bindings(), leafNode);
              if (parameterHandle == zc::none) return zc::none;
              zc::Maybe<identity::CallableParameterKey> resolvedKey;
              ZC_IF_SOME(handle, parameterHandle) {
                auto authority = registries.callableParameter(handle);
                ZC_IF_SOME(entry, authority) {
                  for (const auto& parameter : parameters) {
                    if (parameter.key == entry.key() && parameter.type == arithType) {
                      resolvedKey = entry.key().clone();
                    }
                  }
                }
              }
              if (resolvedKey == zc::none) return zc::none;
              return PendingLeadingConditionOperand{
                  arithType, leafSpan.clone(), zc::none, zc::mv(resolvedKey), 0, false, leafNode};
            };
            auto arithLeftSpan =
                bound.parsedModule().spanFor(tree.node(shape.nestedArithmeticLeft).range);
            auto arithRightSpan =
                bound.parsedModule().spanFor(tree.node(shape.nestedArithmeticRight).range);
            if (arithLeftSpan == zc::none || arithRightSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            auto arithLeft = resolveArithmeticLeaf(shape.nestedArithmeticLeft,
                                                   shape.nestedArithmeticLeftIsLiteral,
                                                   ZC_ASSERT_NONNULL(arithLeftSpan));
            auto arithRight = resolveArithmeticLeaf(shape.nestedArithmeticRight,
                                                    shape.nestedArithmeticRightIsLiteral,
                                                    ZC_ASSERT_NONNULL(arithRightSpan));
            if (arithLeft == zc::none || arithRight == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            pendingBindings.add(PendingLeadingLocalBinding{
                arithType, ZC_ASSERT_NONNULL(arithSpan).clone(),
                ZC_ASSERT_NONNULL(arithSpan).clone(), SequentialInitializerKind::PrimitiveBinary,
                zc::none, zc::none, 0, arithOperation, zc::mv(arithLeft), zc::mv(arithRight),
                ast::NodeId{}, arithNode});
          }
          // Resolve one comparison operand to a leading local, a parameter, or
          // a scalar literal.
          auto resolveConditionOperand = [&](ast::NodeId operandNode, bool isLiteral,
                                             const identity::SourceSpan& operandSpan)
              -> zc::Maybe<PendingLeadingConditionOperand> {
            if (!isLiteral) {
              auto operandTypeIndex = factIndex(facts.nodeTypes(), operandNode);
              if (operandTypeIndex == zc::none) return zc::none;
              size_t operandTypeSlot = 0;
              ZC_IF_SOME(index, operandTypeIndex) { operandTypeSlot = index; }
              const auto resolvedType = facts.nodeTypes().entries()[operandTypeSlot].value;
              auto ownerBinding = resolvedOwnerLocal(bound.bindings(), operandNode);
              if (ownerBinding != zc::none) {
                for (size_t bindingIndex = 0; bindingIndex < localBindingIds.size();
                     ++bindingIndex) {
                  if (ZC_ASSERT_NONNULL(ownerBinding) == localBindingIds[bindingIndex]) {
                    return PendingLeadingConditionOperand{
                        resolvedType, operandSpan.clone(), zc::none, zc::none, bindingIndex,
                        true,         operandNode};
                  }
                }
              }
              auto parameterHandle = resolvedCallableParameter(bound.bindings(), operandNode);
              if (parameterHandle != zc::none) {
                identity::CallableParameterId handle;
                ZC_IF_SOME(value, parameterHandle) { handle = value; }
                auto authority = registries.callableParameter(handle);
                ZC_IF_SOME(entry, authority) {
                  for (const auto& parameter : parameters) {
                    if (parameter.key == entry.key()) {
                      return PendingLeadingConditionOperand{
                          parameter.type, operandSpan.clone(), zc::none, entry.key().clone(), 0,
                          false,          operandNode};
                    }
                  }
                }
              }
              (void)resolvedType;
              return zc::none;
            }
            auto literalIndex = factIndex(facts.literals(), operandNode);
            if (literalIndex == zc::none) return zc::none;
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            return PendingLeadingConditionOperand{literalFact.type,
                                                  operandSpan.clone(),
                                                  literalFact.literal.clone(),
                                                  zc::none,
                                                  0,
                                                  false,
                                                  operandNode};
          };
          // The operand types and comparison call fact are validated below once
          // both operands resolve; placeholder kept out-of-line by computing the
          // spans and node types first.
          zc::Maybe<PendingLeadingConditionOperand> leftOperand;
          zc::Maybe<PendingLeadingConditionOperand> rightOperand;
          identity::SemanticTypeId operandType{};
          checker::PrimitiveOperation operation{};
          if (shape.conditionIsUnary) {
            // The unary `!x` condition desugars to `x == false`. The operand is
            // a parameter reference or a scalar literal; the synthetic false
            // operand has no AST node and carries no checker-produced fact.
            auto operandTypeIndex = factIndex(facts.nodeTypes(), shape.conditionUnaryOperand);
            auto unaryCallIndex = factIndex(facts.calls(), shape.condition);
            auto operandSpan =
                bound.parsedModule().spanFor(tree.node(shape.conditionUnaryOperand).range);
            if (leadingRejected || operandTypeIndex == zc::none || unaryCallIndex == zc::none ||
                operandSpan == zc::none) {
              // A unary condition whose operand type or call fact is missing
              // (the body checker drained the construct, or a leading binding
              // could not be resolved) is legal source the lowering carrier
              // does not implement yet. Drain it as a per-definition capability
              // rejection (ZOM4099) rather than an internal missing-fact
              // incident.
              return rejectHirCapability<HirModuleCandidate>(
                  definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
                  definition.source.clone());
            }
            size_t operandTypeSlot = 0;
            size_t unaryCallSlot = 0;
            ZC_IF_SOME(index, operandTypeIndex) { operandTypeSlot = index; }
            ZC_IF_SOME(index, unaryCallIndex) { unaryCallSlot = index; }
            operandType = facts.nodeTypes().entries()[operandTypeSlot].value;
            const auto& unaryCallFact = facts.calls().entries()[unaryCallSlot].value;
            const auto& unaryCall = unaryCallFact.invocation;
            const auto& unarySelected = unaryCall.selected.variant();
            if (unaryCallFact.node != shape.condition ||
                !unarySelected.is<checker::checked::PrimitiveCallable>() ||
                unarySelected.get<checker::checked::PrimitiveCallable>().operation !=
                    checker::PrimitiveOperation::LogicalNot ||
                unaryCall.calleeType != operandType || unaryCall.receiver != zc::none ||
                unaryCall.receiverMode != zc::none || unaryCall.receiverAdjustment != zc::none ||
                unaryCall.arguments.size() != 1 ||
                unaryCall.arguments[0].sourceNode != shape.conditionUnaryOperand ||
                unaryCall.arguments[0].sourceType != operandType ||
                unaryCall.successType != conditionType || unaryCall.resultType != conditionType ||
                unaryCall.substitutions != zc::none || unaryCall.witnesses != zc::none ||
                unaryCall.raises != zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            leftOperand = resolveConditionOperand(shape.conditionUnaryOperand,
                                                  shape.conditionUnaryOperandIsLiteral,
                                                  ZC_ASSERT_NONNULL(operandSpan));
            if (leftOperand == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            // The synthetic false operand: `!x` desugars to `x == false`.
            auto syntheticValue = checker::checked::CanonicalConstValue::boolean(false);
            rightOperand = PendingLeadingConditionOperand{operandType,
                                                          ZC_ASSERT_NONNULL(conditionSpan).clone(),
                                                          zc::mv(syntheticValue),
                                                          zc::none,
                                                          0,
                                                          false};
            operation = checker::PrimitiveOperation::Eq;
          } else if (shape.conditionIsBoolReference) {
            // A bare bool identifier condition desugars to `c == true`. The
            // operand is a leading local or parameter reference; the synthetic
            // true operand has no AST node and carries no checker-produced fact.
            auto operandTypeIndex = factIndex(facts.nodeTypes(), shape.condition);
            if (leadingRejected || operandTypeIndex == zc::none) {
              // A bool-reference condition whose type fact is missing (the body
              // checker drained the construct, or a leading binding could not
              // be resolved) is legal source the lowering carrier does not
              // implement yet. Drain it as a per-definition capability
              // rejection (ZOM4099) rather than an internal missing-fact
              // incident.
              return rejectHirCapability<HirModuleCandidate>(
                  definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
                  definition.source.clone());
            }
            size_t operandTypeSlot = 0;
            ZC_IF_SOME(index, operandTypeIndex) { operandTypeSlot = index; }
            operandType = facts.nodeTypes().entries()[operandTypeSlot].value;
            leftOperand =
                resolveConditionOperand(shape.condition, false, ZC_ASSERT_NONNULL(conditionSpan));
            if (leftOperand == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            // The synthetic true operand: `c` desugars to `c == true`.
            auto syntheticValue = checker::checked::CanonicalConstValue::boolean(true);
            rightOperand = PendingLeadingConditionOperand{operandType,
                                                          ZC_ASSERT_NONNULL(conditionSpan).clone(),
                                                          zc::mv(syntheticValue),
                                                          zc::none,
                                                          0,
                                                          false};
            operation = checker::PrimitiveOperation::Eq;
          } else {
            auto leftTypeIndex = factIndex(facts.nodeTypes(), shape.conditionLeft);
            auto rightTypeIndex = factIndex(facts.nodeTypes(), shape.conditionRight);
            auto callIndex = factIndex(facts.calls(), shape.condition);
            auto leftSpan = bound.parsedModule().spanFor(tree.node(shape.conditionLeft).range);
            auto rightSpan = bound.parsedModule().spanFor(tree.node(shape.conditionRight).range);
            if (leadingRejected || leftTypeIndex == zc::none || rightTypeIndex == zc::none ||
                callIndex == zc::none || leftSpan == zc::none || rightSpan == zc::none) {
              // A comparison condition whose operand type or call fact is
              // missing (the body checker drained the construct, or a leading
              // binding could not be resolved) is legal source the lowering
              // carrier does not implement yet. Drain it as a per-definition
              // capability rejection (ZOM4099) rather than an internal
              // missing-fact incident.
              return rejectHirCapability<HirModuleCandidate>(
                  definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
                  definition.source.clone());
            }
            size_t leftTypeSlot = 0;
            size_t rightTypeSlot = 0;
            size_t callSlot = 0;
            ZC_IF_SOME(index, leftTypeIndex) { leftTypeSlot = index; }
            ZC_IF_SOME(index, rightTypeIndex) { rightTypeSlot = index; }
            ZC_IF_SOME(index, callIndex) { callSlot = index; }
            operandType = facts.nodeTypes().entries()[leftTypeSlot].value;
            const auto rightType = facts.nodeTypes().entries()[rightTypeSlot].value;
            const auto& callFact = facts.calls().entries()[callSlot].value;
            const auto& call = callFact.invocation;
            const auto& selected = call.selected.variant();
            if (operandType != rightType || callFact.node != shape.condition ||
                !selected.is<checker::checked::PrimitiveCallable>() ||
                !(isScalarComparisonOperation(
                      selected.get<checker::checked::PrimitiveCallable>().operation) ||
                  selected.get<checker::checked::PrimitiveCallable>().operation ==
                      checker::PrimitiveOperation::LogicalAnd ||
                  selected.get<checker::checked::PrimitiveCallable>().operation ==
                      checker::PrimitiveOperation::LogicalOr) ||
                call.calleeType != operandType || call.receiver != zc::none ||
                call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
                call.arguments.size() != 2 || call.arguments[0].sourceNode != shape.conditionLeft ||
                call.arguments[0].sourceType != operandType ||
                call.arguments[1].sourceNode != shape.conditionRight ||
                call.arguments[1].sourceType != operandType || call.successType != conditionType ||
                call.resultType != conditionType || call.substitutions != zc::none ||
                call.witnesses != zc::none || call.raises != zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            // The nested arithmetic operand resolves to the synthesized
            // temp local (the last binding); the other operand resolves
            // normally.
            if (synthesizedArithmetic && shape.conditionLeftIsNestedArithmetic) {
              leftOperand = PendingLeadingConditionOperand{
                  operandType,        ZC_ASSERT_NONNULL(leftSpan).clone(), zc::none,
                  zc::none,           pendingBindings.size() - 1,          true,
                  shape.conditionLeft};
            } else {
              leftOperand = resolveConditionOperand(
                  shape.conditionLeft, shape.conditionLeftIsLiteral, ZC_ASSERT_NONNULL(leftSpan));
            }
            if (synthesizedArithmetic && shape.conditionRightIsNestedArithmetic) {
              rightOperand = PendingLeadingConditionOperand{operandType,
                                                            ZC_ASSERT_NONNULL(rightSpan).clone(),
                                                            zc::none,
                                                            zc::none,
                                                            pendingBindings.size() - 1,
                                                            true,
                                                            shape.conditionRight};
            } else {
              rightOperand =
                  resolveConditionOperand(shape.conditionRight, shape.conditionRightIsLiteral,
                                          ZC_ASSERT_NONNULL(rightSpan));
            }
            if (leftOperand == zc::none || rightOperand == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            // Every condition operand shares the comparison operand type.
            const auto& leftResolved = ZC_ASSERT_NONNULL(leftOperand);
            const auto& rightResolved = ZC_ASSERT_NONNULL(rightOperand);
            if (leftResolved.type != operandType || rightResolved.type != operandType) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            operation = selected.get<checker::checked::PrimitiveCallable>().operation;
          }
          // Build one conditional arm from its return-value node. A scalar-
          // literal arm carries the checked literal fact; a bare-identifier
          // arm resolves to a leading local reference or a function parameter
          // reference; a binary arm resolves the checked call fact and lowers
          // its two leaf operands into a primitive binary operation.
          bool armRejected = false;
          auto buildLeadingArm =
              [&](ast::NodeId armNode, identity::SemanticTypeId armType,
                  const identity::SourceSpan& armSpan) -> zc::Maybe<PendingConditionalArm> {
            if (tree.node(armNode).kind == ast::SyntaxKind::IdentExpr) {
              auto ownerBinding = resolvedOwnerLocal(bound.bindings(), armNode);
              if (ownerBinding != zc::none) {
                for (size_t bindingIndex = 0; bindingIndex < localBindingIds.size();
                     ++bindingIndex) {
                  if (ZC_ASSERT_NONNULL(ownerBinding) == localBindingIds[bindingIndex]) {
                    auto reference = HirLocalReferenceExpression{
                        HirNodeId(), hirLocalId(static_cast<uint32_t>(bindingIndex + 1)), armType,
                        HirValueCategory::Place, armSpan.clone()};
                    return PendingConditionalArm{zc::none, zc::none,        zc::mv(reference),
                                                 armType,  armSpan.clone(), zc::none,
                                                 armNode};
                  }
                }
              }
              auto parameter = resolvedCallableParameter(bound.bindings(), armNode);
              if (parameter == zc::none) {
                armRejected = true;
                return zc::none;
              }
              identity::CallableParameterId handle;
              ZC_IF_SOME(value, parameter) { handle = value; }
              auto authority = registries.callableParameter(handle);
              if (authority == zc::none) {
                armRejected = true;
                return zc::none;
              }
              zc::Maybe<PendingConditionalArm> built;
              ZC_IF_SOME(entry, authority) {
                bool matches = false;
                for (const auto& parameterCandidate : parameters) {
                  if (parameterCandidate.key == entry.key() && parameterCandidate.type == armType) {
                    matches = true;
                  }
                }
                if (!matches) {
                  armRejected = true;
                } else {
                  auto reference =
                      HirParameterReferenceExpression{HirNodeId(), entry.key().clone(), armType,
                                                      HirValueCategory::Place, armSpan.clone()};
                  built =
                      PendingConditionalArm{zc::none,        zc::mv(reference), zc::none, armType,
                                            armSpan.clone(), zc::none,          armNode};
                }
              }
              return built;
            }
            if (tree.node(armNode).kind == ast::SyntaxKind::BinaryExpr) {
              auto armCallIndex = factIndex(facts.calls(), armNode);
              if (armCallIndex == zc::none) {
                armRejected = true;
                return zc::none;
              }
              size_t armCallSlot = 0;
              ZC_IF_SOME(index, armCallIndex) { armCallSlot = index; }
              const auto& armCallFact = facts.calls().entries()[armCallSlot].value;
              const auto& armCall = armCallFact.invocation;
              const auto& armSelected = armCall.selected.variant();
              if (!armSelected.is<checker::checked::PrimitiveCallable>()) {
                armRejected = true;
                return zc::none;
              }
              const auto armOperation =
                  armSelected.get<checker::checked::PrimitiveCallable>().operation;
              if (!isScalarArithmeticOperation(armOperation) &&
                  !isScalarComparisonOperation(armOperation)) {
                armRejected = true;
                return zc::none;
              }
              if (armCall.arguments.size() != 2 || armCall.resultType != armType ||
                  armCall.successType != armType || armCall.receiver != zc::none ||
                  armCall.receiverMode != zc::none || armCall.receiverAdjustment != zc::none) {
                armRejected = true;
                return zc::none;
              }
              const auto armOperandType = armCall.arguments[0].sourceType;
              if (armCall.arguments[1].sourceType != armOperandType ||
                  armCall.calleeType != armOperandType) {
                armRejected = true;
                return zc::none;
              }
              auto buildLeaf = [&](ast::NodeId operandNode) -> zc::Maybe<PendingConditionalArm> {
                if (tree.node(operandNode).kind == ast::SyntaxKind::IdentExpr) {
                  auto leafOwnerBinding = resolvedOwnerLocal(bound.bindings(), operandNode);
                  if (leafOwnerBinding != zc::none) {
                    for (size_t bindingIndex = 0; bindingIndex < localBindingIds.size();
                         ++bindingIndex) {
                      if (ZC_ASSERT_NONNULL(leafOwnerBinding) == localBindingIds[bindingIndex]) {
                        auto leafSpan = bound.parsedModule().spanFor(tree.node(operandNode).range);
                        if (leafSpan == zc::none) {
                          armRejected = true;
                          return zc::none;
                        }
                        auto reference = HirLocalReferenceExpression{
                            HirNodeId(), hirLocalId(static_cast<uint32_t>(bindingIndex + 1)),
                            armOperandType, HirValueCategory::Place,
                            ZC_ASSERT_NONNULL(leafSpan).clone()};
                        return PendingConditionalArm{zc::none,
                                                     zc::none,
                                                     zc::mv(reference),
                                                     armOperandType,
                                                     ZC_ASSERT_NONNULL(leafSpan).clone(),
                                                     zc::none,
                                                     operandNode};
                      }
                    }
                  }
                  auto leafParameter = resolvedCallableParameter(bound.bindings(), operandNode);
                  if (leafParameter == zc::none) {
                    armRejected = true;
                    return zc::none;
                  }
                  identity::CallableParameterId leafHandle;
                  ZC_IF_SOME(value, leafParameter) { leafHandle = value; }
                  auto leafAuthority = registries.callableParameter(leafHandle);
                  if (leafAuthority == zc::none) {
                    armRejected = true;
                    return zc::none;
                  }
                  zc::Maybe<PendingConditionalArm> leafBuilt;
                  ZC_IF_SOME(entry, leafAuthority) {
                    bool leafMatches = false;
                    for (const auto& parameterCandidate : parameters) {
                      if (parameterCandidate.key == entry.key() &&
                          parameterCandidate.type == armOperandType) {
                        leafMatches = true;
                      }
                    }
                    auto leafSpan = bound.parsedModule().spanFor(tree.node(operandNode).range);
                    if (!leafMatches || leafSpan == zc::none) {
                      armRejected = true;
                    } else {
                      auto reference = HirParameterReferenceExpression{
                          HirNodeId(), entry.key().clone(), armOperandType, HirValueCategory::Place,
                          ZC_ASSERT_NONNULL(leafSpan).clone()};
                      leafBuilt = PendingConditionalArm{zc::none,
                                                        zc::mv(reference),
                                                        zc::none,
                                                        armOperandType,
                                                        ZC_ASSERT_NONNULL(leafSpan).clone(),
                                                        zc::none,
                                                        operandNode};
                    }
                  }
                  return leafBuilt;
                }
                if (!isScalarLiteral(tree.node(operandNode).kind)) {
                  armRejected = true;
                  return zc::none;
                }
                auto leafLiteralIndex = factIndex(facts.literals(), operandNode);
                if (leafLiteralIndex == zc::none) {
                  armRejected = true;
                  return zc::none;
                }
                size_t leafLiteralSlot = 0;
                ZC_IF_SOME(index, leafLiteralIndex) { leafLiteralSlot = index; }
                const auto& leafLiteralFact = facts.literals().entries()[leafLiteralSlot].value;
                auto leafSpan = bound.parsedModule().spanFor(tree.node(operandNode).range);
                if (leafSpan == zc::none) {
                  armRejected = true;
                  return zc::none;
                }
                return PendingConditionalArm{
                    leafLiteralFact.literal.clone(),     zc::none, zc::none,   armOperandType,
                    ZC_ASSERT_NONNULL(leafSpan).clone(), zc::none, operandNode};
              };
              auto armLeft = buildLeaf(armCall.arguments[0].sourceNode);
              auto armRight = buildLeaf(armCall.arguments[1].sourceNode);
              if (armRejected || armLeft == zc::none || armRight == zc::none) { return zc::none; }
              auto armBinary = PendingConditionalArmBinary{
                  zc::heap<PendingConditionalArm>(zc::mv(ZC_ASSERT_NONNULL(armLeft))),
                  zc::heap<PendingConditionalArm>(zc::mv(ZC_ASSERT_NONNULL(armRight))),
                  armOperandType, armOperation};
              return PendingConditionalArm{zc::none,        zc::none,          zc::none, armType,
                                           armSpan.clone(), zc::mv(armBinary), armNode};
            }
            auto literalIndex = factIndex(facts.literals(), armNode);
            if (literalIndex == zc::none) {
              armRejected = true;
              return zc::none;
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            if (literalFact.type != armType) {
              armRejected = true;
              return zc::none;
            }
            return PendingConditionalArm{literalFact.literal.clone(),
                                         zc::none,
                                         zc::none,
                                         armType,
                                         armSpan.clone(),
                                         zc::none,
                                         armNode};
          };
          auto thenArm =
              buildLeadingArm(shape.thenReturnValue, callable.success, ZC_ASSERT_NONNULL(thenSpan));
          auto elseArm =
              buildLeadingArm(shape.elseReturnValue, callable.success, ZC_ASSERT_NONNULL(elseSpan));
          if (armRejected || thenArm == zc::none || elseArm == zc::none) {
            return rejectHirCapability<HirModuleCandidate>(
                definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
                definition.source.clone());
          }
          auto leadingPending = PendingLeadingLocalConditionalReturn{
              zc::mv(pendingBindings),
              zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
              zc::mv(ZC_ASSERT_NONNULL(rightOperand)),
              operandType,
              conditionType,
              operation,
              ZC_ASSERT_NONNULL(conditionSpan).clone(),
              zc::mv(ZC_ASSERT_NONNULL(thenArm)),
              zc::mv(ZC_ASSERT_NONNULL(elseArm)),
              callable.success,
              ZC_ASSERT_NONNULL(thenSpan).clone(),
              ZC_ASSERT_NONNULL(elseSpan).clone(),
              returnSpanValue.clone(),
              shape.conditionIsUnary || shape.conditionIsBoolReference,
              shape.conditionIsBoolReference,
              leadingShape.ifStatement};
          pendingFunctions.add(PendingFunctionDeclaration{
              definition.definition, definition.node, callable.success, zc::mv(parameters),
              zc::none, zc::mv(visibilityValue), linkageValue, definition.source.clone(),
              bodySpanValue.clone(), returnSpanValue.clone(), valueSpanValue.clone(),
              zc::mv(orderingKey), shape.body, shape.returnStatement, shape.value, zc::none,
              LeadingLocalConditionalReturn{zc::mv(leadingPending)}});
          continue;
        }
        if (shape.isMatchChainedEquality) {
          // Chained integer match: N literal arms plus one default arm. Build N
          // synthetic equality conditions (scrutinee == literal_i) and N+1
          // arms, then create a PendingChainedConditionalReturn for the
          // lowering pass to materialize as a nested conditional chain.
          auto scrutineeTypeIndex = factIndex(facts.nodeTypes(), shape.condition);
          if (scrutineeTypeIndex == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t scrutineeTypeSlot = 0;
          ZC_IF_SOME(index, scrutineeTypeIndex) { scrutineeTypeSlot = index; }
          const auto operandType = facts.nodeTypes().entries()[scrutineeTypeSlot].value;
          auto boolCanonical = semanticTypes.canonicalizeClosed(type::semantic::TypeData(
              type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::Bool}));
          if (!boolCanonical.is<type::semantic::CanonicalTypeData>()) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto boolInterned =
              semanticTypes.intern(zc::mv(boolCanonical).get<type::semantic::CanonicalTypeData>());
          if (!boolInterned.is<type::SemanticTypeInterned>()) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          const auto boolType = boolInterned.get<type::SemanticTypeInterned>().id;
          auto conditionParameter = resolvedCallableParameter(bound.bindings(), shape.condition);
          if (conditionParameter == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          identity::CallableParameterId conditionHandle;
          ZC_IF_SOME(value, conditionParameter) { conditionHandle = value; }
          auto conditionAuthority = registries.callableParameter(conditionHandle);
          if (conditionAuthority == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          zc::Maybe<identity::CallableParameterKey> parameterKey;
          bool parameterMatches = false;
          ZC_IF_SOME(entry, conditionAuthority) {
            for (const auto& candidate : parameters) {
              if (candidate.key == entry.key() && candidate.type == operandType) {
                parameterMatches = true;
                parameterKey = entry.key().clone();
                break;
              }
            }
          }
          if (!parameterMatches) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          zc::Vector<PendingChainedConditionalReturn::Entry> entries;
          for (size_t armIndex = 0; armIndex < shape.matchChainedLiterals.size(); ++armIndex) {
            const auto literalNode = shape.matchChainedLiterals[armIndex];
            const auto thenValueNode = shape.matchChainedThenValues[armIndex];
            auto literalIndex = factIndex(facts.literals(), literalNode);
            auto literalSpan = bound.parsedModule().spanFor(tree.node(literalNode).range);
            if (literalIndex == zc::none || literalSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            auto leftReference = HirParameterReferenceExpression{
                HirNodeId(), ZC_ASSERT_NONNULL(parameterKey).clone(), operandType,
                HirValueCategory::Place, ZC_ASSERT_NONNULL(conditionSpan).clone()};
            auto leftOperand = PendingConditionalArm{zc::none,
                                                     zc::mv(leftReference),
                                                     zc::none,
                                                     operandType,
                                                     ZC_ASSERT_NONNULL(conditionSpan).clone(),
                                                     zc::none,
                                                     shape.condition};
            auto rightOperand = PendingConditionalArm{literalFact.literal.clone(),
                                                      zc::none,
                                                      zc::none,
                                                      operandType,
                                                      ZC_ASSERT_NONNULL(literalSpan).clone(),
                                                      zc::none,
                                                      literalNode};
            auto equalityCondition =
                PendingEqualityCondition{zc::mv(leftOperand),
                                         zc::mv(rightOperand),
                                         operandType,
                                         boolType,
                                         checker::PrimitiveOperation::Eq,
                                         ZC_ASSERT_NONNULL(conditionSpan).clone(),
                                         false};
            auto thenLiteralIndex = factIndex(facts.literals(), thenValueNode);
            auto thenSpan = bound.parsedModule().spanFor(tree.node(thenValueNode).range);
            if (thenLiteralIndex == zc::none || thenSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t thenLiteralSlot = 0;
            ZC_IF_SOME(index, thenLiteralIndex) { thenLiteralSlot = index; }
            const auto& thenLiteralFact = facts.literals().entries()[thenLiteralSlot].value;
            auto thenArm = PendingConditionalArm{
                thenLiteralFact.literal.clone(),     zc::none, zc::none,     thenType,
                ZC_ASSERT_NONNULL(thenSpan).clone(), zc::none, thenValueNode};
            entries.add(
                PendingChainedConditionalReturn::Entry{zc::mv(equalityCondition), zc::mv(thenArm)});
          }
          auto elseLiteralIndex = factIndex(facts.literals(), shape.matchChainedElseValue);
          auto elseSpan =
              bound.parsedModule().spanFor(tree.node(shape.matchChainedElseValue).range);
          if (elseLiteralIndex == zc::none || elseSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t elseLiteralSlot = 0;
          ZC_IF_SOME(index, elseLiteralIndex) { elseLiteralSlot = index; }
          const auto& elseLiteralFact = facts.literals().entries()[elseLiteralSlot].value;
          auto elseArm = PendingConditionalArm{elseLiteralFact.literal.clone(),
                                               zc::none,
                                               zc::none,
                                               elseType,
                                               ZC_ASSERT_NONNULL(elseSpan).clone(),
                                               zc::none,
                                               shape.matchChainedElseValue};
          auto chainedReturn = PendingChainedConditionalReturn{
              zc::mv(entries), zc::mv(elseArm), valueSpanValue.clone(), shape.matchStatement};
          pendingFunctions.add(PendingFunctionDeclaration{
              definition.definition, definition.node, callable.success, zc::mv(parameters),
              zc::mv(conditionalReceiver), zc::mv(visibilityValue), linkageValue,
              definition.source.clone(), bodySpanValue.clone(), returnSpanValue.clone(),
              valueSpanValue.clone(), zc::mv(orderingKey), shape.body, shape.returnStatement,
              shape.value, zc::none, ChainedConditionalReturn{zc::mv(chainedReturn)}});
          continue;
        }
        PendingConditionalCondition pendingCondition;
        if (shape.isMatchEquality) {
          // Integer match-equality: synthesize `scrutinee == literal` without
          // an AST BinaryExpr node. The scrutinee resolves to a parameter
          // reference (left operand); the pattern literal provides the right
          // operand and its checked literal fact. The comparison has no call
          // fact or comparison-result node-type fact, so the count equations
          // subtract the phantom terms.
          auto scrutineeTypeIndex = factIndex(facts.nodeTypes(), shape.condition);
          auto literalIndex = factIndex(facts.literals(), shape.matchEqualityLiteral);
          auto literalSpan =
              bound.parsedModule().spanFor(tree.node(shape.matchEqualityLiteral).range);
          if (scrutineeTypeIndex == zc::none || literalIndex == zc::none ||
              literalSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t scrutineeTypeSlot = 0;
          size_t literalSlot = 0;
          ZC_IF_SOME(index, scrutineeTypeIndex) { scrutineeTypeSlot = index; }
          ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
          const auto operandType = facts.nodeTypes().entries()[scrutineeTypeSlot].value;
          const auto& literalFact = facts.literals().entries()[literalSlot].value;
          auto boolCanonical = semanticTypes.canonicalizeClosed(type::semantic::TypeData(
              type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::Bool}));
          if (!boolCanonical.is<type::semantic::CanonicalTypeData>()) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto boolInterned =
              semanticTypes.intern(zc::mv(boolCanonical).get<type::semantic::CanonicalTypeData>());
          if (!boolInterned.is<type::SemanticTypeInterned>()) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          const auto boolType = boolInterned.get<type::SemanticTypeInterned>().id;
          auto conditionParameter = resolvedCallableParameter(bound.bindings(), shape.condition);
          if (conditionParameter == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          identity::CallableParameterId conditionHandle;
          ZC_IF_SOME(value, conditionParameter) { conditionHandle = value; }
          auto conditionAuthority = registries.callableParameter(conditionHandle);
          if (conditionAuthority == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          zc::Maybe<PendingConditionalArm> leftOperand;
          ZC_IF_SOME(entry, conditionAuthority) {
            bool matches = false;
            for (const auto& parameterCandidate : parameters) {
              if (parameterCandidate.key == entry.key() && parameterCandidate.type == operandType) {
                matches = true;
              }
            }
            if (!matches) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            auto reference = HirParameterReferenceExpression{
                HirNodeId(), entry.key().clone(), operandType, HirValueCategory::Place,
                ZC_ASSERT_NONNULL(conditionSpan).clone()};
            leftOperand = PendingConditionalArm{zc::none,
                                                zc::mv(reference),
                                                zc::none,
                                                operandType,
                                                ZC_ASSERT_NONNULL(conditionSpan).clone(),
                                                zc::none,
                                                shape.condition};
          }
          auto rightOperand = PendingConditionalArm{literalFact.literal.clone(),
                                                    zc::none,
                                                    zc::none,
                                                    operandType,
                                                    ZC_ASSERT_NONNULL(literalSpan).clone(),
                                                    zc::none,
                                                    shape.matchEqualityLiteral};
          pendingCondition.equality =
              PendingEqualityCondition{zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                       zc::mv(rightOperand),
                                       operandType,
                                       boolType,
                                       checker::PrimitiveOperation::Eq,
                                       ZC_ASSERT_NONNULL(conditionSpan).clone(),
                                       false};
        } else if (shape.conditionIsEquality) {
          // The comparison condition consumes the checked relational call fact
          // whose two arguments are the left and right operands. Each operand is
          // either a parameter reference or a scalar literal; at least one is a
          // parameter, from which the shared operand type is derived.
          auto leftTypeIndex = factIndex(facts.nodeTypes(), shape.conditionLeft);
          auto rightTypeIndex = factIndex(facts.nodeTypes(), shape.conditionRight);
          auto callIndex = factIndex(facts.calls(), shape.condition);
          auto leftSpan = bound.parsedModule().spanFor(tree.node(shape.conditionLeft).range);
          auto rightSpan = bound.parsedModule().spanFor(tree.node(shape.conditionRight).range);
          if (leftTypeIndex == zc::none || rightTypeIndex == zc::none || callIndex == zc::none ||
              leftSpan == zc::none || rightSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t leftTypeSlot = 0;
          size_t rightTypeSlot = 0;
          size_t callSlot = 0;
          ZC_IF_SOME(index, leftTypeIndex) { leftTypeSlot = index; }
          ZC_IF_SOME(index, rightTypeIndex) { rightTypeSlot = index; }
          ZC_IF_SOME(index, callIndex) { callSlot = index; }
          const auto operandType = facts.nodeTypes().entries()[leftTypeSlot].value;
          const auto rightType = facts.nodeTypes().entries()[rightTypeSlot].value;
          const auto& callFact = facts.calls().entries()[callSlot].value;
          const auto& call = callFact.invocation;
          const auto& selected = call.selected.variant();
          // Both operand type facts must share the primitive scalar type and the
          // call fact must match the binary-operation contract exactly for one
          // of the six relational comparison operators or the two logical
          // short-circuit operators (all produce bool).
          const auto conditionOp = selected.get<checker::checked::PrimitiveCallable>().operation;
          const bool conditionOpSupported =
              isScalarComparisonOperation(conditionOp) ||
              conditionOp == checker::PrimitiveOperation::LogicalAnd ||
              conditionOp == checker::PrimitiveOperation::LogicalOr;
          if (operandType != rightType || callFact.node != shape.condition ||
              !selected.is<checker::checked::PrimitiveCallable>() || !conditionOpSupported ||
              call.calleeType != operandType || call.receiver != zc::none ||
              call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
              call.arguments.size() != 2 || call.arguments[0].sourceNode != shape.conditionLeft ||
              call.arguments[0].sourceType != operandType ||
              call.arguments[1].sourceNode != shape.conditionRight ||
              call.arguments[1].sourceType != operandType || call.successType != conditionType ||
              call.resultType != conditionType || call.substitutions != zc::none ||
              call.witnesses != zc::none || call.raises != zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          // Build one operand: a parameter operand resolves to a parameter
          // reference; a literal operand consumes its checked literal fact. Both
          // operands must share the derived operand type.
          bool operandRejected = false;
          auto buildOperand =
              [&](ast::NodeId operandNode, bool isLiteral,
                  const identity::SourceSpan& operandSpan) -> zc::Maybe<PendingConditionalArm> {
            if (!isLiteral) {
              auto parameter = resolvedCallableParameter(bound.bindings(), operandNode);
              if (parameter == zc::none) {
                operandRejected = true;
                return zc::none;
              }
              identity::CallableParameterId handle;
              ZC_IF_SOME(value, parameter) { handle = value; }
              auto authority = registries.callableParameter(handle);
              if (authority == zc::none) {
                operandRejected = true;
                return zc::none;
              }
              zc::Maybe<PendingConditionalArm> built;
              ZC_IF_SOME(entry, authority) {
                bool matches = false;
                for (const auto& parameterCandidate : parameters) {
                  if (parameterCandidate.key == entry.key() &&
                      parameterCandidate.type == operandType) {
                    matches = true;
                  }
                }
                if (!matches) {
                  operandRejected = true;
                } else {
                  auto reference =
                      HirParameterReferenceExpression{HirNodeId(), entry.key().clone(), operandType,
                                                      HirValueCategory::Place, operandSpan.clone()};
                  built = PendingConditionalArm{zc::none,    zc::mv(reference),   zc::none,
                                                operandType, operandSpan.clone(), zc::none,
                                                operandNode};
                }
              }
              return built;
            }
            auto literalIndex = factIndex(facts.literals(), operandNode);
            if (literalIndex == zc::none) {
              operandRejected = true;
              return zc::none;
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            return PendingConditionalArm{
                literalFact.literal.clone(), zc::none, zc::none,   operandType,
                operandSpan.clone(),         zc::none, operandNode};
          };
          auto leftOperand = buildOperand(shape.conditionLeft, shape.conditionLeftIsLiteral,
                                          ZC_ASSERT_NONNULL(leftSpan));
          auto rightOperand = buildOperand(shape.conditionRight, shape.conditionRightIsLiteral,
                                           ZC_ASSERT_NONNULL(rightSpan));
          if (operandRejected || leftOperand == zc::none || rightOperand == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          pendingCondition.equality = PendingEqualityCondition{
              zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
              zc::mv(ZC_ASSERT_NONNULL(rightOperand)),
              operandType,
              conditionType,
              selected.get<checker::checked::PrimitiveCallable>().operation,
              ZC_ASSERT_NONNULL(conditionSpan).clone(),
              false,
              shape.condition};
        } else if (shape.conditionIsUnary) {
          // The unary `!x` condition desugars to `x == false`. The operand is
          // a parameter reference or a scalar literal; the synthetic false
          // operand has no AST node and carries no checker-produced fact. The
          // desugared comparison reuses the equality condition path.
          auto operandTypeIndex = factIndex(facts.nodeTypes(), shape.conditionUnaryOperand);
          auto callIndex = factIndex(facts.calls(), shape.condition);
          auto operandSpan =
              bound.parsedModule().spanFor(tree.node(shape.conditionUnaryOperand).range);
          if (operandTypeIndex == zc::none || callIndex == zc::none || operandSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t operandTypeSlot = 0;
          size_t callSlot = 0;
          ZC_IF_SOME(index, operandTypeIndex) { operandTypeSlot = index; }
          ZC_IF_SOME(index, callIndex) { callSlot = index; }
          const auto operandType = facts.nodeTypes().entries()[operandTypeSlot].value;
          const auto& callFact = facts.calls().entries()[callSlot].value;
          const auto& call = callFact.invocation;
          const auto& selected = call.selected.variant();
          // The call fact must match the primitive-unary contract: one argument
          // (the operand), a LogicalNot primitive callable, and a bool result.
          if (callFact.node != shape.condition ||
              !selected.is<checker::checked::PrimitiveCallable>() ||
              selected.get<checker::checked::PrimitiveCallable>().operation !=
                  checker::PrimitiveOperation::LogicalNot ||
              call.calleeType != operandType || call.receiver != zc::none ||
              call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
              call.arguments.size() != 1 ||
              call.arguments[0].sourceNode != shape.conditionUnaryOperand ||
              call.arguments[0].sourceType != operandType || call.successType != conditionType ||
              call.resultType != conditionType || call.substitutions != zc::none ||
              call.witnesses != zc::none || call.raises != zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          // Build the real operand: a parameter reference or a scalar literal.
          bool operandRejected = false;
          zc::Maybe<PendingConditionalArm> realOperand;
          if (!shape.conditionUnaryOperandIsLiteral) {
            auto parameter =
                resolvedCallableParameter(bound.bindings(), shape.conditionUnaryOperand);
            if (parameter == zc::none) {
              operandRejected = true;
            } else {
              identity::CallableParameterId handle;
              ZC_IF_SOME(value, parameter) { handle = value; }
              auto authority = registries.callableParameter(handle);
              if (authority == zc::none) {
                operandRejected = true;
              } else {
                ZC_IF_SOME(entry, authority) {
                  bool matches = false;
                  for (const auto& parameterCandidate : parameters) {
                    if (parameterCandidate.key == entry.key() &&
                        parameterCandidate.type == operandType) {
                      matches = true;
                    }
                  }
                  if (!matches) {
                    operandRejected = true;
                  } else {
                    auto reference = HirParameterReferenceExpression{
                        HirNodeId(), entry.key().clone(), operandType, HirValueCategory::Place,
                        ZC_ASSERT_NONNULL(operandSpan).clone()};
                    realOperand = PendingConditionalArm{zc::none,
                                                        zc::mv(reference),
                                                        zc::none,
                                                        operandType,
                                                        ZC_ASSERT_NONNULL(operandSpan).clone(),
                                                        zc::none,
                                                        shape.conditionUnaryOperand};
                  }
                }
              }
            }
          } else {
            auto literalIndex = factIndex(facts.literals(), shape.conditionUnaryOperand);
            if (literalIndex == zc::none) {
              operandRejected = true;
            } else {
              size_t literalSlot = 0;
              ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
              const auto& literalFact = facts.literals().entries()[literalSlot].value;
              realOperand = PendingConditionalArm{literalFact.literal.clone(),
                                                  zc::none,
                                                  zc::none,
                                                  operandType,
                                                  ZC_ASSERT_NONNULL(operandSpan).clone(),
                                                  zc::none,
                                                  shape.conditionUnaryOperand};
            }
          }
          if (operandRejected || realOperand == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          // The synthetic false operand: `!x` desugars to `x == false`.
          auto syntheticValue = checker::checked::CanonicalConstValue::boolean(false);
          auto syntheticOperand =
              PendingConditionalArm{zc::mv(syntheticValue), zc::none, zc::none, operandType,
                                    ZC_ASSERT_NONNULL(conditionSpan).clone()};
          pendingCondition.equality =
              PendingEqualityCondition{zc::mv(ZC_ASSERT_NONNULL(realOperand)),
                                       zc::mv(syntheticOperand),
                                       operandType,
                                       conditionType,
                                       checker::PrimitiveOperation::Eq,
                                       ZC_ASSERT_NONNULL(conditionSpan).clone(),
                                       true};
        } else if (shape.hasMatchGuard) {
          // Match-guard: the condition is a conjunction of the scrutinee bool
          // parameter and the guard comparison. The scrutinee resolves to a
          // parameter reference (the bare-parameter path); the guard resolves
          // to an equality condition whose operands are a parameter reference
          // and a scalar literal. The HIR builder materializes the guard
          // comparison and the conjunction (BitAnd) as two primitive binaries,
          // keeping the four-block diamond CFG.
          auto conditionParameter = resolvedCallableParameter(bound.bindings(), shape.condition);
          if (conditionParameter == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          identity::CallableParameterId conditionHandle;
          ZC_IF_SOME(value, conditionParameter) { conditionHandle = value; }
          auto conditionAuthority = registries.callableParameter(conditionHandle);
          if (conditionAuthority == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          zc::Maybe<HirParameterReferenceExpression> scrutineeRef;
          ZC_IF_SOME(entry, conditionAuthority) {
            scrutineeRef = HirParameterReferenceExpression{
                HirNodeId(), entry.key().clone(), conditionType, HirValueCategory::Place,
                ZC_ASSERT_NONNULL(conditionSpan).clone()};
          }
          if (scrutineeRef == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          // The guard is a BinaryExpr whose one operand is a bare identifier
          // (parameter reference) and whose other operand is a scalar literal.
          const ast::NodeId guardLeft(
              tree.node(shape.matchGuard).payload.words[ast::kBinaryExprLhsWord]);
          const ast::NodeId guardRight(
              tree.node(shape.matchGuard).payload.words[ast::kBinaryExprRhsWord]);
          const bool guardLeftIsIdent =
              tree.contains(guardLeft) && tree.node(guardLeft).kind == ast::SyntaxKind::IdentExpr;
          const bool guardRightIsIdent =
              tree.contains(guardRight) && tree.node(guardRight).kind == ast::SyntaxKind::IdentExpr;
          if (guardLeftIsIdent == guardRightIsIdent) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          const ast::NodeId guardParamNode = guardLeftIsIdent ? guardLeft : guardRight;
          const ast::NodeId guardLiteralNode = guardLeftIsIdent ? guardRight : guardLeft;
          auto guardLeftTypeIndex = factIndex(facts.nodeTypes(), guardLeft);
          auto guardRightTypeIndex = factIndex(facts.nodeTypes(), guardRight);
          auto guardCallIndex = factIndex(facts.calls(), shape.matchGuard);
          auto guardLeftSpan = bound.parsedModule().spanFor(tree.node(guardLeft).range);
          auto guardRightSpan = bound.parsedModule().spanFor(tree.node(guardRight).range);
          if (guardLeftTypeIndex == zc::none || guardRightTypeIndex == zc::none ||
              guardCallIndex == zc::none || guardLeftSpan == zc::none ||
              guardRightSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t guardLeftTypeSlot = 0;
          size_t guardRightTypeSlot = 0;
          size_t guardCallSlot = 0;
          ZC_IF_SOME(index, guardLeftTypeIndex) { guardLeftTypeSlot = index; }
          ZC_IF_SOME(index, guardRightTypeIndex) { guardRightTypeSlot = index; }
          ZC_IF_SOME(index, guardCallIndex) { guardCallSlot = index; }
          const auto guardOperandType = facts.nodeTypes().entries()[guardLeftTypeSlot].value;
          const auto guardRightType = facts.nodeTypes().entries()[guardRightTypeSlot].value;
          const auto& guardCallFact = facts.calls().entries()[guardCallSlot].value;
          const auto& guardCall = guardCallFact.invocation;
          const auto& guardSelected = guardCall.selected.variant();
          const auto guardOp = guardSelected.get<checker::checked::PrimitiveCallable>().operation;
          if (guardOperandType != guardRightType || guardCallFact.node != shape.matchGuard ||
              !guardSelected.is<checker::checked::PrimitiveCallable>() ||
              !isScalarComparisonOperation(guardOp) || guardCall.calleeType != guardOperandType ||
              guardCall.receiver != zc::none || guardCall.receiverMode != zc::none ||
              guardCall.receiverAdjustment != zc::none || guardCall.arguments.size() != 2 ||
              guardCall.arguments[0].sourceNode != guardLeft ||
              guardCall.arguments[0].sourceType != guardOperandType ||
              guardCall.arguments[1].sourceNode != guardRight ||
              guardCall.arguments[1].sourceType != guardOperandType ||
              guardCall.successType != conditionType || guardCall.resultType != conditionType ||
              guardCall.substitutions != zc::none || guardCall.witnesses != zc::none ||
              guardCall.raises != zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          // Build the guard parameter operand.
          auto guardParameter = resolvedCallableParameter(bound.bindings(), guardParamNode);
          if (guardParameter == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          identity::CallableParameterId guardParamHandle;
          ZC_IF_SOME(value, guardParameter) { guardParamHandle = value; }
          auto guardParamAuthority = registries.callableParameter(guardParamHandle);
          if (guardParamAuthority == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          zc::Maybe<PendingConditionalArm> guardParamArm;
          ZC_IF_SOME(entry, guardParamAuthority) {
            bool matches = false;
            for (const auto& parameterCandidate : parameters) {
              if (parameterCandidate.key == entry.key() &&
                  parameterCandidate.type == guardOperandType) {
                matches = true;
              }
            }
            if (!matches) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            auto reference = HirParameterReferenceExpression{
                HirNodeId(), entry.key().clone(), guardOperandType, HirValueCategory::Place,
                (guardLeftIsIdent ? ZC_ASSERT_NONNULL(guardLeftSpan)
                                  : ZC_ASSERT_NONNULL(guardRightSpan))
                    .clone()};
            guardParamArm =
                PendingConditionalArm{zc::none,
                                      zc::mv(reference),
                                      zc::none,
                                      guardOperandType,
                                      (guardLeftIsIdent ? ZC_ASSERT_NONNULL(guardLeftSpan)
                                                        : ZC_ASSERT_NONNULL(guardRightSpan))
                                          .clone(),
                                      zc::none,
                                      guardParamNode};
          }
          if (guardParamArm == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          // Build the guard literal operand.
          auto guardLiteralIndex = factIndex(facts.literals(), guardLiteralNode);
          if (guardLiteralIndex == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t guardLiteralSlot = 0;
          ZC_IF_SOME(index, guardLiteralIndex) { guardLiteralSlot = index; }
          const auto& guardLiteralFact = facts.literals().entries()[guardLiteralSlot].value;
          auto guardLiteralArm =
              PendingConditionalArm{guardLiteralFact.literal.clone(),
                                    zc::none,
                                    zc::none,
                                    guardOperandType,
                                    (guardLeftIsIdent ? ZC_ASSERT_NONNULL(guardRightSpan)
                                                      : ZC_ASSERT_NONNULL(guardLeftSpan))
                                        .clone(),
                                    zc::none,
                                    guardLiteralNode};
          // The guard comparison condition. The left operand is the parameter
          // and the right operand is the literal when the guard source is
          // `param <op> literal`; the roles swap for `literal <op> param`.
          auto guardEquality = PendingEqualityCondition{
              guardLeftIsIdent ? zc::mv(ZC_ASSERT_NONNULL(guardParamArm)) : zc::mv(guardLiteralArm),
              guardLeftIsIdent ? zc::mv(guardLiteralArm) : zc::mv(ZC_ASSERT_NONNULL(guardParamArm)),
              guardOperandType,
              conditionType,
              guardOp,
              ZC_ASSERT_NONNULL(conditionSpan).clone(),
              false,
              shape.matchGuard};
          pendingCondition.conjunctive = PendingConjunctiveCondition{
              zc::mv(ZC_ASSERT_NONNULL(scrutineeRef)), zc::mv(guardEquality),
              ZC_ASSERT_NONNULL(conditionSpan).clone(), shape.matchGuard};
        } else if (shape.conditionIsCall) {
          // A direct call condition: `if (f(args))`. The checked call fact on
          // the condition node resolves to a DirectCallable whose callee is a
          // free function. The call result is bool and drives the conditional.
          // The HIR builder materializes the call as a HirDirectCallExpression;
          // the MIR builder evaluates it into a temporary before SwitchInt.
          const auto& sourceCall = tree.node(shape.condition);
          const ast::NodeId calleeNode(sourceCall.payload.words[ast::kCallExpressionCalleeWord]);
          const ast::NodeList arguments{sourceCall.payload.words[ast::kCallExpressionArgsFirstWord],
                                        sourceCall.payload.words[ast::kCallExpressionArgsSizeWord]};
          auto calleeTypeIndex = factIndex(facts.nodeTypes(), calleeNode);
          auto checkedCallIndex = factIndex(facts.calls(), shape.condition);
          auto callKey = checkedNodeKey(tree, bound.parsedModule(), shape.condition);
          auto dispatchIndex = callKey == zc::none
                                   ? zc::none
                                   : dispatchFactIndex(checkedModule.dispatchFacts().facts(),
                                                       ZC_ASSERT_NONNULL(callKey));
          auto callee = resolvedDefinition(bound.bindings(), calleeNode);
          if (!tree.contains(calleeNode) ||
              tree.node(calleeNode).kind != ast::SyntaxKind::IdentExpr ||
              !tree.contains(arguments) || calleeTypeIndex == zc::none ||
              checkedCallIndex == zc::none || callKey == zc::none || dispatchIndex == zc::none ||
              callee == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t calleeTypeSlot = 0;
          size_t checkedCallSlot = 0;
          size_t dispatchSlot = 0;
          ZC_IF_SOME(index, calleeTypeIndex) { calleeTypeSlot = index; }
          ZC_IF_SOME(index, checkedCallIndex) { checkedCallSlot = index; }
          ZC_IF_SOME(index, dispatchIndex) { dispatchSlot = index; }
          const auto& checkedCall = facts.calls().entries()[checkedCallSlot].value;
          const auto& invocation = checkedCall.invocation;
          const auto& selected = invocation.selected.variant();
          const auto& dispatch = checkedModule.dispatchFacts().facts()[dispatchSlot];
          const auto& target = dispatch.fact.target.variant();
          const auto& transform = dispatch.fact.resultTransform.variant();
          bool dispatchOwnerMatches = false;
          ZC_IF_SOME(owner, dispatch.owner) {
            dispatchOwnerMatches = owner == definition.definition;
          }
          if (!selected.is<checker::checked::DirectCallable>() ||
              selected.get<checker::checked::DirectCallable>().callee !=
                  ZC_ASSERT_NONNULL(callee) ||
              invocation.calleeType != facts.nodeTypes().entries()[calleeTypeSlot].value ||
              invocation.successType != conditionType || invocation.resultType != conditionType ||
              invocation.receiver != zc::none || invocation.receiverMode != zc::none ||
              invocation.receiverAdjustment != zc::none ||
              invocation.arguments.size() != arguments.size ||
              invocation.substitutions != zc::none || invocation.witnesses != zc::none ||
              invocation.raises != zc::none || !dispatchOwnerMatches ||
              !target.is<checker::dispatch::DirectTarget>() ||
              target.get<checker::dispatch::DirectTarget>().callee != ZC_ASSERT_NONNULL(callee) ||
              !transform.is<checker::dispatch::IdentityResultTransform>() ||
              dispatch.fact.receiver != zc::none ||
              dispatch.fact.arguments.size() != arguments.size ||
              dispatch.fact.successType != conditionType ||
              dispatch.fact.resultType != conditionType ||
              dispatch.fact.substitutions != zc::none || dispatch.fact.witnesses != zc::none ||
              dispatch.fact.raises != zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          // Build the call arguments: each is a scalar literal or a parameter
          // reference, matching the shape classification contract.
          zc::Vector<HirDirectCallArgument> callArguments;
          const auto argumentNodes = tree.list(arguments);
          for (size_t index = 0; index < argumentNodes.size(); ++index) {
            const auto argument = argumentNodes[index];
            auto argumentTypeIndex = factIndex(facts.nodeTypes(), argument);
            auto argumentSpan = bound.parsedModule().spanFor(tree.node(argument).range);
            const bool isLiteralArgument =
                tree.contains(argument) && isScalarLiteral(tree.node(argument).kind);
            const bool isParameterArgument =
                tree.contains(argument) && tree.node(argument).kind == ast::SyntaxKind::IdentExpr;
            if ((!isLiteralArgument && !isParameterArgument) || argumentTypeIndex == zc::none ||
                argumentSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t argumentTypeSlot = 0;
            ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
            const auto& checkedArgument = invocation.arguments[index];
            const auto& dispatchArgument = dispatch.fact.arguments[index];
            const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
            if (checkedArgument.sourceNode != argument ||
                checkedArgument.sourceType != argumentType ||
                checkedArgument.parameterType != argumentType ||
                checkedArgument.adjustment != zc::none ||
                dispatchArgument.sourceType != argumentType ||
                dispatchArgument.parameterType != argumentType ||
                dispatchArgument.adjustment != zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            if (isLiteralArgument) {
              auto literalIndex = factIndex(facts.literals(), argument);
              if (literalIndex == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              size_t literalSlot = 0;
              ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
              const auto& literal = facts.literals().entries()[literalSlot].value;
              if (literal.node != argument || literal.type != argumentType) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              zc::Maybe<identity::CallableParameterKey> noParameter;
              callArguments.add(HirDirectCallArgument{
                  argumentType, literal.literal.clone(), zc::mv(noParameter), zc::none, zc::none,
                  zc::none, ZC_ASSERT_NONNULL(argumentSpan).clone()});
            } else {
              auto parameter = resolvedCallableParameter(bound.bindings(), argument);
              if (parameter == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              identity::CallableParameterId handle;
              ZC_IF_SOME(value, parameter) { handle = value; }
              auto authority = registries.callableParameter(handle);
              if (authority == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              zc::Maybe<identity::CallableParameterKey> parameterKey;
              ZC_IF_SOME(entry, authority) {
                bool matches = false;
                for (const auto& parameterCandidate : parameters) {
                  if (parameterCandidate.key == entry.key() &&
                      parameterCandidate.type == argumentType) {
                    matches = true;
                  }
                }
                if (!matches) {
                  return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                       ir::IrFailureKind::InvalidFact, module,
                                                       registries, ordinal + 2);
                }
                parameterKey = entry.key().clone();
              }
              zc::Maybe<checker::checked::CanonicalConstValue> noValue;
              callArguments.add(HirDirectCallArgument{
                  argumentType, zc::mv(noValue), zc::mv(parameterKey), zc::none, zc::none, zc::none,
                  ZC_ASSERT_NONNULL(argumentSpan).clone()});
            }
          }
          pendingCondition.call = PendingCallCondition{
              HirDirectCallExpression{HirNodeId(), ZC_ASSERT_NONNULL(callee), invocation.calleeType,
                                      conditionType, zc::mv(callArguments),
                                      ZC_ASSERT_NONNULL(conditionSpan).clone()},
              ZC_ASSERT_NONNULL(conditionSpan).clone(), shape.condition};
        } else {
          auto conditionParameter = resolvedCallableParameter(bound.bindings(), shape.condition);
          if (conditionParameter == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          identity::CallableParameterId conditionHandle;
          ZC_IF_SOME(value, conditionParameter) { conditionHandle = value; }
          auto conditionAuthority = registries.callableParameter(conditionHandle);
          if (conditionAuthority == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(entry, conditionAuthority) {
            pendingCondition.parameter = HirParameterReferenceExpression{
                HirNodeId(), entry.key().clone(), conditionType, HirValueCategory::Place,
                ZC_ASSERT_NONNULL(conditionSpan).clone()};
          }
        }
        // Build one conditional arm from its return-value node. A scalar-literal
        // arm carries the checked literal fact; a bare-identifier arm resolves to
        // a function parameter reference (no literal fact is required for it); a
        // binary arm resolves the checked call fact and lowers its two leaf
        // operands into a primitive binary operation. An enum-destructure arm
        // resolves the pattern binding to a positional field extraction off the
        // scrutinee parameter (payload at field index 1).
        bool armRejected = false;
        auto buildArm =
            [&](ast::NodeId armNode, identity::SemanticTypeId armType,
                const identity::SourceSpan& armSpan,
                zc::Maybe<ast::NodeId> patternBinding) -> zc::Maybe<PendingConditionalArm> {
          if (tree.node(armNode).kind == ast::SyntaxKind::IdentExpr) {
            // Enum destructure: the arm is a pattern binding, not a callable
            // parameter. Resolve the scrutinee parameter and synthesize a
            // positional field extraction (payload at aggregate index 1).
            if (shape.isMatchEnumDestructure && patternBinding != zc::none) {
              const auto armName = tree.node(armNode).payload.words[ast::kIdentExprNameWord];
              const auto bindingName = tree.node(ZC_ASSERT_NONNULL(patternBinding))
                                           .payload.words[ast::kIdentifierPatternNameWord];
              if (armName == bindingName) {
                auto scrutineeParameter =
                    resolvedCallableParameter(bound.bindings(), shape.condition);
                if (scrutineeParameter == zc::none) {
                  armRejected = true;
                  return zc::none;
                }
                identity::CallableParameterId scrutineeHandle;
                ZC_IF_SOME(value, scrutineeParameter) { scrutineeHandle = value; }
                auto scrutineeAuthority = registries.callableParameter(scrutineeHandle);
                if (scrutineeAuthority == zc::none) {
                  armRejected = true;
                  return zc::none;
                }
                zc::Maybe<PendingConditionalArm> built;
                ZC_IF_SOME(entry, scrutineeAuthority) {
                  bool matches = false;
                  for (const auto& parameterCandidate : parameters) {
                    if (parameterCandidate.key == entry.key() &&
                        parameterCandidate.type == conditionType) {
                      matches = true;
                    }
                  }
                  if (!matches) {
                    armRejected = true;
                  } else {
                    // Resolve the enum definition's DefId from the scrutinee's
                    // nominal type for fact correlation.
                    auto typeLookup = semanticTypes.get(conditionType);
                    if (!typeLookup.is<type::SemanticTypeLookup>()) {
                      armRejected = true;
                    } else {
                      const auto& typeData = typeLookup.get<type::SemanticTypeLookup>().data();
                      if (!typeData.is<type::semantic::NominalTypeData>()) {
                        armRejected = true;
                      } else {
                        const auto& nominal = typeData.get<type::semantic::NominalTypeData>();
                        auto projection = PendingConditionalArmParameterFieldProjection{
                            entry.key().clone(), conditionType, nominal.definition, armType, 1};
                        built = PendingConditionalArm{
                            zc::none, zc::none, zc::none, armType,           armSpan.clone(),
                            zc::none, armNode,  zc::none, zc::mv(projection)};
                      }
                    }
                  }
                }
                return built;
              }
            }
            auto parameter = resolvedCallableParameter(bound.bindings(), armNode);
            if (parameter == zc::none) {
              armRejected = true;
              return zc::none;
            }
            identity::CallableParameterId handle;
            ZC_IF_SOME(value, parameter) { handle = value; }
            auto authority = registries.callableParameter(handle);
            if (authority == zc::none) {
              armRejected = true;
              return zc::none;
            }
            zc::Maybe<PendingConditionalArm> built;
            ZC_IF_SOME(entry, authority) {
              bool matches = false;
              for (const auto& parameterCandidate : parameters) {
                if (parameterCandidate.key == entry.key() && parameterCandidate.type == armType) {
                  matches = true;
                }
              }
              if (!matches) {
                armRejected = true;
              } else {
                auto reference =
                    HirParameterReferenceExpression{HirNodeId(), entry.key().clone(), armType,
                                                    HirValueCategory::Place, armSpan.clone()};
                built = PendingConditionalArm{zc::none,        zc::mv(reference), zc::none, armType,
                                              armSpan.clone(), zc::none,          armNode};
              }
            }
            return built;
          }
          if (tree.node(armNode).kind == ast::SyntaxKind::BinaryExpr) {
            // An arithmetic or comparison arm (`0 - x`, `a < b`): resolve the
            // checked call fact and lower the two leaf operands into a binary
            // arm. The operands are bare parameter references or scalar
            // literals; nested binaries keep their own future shape.
            auto armCallIndex = factIndex(facts.calls(), armNode);
            if (armCallIndex == zc::none) {
              armRejected = true;
              return zc::none;
            }
            size_t armCallSlot = 0;
            ZC_IF_SOME(index, armCallIndex) { armCallSlot = index; }
            const auto& armCallFact = facts.calls().entries()[armCallSlot].value;
            const auto& armCall = armCallFact.invocation;
            const auto& armSelected = armCall.selected.variant();
            if (!armSelected.is<checker::checked::PrimitiveCallable>()) {
              armRejected = true;
              return zc::none;
            }
            const auto armOperation =
                armSelected.get<checker::checked::PrimitiveCallable>().operation;
            if (!isScalarArithmeticOperation(armOperation) &&
                !isScalarComparisonOperation(armOperation)) {
              armRejected = true;
              return zc::none;
            }
            if (armCall.arguments.size() != 2 || armCall.resultType != armType ||
                armCall.successType != armType || armCall.receiver != zc::none ||
                armCall.receiverMode != zc::none || armCall.receiverAdjustment != zc::none) {
              armRejected = true;
              return zc::none;
            }
            const auto armOperandType = armCall.arguments[0].sourceType;
            if (armCall.arguments[1].sourceType != armOperandType ||
                armCall.calleeType != armOperandType) {
              armRejected = true;
              return zc::none;
            }
            auto buildLeaf = [&](ast::NodeId operandNode) -> zc::Maybe<PendingConditionalArm> {
              if (tree.node(operandNode).kind == ast::SyntaxKind::IdentExpr) {
                auto parameter = resolvedCallableParameter(bound.bindings(), operandNode);
                if (parameter == zc::none) {
                  armRejected = true;
                  return zc::none;
                }
                identity::CallableParameterId leafHandle;
                ZC_IF_SOME(value, parameter) { leafHandle = value; }
                auto leafAuthority = registries.callableParameter(leafHandle);
                if (leafAuthority == zc::none) {
                  armRejected = true;
                  return zc::none;
                }
                zc::Maybe<PendingConditionalArm> leafBuilt;
                ZC_IF_SOME(entry, leafAuthority) {
                  bool leafMatches = false;
                  for (const auto& parameterCandidate : parameters) {
                    if (parameterCandidate.key == entry.key() &&
                        parameterCandidate.type == armOperandType) {
                      leafMatches = true;
                    }
                  }
                  auto leafSpan = bound.parsedModule().spanFor(tree.node(operandNode).range);
                  if (!leafMatches || leafSpan == zc::none) {
                    armRejected = true;
                  } else {
                    auto reference = HirParameterReferenceExpression{
                        HirNodeId(), entry.key().clone(), armOperandType, HirValueCategory::Place,
                        ZC_ASSERT_NONNULL(leafSpan).clone()};
                    leafBuilt = PendingConditionalArm{zc::none,
                                                      zc::mv(reference),
                                                      zc::none,
                                                      armOperandType,
                                                      ZC_ASSERT_NONNULL(leafSpan).clone(),
                                                      zc::none,
                                                      operandNode};
                  }
                }
                return leafBuilt;
              }
              if (!isScalarLiteral(tree.node(operandNode).kind)) {
                armRejected = true;
                return zc::none;
              }
              auto leafLiteralIndex = factIndex(facts.literals(), operandNode);
              if (leafLiteralIndex == zc::none) {
                armRejected = true;
                return zc::none;
              }
              size_t leafLiteralSlot = 0;
              ZC_IF_SOME(index, leafLiteralIndex) { leafLiteralSlot = index; }
              const auto& leafLiteralFact = facts.literals().entries()[leafLiteralSlot].value;
              auto leafSpan = bound.parsedModule().spanFor(tree.node(operandNode).range);
              if (leafSpan == zc::none) {
                armRejected = true;
                return zc::none;
              }
              return PendingConditionalArm{
                  leafLiteralFact.literal.clone(),     zc::none, zc::none,   armOperandType,
                  ZC_ASSERT_NONNULL(leafSpan).clone(), zc::none, operandNode};
            };
            auto armLeft = buildLeaf(armCall.arguments[0].sourceNode);
            auto armRight = buildLeaf(armCall.arguments[1].sourceNode);
            if (armRejected || armLeft == zc::none || armRight == zc::none) { return zc::none; }
            auto armBinary = PendingConditionalArmBinary{
                zc::heap<PendingConditionalArm>(zc::mv(ZC_ASSERT_NONNULL(armLeft))),
                zc::heap<PendingConditionalArm>(zc::mv(ZC_ASSERT_NONNULL(armRight))),
                armOperandType, armOperation};
            return PendingConditionalArm{zc::none,        zc::none,          zc::none, armType,
                                         armSpan.clone(), zc::mv(armBinary), armNode};
          }
          auto literalIndex = factIndex(facts.literals(), armNode);
          if (literalIndex == zc::none) {
            armRejected = true;
            return zc::none;
          }
          size_t literalSlot = 0;
          ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
          const auto& literalFact = facts.literals().entries()[literalSlot].value;
          return PendingConditionalArm{literalFact.literal.clone(),
                                       zc::none,
                                       zc::none,
                                       armType,
                                       armSpan.clone(),
                                       zc::none,
                                       armNode};
        };
        zc::Maybe<ast::NodeId> thenPatternBinding;
        zc::Maybe<ast::NodeId> elsePatternBinding;
        if (shape.isMatchEnumDestructure && shape.matchDestructureBindings.size() >= 2) {
          thenPatternBinding = shape.matchDestructureBindings[0];
          elsePatternBinding = shape.matchDestructureBindings[1];
        }
        auto thenArm = buildArm(shape.thenReturnValue, thenType, ZC_ASSERT_NONNULL(thenSpan),
                                thenPatternBinding);
        auto elseArm = buildArm(shape.elseReturnValue, elseType, ZC_ASSERT_NONNULL(elseSpan),
                                elsePatternBinding);
        if (armRejected || thenArm == zc::none || elseArm == zc::none) {
          // A conditional arm that is neither a scalar literal, a bare parameter
          // reference, nor a one-level arithmetic/comparison binary is legal
          // source the lowering carrier does not implement yet. Drain it as a
          // per-definition capability rejection (ZOM4099) rather than an
          // internal invalid-fact incident.
          return rejectHirCapability<HirModuleCandidate>(
              definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
              definition.source.clone());
        }
        {
          auto conditionalReturn = PendingConditionalReturn{
              zc::mv(pendingCondition),
              zc::mv(ZC_ASSERT_NONNULL(thenArm)),
              zc::mv(ZC_ASSERT_NONNULL(elseArm)),
              valueSpanValue.clone(),
              shape.isMatchReturn,
              shape.matchHasDefaultArm,
              shape.isMatchEquality,
              shape.hasMatchGuard,
              shape.isMatchReturn ? shape.matchStatement : shape.condition};
          pendingFunctions.add(PendingFunctionDeclaration{
              definition.definition, definition.node, callable.success, zc::mv(parameters),
              zc::mv(conditionalReceiver), zc::mv(visibilityValue), linkageValue,
              definition.source.clone(), bodySpanValue.clone(), returnSpanValue.clone(),
              valueSpanValue.clone(), zc::mv(orderingKey), shape.body, shape.returnStatement,
              shape.value, zc::none, ConditionalReturn{zc::mv(conditionalReturn)}});
        }
        continue;
      }
      if (shape.isLoop) {
        // Loop shape: a `while` with a bool parameter condition and empty body,
        // followed by a scalar return.
        auto conditionTypeIndex = factIndex(facts.nodeTypes(), shape.loopCondition);
        auto returnTypeIndex = factIndex(facts.nodeTypes(), shape.value);
        auto returnLiteralIndex = factIndex(facts.literals(), shape.value);
        auto bodySpan = bound.parsedModule().spanFor(tree.node(shape.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(shape.returnStatement).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(shape.value).range);
        auto conditionSpan = bound.parsedModule().spanFor(tree.node(shape.loopCondition).range);
        auto loopSpan = bound.parsedModule().spanFor(tree.node(shape.loopStatement).range);
        if (conditionTypeIndex == zc::none || returnTypeIndex == zc::none ||
            returnLiteralIndex == zc::none || bodySpan == zc::none || returnSpan == zc::none ||
            valueSpan == zc::none || conditionSpan == zc::none || loopSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const auto& signature = *signaturePtr;
        const auto& root = signatures.roots[rootSlot];
        if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
            !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        auto functionVisibility = visibility(root.visibility);
        auto functionLinkage = linkage(callable);
        if (functionVisibility == zc::none || functionLinkage == zc::none ||
            signature.definitionKind != identity::DefinitionKind::Function ||
            root.sourceModule != module || root.canonicalDefinition != definition.definition ||
            callable.receiver != zc::none || callable.raises != zc::none ||
            !sameSpan(signature.declarationSpan, definition.source)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const ast::NodeId parameterListNode(
            tree.node(definition.node).payload.words[ast::kFunctionDeclParamsIdWord]);
        if (!tree.contains(parameterListNode) ||
            tree.node(parameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& parameterList = tree.node(parameterListNode);
        const ast::NodeList parameterNodes{
            parameterList.payload.words[ast::kFunctionParameterListParamsFirstWord],
            parameterList.payload.words[ast::kFunctionParameterListParamsSizeWord]};
        if (!tree.contains(parameterNodes) || parameterNodes.size != callable.parameters.size()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        zc::Vector<HirParameter> parameters(callable.parameters.size());
        for (size_t index = 0; index < callable.parameters.size(); ++index) {
          const auto parameterNode = tree.list(parameterNodes)[index];
          const auto& parameter = callable.parameters[index];
          if (!tree.contains(parameterNode) ||
              tree.node(parameterNode).kind != ast::SyntaxKind::FunctionParameterDecl ||
              parameter.hasDefault || !typeExists(parameter.type, checkedModule.semanticTypes())) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto parameterSpan = bound.parsedModule().spanFor(tree.node(parameterNode).range);
          if (parameterSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(span, parameterSpan) {
            parameters.add(HirParameter{parameter.parameter.clone(), parameter.type, span.clone()});
          }
        }
        if (!typeExists(callable.success, checkedModule.semanticTypes())) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        if (isExistentialSemanticType(checkedModule.semanticTypes(), callable.success)) {
          return rejectHirCapability<HirModuleCandidate>(
              definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
              definition.source.clone());
        }
        size_t returnTypeSlot = 0;
        size_t returnLiteralSlot = 0;
        ZC_IF_SOME(index, returnTypeIndex) { returnTypeSlot = index; }
        ZC_IF_SOME(index, returnLiteralIndex) { returnLiteralSlot = index; }
        const auto returnType = facts.nodeTypes().entries()[returnTypeSlot].value;
        const auto& returnLiteral = facts.literals().entries()[returnLiteralSlot].value;
        if (returnType != callable.success) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto conditionParameter = resolvedCallableParameter(bound.bindings(), shape.loopCondition);
        if (conditionParameter == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        identity::CallableParameterId conditionHandle;
        ZC_IF_SOME(value, conditionParameter) { conditionHandle = value; }
        auto conditionAuthority = registries.callableParameter(conditionHandle);
        if (conditionAuthority == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        HirVisibility visibilityValue = HirVisibility::external();
        HirLinkage linkageValue = HirLinkage::Internal;
        ZC_IF_SOME(value, functionVisibility) { visibilityValue = zc::mv(value); }
        ZC_IF_SOME(value, functionLinkage) { linkageValue = value; }
        identity::SourceSpan bodySpanValue = definition.source.clone();
        identity::SourceSpan returnSpanValue = definition.source.clone();
        identity::SourceSpan valueSpanValue = definition.source.clone();
        ZC_IF_SOME(value, bodySpan) { bodySpanValue = value.clone(); }
        ZC_IF_SOME(value, returnSpan) { returnSpanValue = value.clone(); }
        ZC_IF_SOME(value, valueSpan) { valueSpanValue = value.clone(); }
        zc::Array<uint8_t> orderingKey = definition.key.encode();
        size_t conditionTypeSlot = 0;
        ZC_IF_SOME(index, conditionTypeIndex) { conditionTypeSlot = index; }
        const auto conditionType = facts.nodeTypes().entries()[conditionTypeSlot].value;
        ZC_IF_SOME(entry, conditionAuthority) {
          auto conditionRef = HirParameterReferenceExpression{
              HirNodeId(), entry.key().clone(), conditionType, HirValueCategory::Place,
              ZC_ASSERT_NONNULL(conditionSpan).clone()};
          auto loopReturn = PendingLoopReturn{zc::mv(conditionRef),
                                              returnLiteral.literal.clone(),
                                              returnType,
                                              ZC_ASSERT_NONNULL(loopSpan).clone(),
                                              valueSpanValue.clone(),
                                              shape.loopStatement};
          pendingFunctions.add(PendingFunctionDeclaration{
              definition.definition, definition.node, callable.success, zc::mv(parameters),
              zc::none, zc::mv(visibilityValue), linkageValue, definition.source.clone(),
              bodySpanValue.clone(), returnSpanValue.clone(), valueSpanValue.clone(),
              zc::mv(orderingKey), shape.body, shape.returnStatement, shape.value, zc::none,
              LoopReturn{zc::mv(loopReturn)}});
        }
        continue;
      }
      if (shape.returnsComparison) {
        // Comparison-return shape: `return <a CMP b>`. The comparison result is
        // the function's bool return value. This consumes the same checked
        // relational call fact as the equality-conditional condition, but the
        // result flows into the Return terminator rather than a SwitchInt.
        auto nodeTypeIndex = factIndex(facts.nodeTypes(), shape.value);
        auto leftTypeIndex = factIndex(facts.nodeTypes(), shape.comparisonLeft);
        auto rightTypeIndex = factIndex(facts.nodeTypes(), shape.comparisonRight);
        auto callIndex = factIndex(facts.calls(), shape.value);
        auto bodySpan = bound.parsedModule().spanFor(tree.node(shape.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(shape.returnStatement).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(shape.value).range);
        auto leftSpan = bound.parsedModule().spanFor(tree.node(shape.comparisonLeft).range);
        auto rightSpan = bound.parsedModule().spanFor(tree.node(shape.comparisonRight).range);
        if (nodeTypeIndex == zc::none || leftTypeIndex == zc::none || rightTypeIndex == zc::none ||
            callIndex == zc::none || bodySpan == zc::none || returnSpan == zc::none ||
            valueSpan == zc::none || leftSpan == zc::none || rightSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const auto& signature = *signaturePtr;
        checker::signature::MemberSignatureScope comparisonMemberScopeValue;
        zc::Maybe<checker::signature::MemberSignatureScope> comparisonMemberScope;
        if (isMethod) {
          if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
              !signature.scope.variant().is<checker::signature::MemberSignatureScope>()) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          comparisonMemberScope =
              signature.scope.variant().get<checker::signature::MemberSignatureScope>();
          comparisonMemberScopeValue = ZC_ASSERT_NONNULL(comparisonMemberScope);
        } else if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
                   !signature.scope.variant()
                        .is<checker::signature::ModuleDefinitionSignatureScope>()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        zc::Maybe<HirVisibility> functionVisibility;
        if (isMethod) {
          functionVisibility = memberVisibility(comparisonMemberScopeValue.visibility, module);
        } else {
          const auto& root = signatures.roots[rootSlot];
          functionVisibility = visibility(root.visibility);
        }
        auto functionLinkage = linkage(callable);
        size_t nodeTypeSlot = 0;
        ZC_IF_SOME(index, nodeTypeIndex) { nodeTypeSlot = index; }
        const auto resultType = facts.nodeTypes().entries()[nodeTypeSlot].value;
        const bool comparisonModuleMembershipValid =
            isMethod ? definitionBelongsToModule(definition, bound.definitions())
                     : (signatures.roots[rootSlot].sourceModule == module &&
                        signatures.roots[rootSlot].canonicalDefinition == definition.definition);
        if (functionVisibility == zc::none || functionLinkage == zc::none ||
            signature.definitionKind != (isMethod ? identity::DefinitionKind::Method
                                                  : identity::DefinitionKind::Function) ||
            !comparisonModuleMembershipValid || callable.raises != zc::none ||
            (!isMethod && callable.receiver != zc::none) ||
            (isMethod && callable.receiver == zc::none) || callable.success != resultType ||
            !sameSpan(signature.declarationSpan, definition.source)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const ast::NodeId parameterListNode(
            tree.node(definition.node)
                .payload
                .words[isMethod ? ast::kMethodDeclParamsIdWord : ast::kFunctionDeclParamsIdWord]);
        if (!tree.contains(parameterListNode) ||
            tree.node(parameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& parameterList = tree.node(parameterListNode);
        const ast::NodeList parameterNodes{
            parameterList.payload.words[ast::kFunctionParameterListParamsFirstWord],
            parameterList.payload.words[ast::kFunctionParameterListParamsSizeWord]};
        // A method's AST parameter list leads with the implicit `this`
        // receiver, which is carried separately and never enters `parameters`.
        if (!tree.contains(parameterNodes) || parameterNodes.size < callable.parameters.size()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const size_t comparisonReceiverCount = parameterNodes.size - callable.parameters.size();
        if ((!isMethod && comparisonReceiverCount != 0) ||
            (isMethod && comparisonReceiverCount > 1)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        zc::Vector<HirParameter> parameters(callable.parameters.size());
        for (size_t index = 0; index < callable.parameters.size(); ++index) {
          const auto parameterNode = tree.list(parameterNodes)[comparisonReceiverCount + index];
          const auto& parameter = callable.parameters[index];
          if (!tree.contains(parameterNode) ||
              tree.node(parameterNode).kind != ast::SyntaxKind::FunctionParameterDecl ||
              parameter.hasDefault || !typeExists(parameter.type, checkedModule.semanticTypes())) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto parameterSpan = bound.parsedModule().spanFor(tree.node(parameterNode).range);
          if (parameterSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(span, parameterSpan) {
            parameters.add(HirParameter{parameter.parameter.clone(), parameter.type, span.clone()});
          }
        }
        zc::Maybe<HirParameter> comparisonReceiver;
        if (isMethod) {
          ZC_IF_SOME(receiver, callable.receiver) {
            if (comparisonMemberScope == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto owner = ZC_ASSERT_NONNULL(comparisonMemberScope).owner;
            if (owner == definition.definition) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            auto built = buildMethodReceiverParameter(definition, owner, receiver, bound,
                                                      registries, semanticTypes);
            if (built == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            comparisonReceiver = zc::mv(built);
          }
          if (comparisonReceiver == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
        }
        if (!typeExists(callable.success, checkedModule.semanticTypes()) ||
            (comparisonReceiver != zc::none &&
             !typeExists(ZC_ASSERT_NONNULL(comparisonReceiver).type,
                         checkedModule.semanticTypes()))) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        if (isExistentialSemanticType(checkedModule.semanticTypes(), callable.success)) {
          return rejectHirCapability<HirModuleCandidate>(
              definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
              definition.source.clone());
        }
        size_t leftTypeSlot = 0;
        size_t rightTypeSlot = 0;
        size_t callSlot = 0;
        ZC_IF_SOME(index, leftTypeIndex) { leftTypeSlot = index; }
        ZC_IF_SOME(index, rightTypeIndex) { rightTypeSlot = index; }
        ZC_IF_SOME(index, callIndex) { callSlot = index; }
        const auto operandType = facts.nodeTypes().entries()[leftTypeSlot].value;
        const auto rightType = facts.nodeTypes().entries()[rightTypeSlot].value;
        const auto& callFact = facts.calls().entries()[callSlot].value;
        const auto& call = callFact.invocation;
        const auto& selected = call.selected.variant();
        // The call fact must match the primitive-binary contract exactly for one
        // of the supported operators. A comparison produces the bool return type;
        // an arithmetic/bitwise operator produces the operand type, so for
        // arithmetic the function return type (resultType) equals operandType.
        const auto selectedOperation =
            selected.is<checker::checked::PrimitiveCallable>()
                ? zc::Maybe<checker::PrimitiveOperation>(
                      selected.get<checker::checked::PrimitiveCallable>().operation)
                : zc::Maybe<checker::PrimitiveOperation>(zc::none);
        bool operationSupported = false;
        ZC_IF_SOME(op, selectedOperation) {
          operationSupported = isScalarComparisonOperation(op) ||
                               (isScalarArithmeticOperation(op) && resultType == operandType);
        }
        if (operandType != rightType || callFact.node != shape.value || !operationSupported ||
            call.calleeType != operandType || call.receiver != zc::none ||
            call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
            call.arguments.size() != 2 || call.arguments[0].sourceNode != shape.comparisonLeft ||
            call.arguments[0].sourceType != operandType ||
            call.arguments[1].sourceNode != shape.comparisonRight ||
            call.arguments[1].sourceType != operandType || call.successType != resultType ||
            call.resultType != resultType || call.substitutions != zc::none ||
            call.witnesses != zc::none || call.raises != zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        HirVisibility visibilityValue = HirVisibility::external();
        HirLinkage linkageValue = HirLinkage::Internal;
        ZC_IF_SOME(value, functionVisibility) { visibilityValue = zc::mv(value); }
        ZC_IF_SOME(value, functionLinkage) { linkageValue = value; }
        identity::SourceSpan bodySpanValue = definition.source.clone();
        identity::SourceSpan returnSpanValue = definition.source.clone();
        identity::SourceSpan valueSpanValue = definition.source.clone();
        ZC_IF_SOME(value, bodySpan) { bodySpanValue = value.clone(); }
        ZC_IF_SOME(value, returnSpan) { returnSpanValue = value.clone(); }
        ZC_IF_SOME(value, valueSpan) { valueSpanValue = value.clone(); }
        zc::Array<uint8_t> orderingKey = definition.key.encode();
        // Build one comparison operand: a parameter operand resolves to a
        // parameter reference; a literal operand consumes its checked literal
        // fact. Both operands share the derived operand type.
        bool operandRejected = false;
        auto buildOperand =
            [&](ast::NodeId operandNode, bool isLiteral,
                const identity::SourceSpan& operandSpan) -> zc::Maybe<PendingConditionalArm> {
          if (!isLiteral) {
            auto parameter = resolvedCallableParameter(bound.bindings(), operandNode);
            if (parameter == zc::none) {
              operandRejected = true;
              return zc::none;
            }
            identity::CallableParameterId handle;
            ZC_IF_SOME(value, parameter) { handle = value; }
            auto authority = registries.callableParameter(handle);
            if (authority == zc::none) {
              operandRejected = true;
              return zc::none;
            }
            zc::Maybe<PendingConditionalArm> built;
            ZC_IF_SOME(entry, authority) {
              bool matches = false;
              for (const auto& parameterCandidate : parameters) {
                if (parameterCandidate.key == entry.key() &&
                    parameterCandidate.type == operandType) {
                  matches = true;
                }
              }
              if (!matches) {
                operandRejected = true;
              } else {
                auto reference =
                    HirParameterReferenceExpression{HirNodeId(), entry.key().clone(), operandType,
                                                    HirValueCategory::Place, operandSpan.clone()};
                built = PendingConditionalArm{zc::none,    zc::mv(reference),   zc::none,
                                              operandType, operandSpan.clone(), zc::none,
                                              operandNode};
              }
            }
            return built;
          }
          auto literalIndex = factIndex(facts.literals(), operandNode);
          if (literalIndex == zc::none) {
            operandRejected = true;
            return zc::none;
          }
          size_t literalSlot = 0;
          ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
          const auto& literalFact = facts.literals().entries()[literalSlot].value;
          return PendingConditionalArm{
              literalFact.literal.clone(), zc::none, zc::none,   operandType,
              operandSpan.clone(),         zc::none, operandNode};
        };
        auto leftOperand = buildOperand(shape.comparisonLeft, shape.comparisonLeftIsLiteral,
                                        ZC_ASSERT_NONNULL(leftSpan));
        auto rightOperand = buildOperand(shape.comparisonRight, shape.comparisonRightIsLiteral,
                                         ZC_ASSERT_NONNULL(rightSpan));
        if (operandRejected || leftOperand == zc::none || rightOperand == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto comparisonReturn =
            PendingEqualityCondition{zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                     zc::mv(ZC_ASSERT_NONNULL(rightOperand)),
                                     operandType,
                                     resultType,
                                     selected.get<checker::checked::PrimitiveCallable>().operation,
                                     ZC_ASSERT_NONNULL(valueSpan).clone(),
                                     false,
                                     shape.condition};
        pendingFunctions.add(PendingFunctionDeclaration{
            definition.definition, definition.node, callable.success, zc::mv(parameters),
            zc::mv(comparisonReceiver), zc::mv(visibilityValue), linkageValue,
            definition.source.clone(), bodySpanValue.clone(), returnSpanValue.clone(),
            valueSpanValue.clone(), zc::mv(orderingKey), shape.body, shape.returnStatement,
            shape.value, zc::none, ComparisonReturn{zc::mv(comparisonReturn)}});
        continue;
      }
      if (shape.returnsUnary) {
        // Unary-return shape: `return <op x>`. The unary operation is desugared
        // to an equivalent binary operation with a synthetic constant operand,
        // reusing the comparison-return materialization path:
        //   Neg        -> Sub(0, x)
        //   UnaryPlus  -> Add(x, 0)
        //   BitNot     -> BitXor(x, -1)
        //   LogicalNot -> Eq(x, false)
        // The synthetic operand has no AST node; its constant is constructed
        // from the operand type and the desugared operation. The real operand
        // must be a parameter reference so the MIR builder derives the shared
        // operand type from it.
        auto nodeTypeIndex = factIndex(facts.nodeTypes(), shape.value);
        auto operandTypeIndex = factIndex(facts.nodeTypes(), shape.unaryOperand);
        auto callIndex = factIndex(facts.calls(), shape.value);
        auto bodySpan = bound.parsedModule().spanFor(tree.node(shape.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(shape.returnStatement).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(shape.value).range);
        auto operandSpan = bound.parsedModule().spanFor(tree.node(shape.unaryOperand).range);
        if (nodeTypeIndex == zc::none || operandTypeIndex == zc::none || callIndex == zc::none ||
            bodySpan == zc::none || returnSpan == zc::none || valueSpan == zc::none ||
            operandSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const auto& signature = *signaturePtr;
        checker::signature::MemberSignatureScope unaryMemberScopeValue;
        zc::Maybe<checker::signature::MemberSignatureScope> unaryMemberScope;
        if (isMethod) {
          if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
              !signature.scope.variant().is<checker::signature::MemberSignatureScope>()) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          unaryMemberScope =
              signature.scope.variant().get<checker::signature::MemberSignatureScope>();
          unaryMemberScopeValue = ZC_ASSERT_NONNULL(unaryMemberScope);
        } else if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
                   !signature.scope.variant()
                        .is<checker::signature::ModuleDefinitionSignatureScope>()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        zc::Maybe<HirVisibility> functionVisibility;
        if (isMethod) {
          functionVisibility = memberVisibility(unaryMemberScopeValue.visibility, module);
        } else {
          const auto& root = signatures.roots[rootSlot];
          functionVisibility = visibility(root.visibility);
        }
        auto functionLinkage = linkage(callable);
        size_t nodeTypeSlot = 0;
        ZC_IF_SOME(index, nodeTypeIndex) { nodeTypeSlot = index; }
        const auto resultType = facts.nodeTypes().entries()[nodeTypeSlot].value;
        const bool unaryModuleMembershipValid =
            isMethod ? definitionBelongsToModule(definition, bound.definitions())
                     : (signatures.roots[rootSlot].sourceModule == module &&
                        signatures.roots[rootSlot].canonicalDefinition == definition.definition);
        if (functionVisibility == zc::none || functionLinkage == zc::none ||
            signature.definitionKind != (isMethod ? identity::DefinitionKind::Method
                                                  : identity::DefinitionKind::Function) ||
            !unaryModuleMembershipValid || callable.raises != zc::none ||
            (!isMethod && callable.receiver != zc::none) ||
            (isMethod && callable.receiver == zc::none) || callable.success != resultType ||
            !sameSpan(signature.declarationSpan, definition.source)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const ast::NodeId parameterListNode(
            tree.node(definition.node)
                .payload
                .words[isMethod ? ast::kMethodDeclParamsIdWord : ast::kFunctionDeclParamsIdWord]);
        if (!tree.contains(parameterListNode) ||
            tree.node(parameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& parameterList = tree.node(parameterListNode);
        const ast::NodeList parameterNodes{
            parameterList.payload.words[ast::kFunctionParameterListParamsFirstWord],
            parameterList.payload.words[ast::kFunctionParameterListParamsSizeWord]};
        if (!tree.contains(parameterNodes) || parameterNodes.size < callable.parameters.size()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const size_t unaryReceiverCount = parameterNodes.size - callable.parameters.size();
        if ((!isMethod && unaryReceiverCount != 0) || (isMethod && unaryReceiverCount > 1)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        zc::Vector<HirParameter> parameters(callable.parameters.size());
        for (size_t index = 0; index < callable.parameters.size(); ++index) {
          const auto parameterNode = tree.list(parameterNodes)[unaryReceiverCount + index];
          const auto& parameter = callable.parameters[index];
          if (!tree.contains(parameterNode) ||
              tree.node(parameterNode).kind != ast::SyntaxKind::FunctionParameterDecl ||
              parameter.hasDefault || !typeExists(parameter.type, checkedModule.semanticTypes())) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto parameterSpan = bound.parsedModule().spanFor(tree.node(parameterNode).range);
          if (parameterSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(span, parameterSpan) {
            parameters.add(HirParameter{parameter.parameter.clone(), parameter.type, span.clone()});
          }
        }
        zc::Maybe<HirParameter> unaryReceiver;
        if (isMethod) {
          ZC_IF_SOME(receiver, callable.receiver) {
            if (unaryMemberScope == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto owner = ZC_ASSERT_NONNULL(unaryMemberScope).owner;
            if (owner == definition.definition) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            auto built = buildMethodReceiverParameter(definition, owner, receiver, bound,
                                                      registries, semanticTypes);
            if (built == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            unaryReceiver = zc::mv(built);
          }
          if (unaryReceiver == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
        }
        if (!typeExists(callable.success, checkedModule.semanticTypes()) ||
            (unaryReceiver != zc::none &&
             !typeExists(ZC_ASSERT_NONNULL(unaryReceiver).type, checkedModule.semanticTypes()))) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        if (isExistentialSemanticType(checkedModule.semanticTypes(), callable.success)) {
          return rejectHirCapability<HirModuleCandidate>(
              definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
              definition.source.clone());
        }
        size_t operandTypeSlot = 0;
        size_t callSlot = 0;
        ZC_IF_SOME(index, operandTypeIndex) { operandTypeSlot = index; }
        ZC_IF_SOME(index, callIndex) { callSlot = index; }
        const auto operandType = facts.nodeTypes().entries()[operandTypeSlot].value;
        const auto& callFact = facts.calls().entries()[callSlot].value;
        const auto& call = callFact.invocation;
        const auto& selected = call.selected.variant();
        // The call fact must match the primitive-unary contract exactly: one
        // argument (the operand), a unary primitive callable, and the result
        // type derived from the operation (bool for LogicalNot, operand type
        // otherwise).
        const auto selectedOperation =
            selected.is<checker::checked::PrimitiveCallable>()
                ? zc::Maybe<checker::PrimitiveOperation>(
                      selected.get<checker::checked::PrimitiveCallable>().operation)
                : zc::Maybe<checker::PrimitiveOperation>(zc::none);
        bool operationSupported = false;
        ZC_IF_SOME(op, selectedOperation) {
          operationSupported = isScalarUnaryOperation(op) && callable.success == resultType;
        }
        if (callFact.node != shape.value || !operationSupported || call.calleeType != operandType ||
            call.receiver != zc::none || call.receiverMode != zc::none ||
            call.receiverAdjustment != zc::none || call.arguments.size() != 1 ||
            call.arguments[0].sourceNode != shape.unaryOperand ||
            call.arguments[0].sourceType != operandType || call.successType != resultType ||
            call.resultType != resultType || call.substitutions != zc::none ||
            call.witnesses != zc::none || call.raises != zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        // Desugar the unary operation to an equivalent binary operation. The
        // synthetic constant operand is constructed from the operand type.
        const auto unaryOperation = ZC_ASSERT_NONNULL(selectedOperation);
        checker::PrimitiveOperation binaryOperation;
        bool syntheticOnLeft = false;
        switch (unaryOperation) {
          case checker::PrimitiveOperation::Neg:
            // -x == 0 - x: synthetic zero on the left.
            binaryOperation = checker::PrimitiveOperation::Sub;
            syntheticOnLeft = true;
            break;
          case checker::PrimitiveOperation::UnaryPlus:
            // +x == x + 0: synthetic zero on the right.
            binaryOperation = checker::PrimitiveOperation::Add;
            break;
          case checker::PrimitiveOperation::BitNot:
            // ~x == x ^ -1: synthetic -1 on the right.
            binaryOperation = checker::PrimitiveOperation::BitXor;
            break;
          case checker::PrimitiveOperation::LogicalNot:
            // !x == x == false: synthetic false on the right.
            binaryOperation = checker::PrimitiveOperation::Eq;
            break;
          default:
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
        }
        auto operandKind = primitiveKindOf(checkedModule.semanticTypes(), operandType);
        if (operandKind == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto kind = ZC_ASSERT_NONNULL(operandKind);
        zc::Maybe<checker::checked::CanonicalConstValue> syntheticValue;
        switch (unaryOperation) {
          case checker::PrimitiveOperation::Neg:
          case checker::PrimitiveOperation::UnaryPlus: {
            // Zero of the operand type: integer zero or float zero.
            if (kind == type::semantic::PrimitiveKind::F32) {
              syntheticValue = checker::checked::CanonicalConstValue::float32(0);
            } else if (kind == type::semantic::PrimitiveKind::F64) {
              syntheticValue = checker::checked::CanonicalConstValue::float64(0);
            } else {
              syntheticValue = checker::checked::CanonicalConstValue::integer(
                  checker::signature::CanonicalInteger{checker::signature::IntegerSign::NonNegative,
                                                       zc::Array<uint8_t>{}});
            }
            break;
          }
          case checker::PrimitiveOperation::BitNot: {
            // -1: Negative sign with magnitude {1}.
            auto magnitude = zc::heapArray<uint8_t>(1);
            magnitude[0] = 1;
            syntheticValue =
                checker::checked::CanonicalConstValue::integer(checker::signature::CanonicalInteger{
                    checker::signature::IntegerSign::Negative, zc::mv(magnitude)});
            break;
          }
          case checker::PrimitiveOperation::LogicalNot:
            syntheticValue = checker::checked::CanonicalConstValue::boolean(false);
            break;
          default:
            break;
        }
        if (syntheticValue == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        HirVisibility visibilityValue = HirVisibility::external();
        HirLinkage linkageValue = HirLinkage::Internal;
        ZC_IF_SOME(value, functionVisibility) { visibilityValue = zc::mv(value); }
        ZC_IF_SOME(value, functionLinkage) { linkageValue = value; }
        identity::SourceSpan bodySpanValue = definition.source.clone();
        identity::SourceSpan returnSpanValue = definition.source.clone();
        identity::SourceSpan valueSpanValue = definition.source.clone();
        ZC_IF_SOME(value, bodySpan) { bodySpanValue = value.clone(); }
        ZC_IF_SOME(value, returnSpan) { returnSpanValue = value.clone(); }
        ZC_IF_SOME(value, valueSpan) { valueSpanValue = value.clone(); }
        zc::Array<uint8_t> orderingKey = definition.key.encode();
        // Build the real operand: a parameter reference. A literal operand is
        // rejected because the MIR builder derives the shared operand type
        // from a parameter operand.
        if (shape.unaryOperandIsLiteral) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto parameter = resolvedCallableParameter(bound.bindings(), shape.unaryOperand);
        if (parameter == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        identity::CallableParameterId handle;
        ZC_IF_SOME(value, parameter) { handle = value; }
        auto authority = registries.callableParameter(handle);
        if (authority == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        zc::Maybe<PendingConditionalArm> realOperand;
        ZC_IF_SOME(entry, authority) {
          bool matches = false;
          for (const auto& parameterCandidate : parameters) {
            if (parameterCandidate.key == entry.key() && parameterCandidate.type == operandType) {
              matches = true;
            }
          }
          if (!matches) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto reference = HirParameterReferenceExpression{HirNodeId(), entry.key().clone(),
                                                           operandType, HirValueCategory::Place,
                                                           ZC_ASSERT_NONNULL(operandSpan).clone()};
          realOperand = PendingConditionalArm{zc::none,
                                              zc::mv(reference),
                                              zc::none,
                                              operandType,
                                              ZC_ASSERT_NONNULL(operandSpan).clone(),
                                              zc::none,
                                              shape.unaryOperand};
        }
        if (realOperand == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto syntheticOperand =
            PendingConditionalArm{zc::mv(ZC_ASSERT_NONNULL(syntheticValue)), zc::none, zc::none,
                                  operandType, ZC_ASSERT_NONNULL(valueSpan).clone()};
        auto comparisonReturn =
            syntheticOnLeft ? PendingEqualityCondition{zc::mv(syntheticOperand),
                                                       zc::mv(ZC_ASSERT_NONNULL(realOperand)),
                                                       operandType,
                                                       resultType,
                                                       binaryOperation,
                                                       ZC_ASSERT_NONNULL(valueSpan).clone(),
                                                       true}
                            : PendingEqualityCondition{zc::mv(ZC_ASSERT_NONNULL(realOperand)),
                                                       zc::mv(syntheticOperand),
                                                       operandType,
                                                       resultType,
                                                       binaryOperation,
                                                       ZC_ASSERT_NONNULL(valueSpan).clone(),
                                                       true};
        pendingFunctions.add(PendingFunctionDeclaration{
            definition.definition, definition.node, callable.success, zc::mv(parameters),
            zc::mv(unaryReceiver), zc::mv(visibilityValue), linkageValue, definition.source.clone(),
            bodySpanValue.clone(), returnSpanValue.clone(), valueSpanValue.clone(),
            zc::mv(orderingKey), shape.body, shape.returnStatement, shape.value, zc::none,
            ComparisonReturn{zc::mv(comparisonReturn)}});
        continue;
      }
      auto nodeTypeIndex = factIndex(facts.nodeTypes(), shape.value);
      auto bodySpan = bound.parsedModule().spanFor(tree.node(shape.body).range);
      auto returnSpan = shape.isVoidBody
                            ? zc::Maybe<identity::SourceSpan>(zc::none)
                            : bound.parsedModule().spanFor(tree.node(shape.returnStatement).range);
      auto valueSpan = shape.isVoidBody
                           ? zc::Maybe<identity::SourceSpan>(zc::none)
                           : bound.parsedModule().spanFor(tree.node(shape.value).range);
      if ((!shape.isVoidBody &&
           (nodeTypeIndex == zc::none || returnSpan == zc::none || valueSpan == zc::none)) ||
          bodySpan == zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      zc::Maybe<identity::SourceSpan> unsafeBlockSpan;
      ZC_IF_SOME(unsafeNode, shape.unsafeBlock) {
        auto unsafeSpan = bound.parsedModule().spanFor(tree.node(unsafeNode).range);
        if (unsafeSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        ZC_IF_SOME(span, unsafeSpan) { unsafeBlockSpan = span.clone(); }
      }
      size_t nodeTypeSlot = 0;
      ZC_IF_SOME(index, nodeTypeIndex) { nodeTypeSlot = index; }
      const auto& signature = *signaturePtr;
      // A void body consumes no return-value node-type fact and continues on
      // its own branch before any read of this entry; non-void paths index the
      // verified return-value fact below.
      const auto& nodeType = facts.nodeTypes().entries()[nodeTypeSlot];
      if (!signature.payload.variant().is<checker::signature::CallableSignature>()) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      zc::Maybe<identity::DefId> methodOwner;
      zc::Maybe<HirVisibility> methodScopeVisibility;
      if (isMethod) {
        if (signature.scope.variant().is<checker::signature::MemberSignatureScope>()) {
          const auto& scope =
              signature.scope.variant().get<checker::signature::MemberSignatureScope>();
          methodOwner = scope.owner;
          methodScopeVisibility = memberVisibility(scope.visibility, module);
        } else if (signature.scope.variant().is<checker::signature::EnclosedSignatureScope>()) {
          methodOwner =
              signature.scope.variant().get<checker::signature::EnclosedSignatureScope>().owner;
          methodScopeVisibility = HirVisibility::external();
        } else if (signature.scope.variant()
                       .is<checker::signature::ModuleDefinitionSignatureScope>()) {
          // Impl methods in standalone impl blocks get a
          // ModuleDefinitionSignatureScope from the signature facts because
          // their enclosing owner is an impl, not a definition. Resolve the
          // concrete self-type DefId from the verified impl heads.
          methodOwner = implMethodOwner(definition, registries,
                                        checkedModule.localSignatureFacts().implHeads());
          if (methodOwner == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          methodScopeVisibility = HirVisibility::external();
        } else {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
      } else if (!signature.scope.variant()
                      .is<checker::signature::ModuleDefinitionSignatureScope>()) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      const auto& callable =
          signature.payload.variant().get<checker::signature::CallableSignature>();
      // An ordinary function has no receiver; an inherent method must have one.
      if (callable.receiver == zc::none) {
        if (isMethod) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
      } else if (!isMethod) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      zc::Maybe<HirVisibility> functionVisibility;
      if (isMethod) {
        ZC_IF_SOME(vis, methodScopeVisibility) { functionVisibility = zc::mv(vis); }
      } else {
        const auto& root = signatures.roots[rootSlot];
        functionVisibility = visibility(root.visibility);
      }
      auto functionLinkage = linkage(callable);
      const bool moduleMembershipValid =
          isMethod ? definitionBelongsToModule(definition, bound.definitions())
                   : (signatures.roots[rootSlot].sourceModule == module &&
                      signatures.roots[rootSlot].canonicalDefinition == definition.definition);
      // A void body structurally matches a resultless field-write method but
      // declares a non-Unit result: the lowering does not admit that shape, so
      // drain it as the ZOM4099 capability code rather than an invalid-fact
      // invariant.
      if (shape.isVoidBody &&
          !isUnitSemanticType(checkedModule.semanticTypes(), callable.success)) {
        return rejectHirCapability<HirModuleCandidate>(
            definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
            definition.source.clone());
      }
      // For an error-operator return (`return f()?!;` or `return f()!!;`),
      // the return-value node is the operand call whose type is the error
      // union, not the success type. The call's successType is verified
      // against callable.success in the direct-call assembly path below.
      const bool successTypeValid = shape.isVoidBody || callable.success == nodeType.value ||
                                    shape.returnsErrorPropagateCall ||
                                    shape.returnsErrorUnwrapCall ||
                                    shape.returnsAggregateConstruction;
      if (functionVisibility == zc::none || functionLinkage == zc::none ||
          signature.definitionKind !=
              (isMethod ? identity::DefinitionKind::Method : identity::DefinitionKind::Function) ||
          !moduleMembershipValid || !successTypeValid ||
          !sameSpan(signature.declarationSpan, definition.source)) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      const auto paramsWord =
          isMethod ? ast::kMethodDeclParamsIdWord : ast::kFunctionDeclParamsIdWord;
      const ast::NodeId parameterListNode(tree.node(definition.node).payload.words[paramsWord]);
      if (!tree.contains(parameterListNode) ||
          tree.node(parameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      const auto& parameterList = tree.node(parameterListNode);
      const ast::NodeList parameterNodes{
          parameterList.payload.words[ast::kFunctionParameterListParamsFirstWord],
          parameterList.payload.words[ast::kFunctionParameterListParamsSizeWord]};
      // A method's AST parameter list may lead with the implicit `this`
      // receiver, which is carried separately and never enters `parameters`.
      if (!tree.contains(parameterNodes) || parameterNodes.size < callable.parameters.size()) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      const size_t receiverCount = parameterNodes.size - callable.parameters.size();
      if ((!isMethod && receiverCount != 0) || (isMethod && receiverCount > 1)) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      zc::Vector<HirParameter> parameters(callable.parameters.size());
      for (size_t index = 0; index < callable.parameters.size(); ++index) {
        const auto parameterNode = tree.list(parameterNodes)[receiverCount + index];
        const auto& parameter = callable.parameters[index];
        if (!tree.contains(parameterNode) ||
            tree.node(parameterNode).kind != ast::SyntaxKind::FunctionParameterDecl ||
            parameter.hasDefault || !typeExists(parameter.type, checkedModule.semanticTypes())) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto parameterSpan = bound.parsedModule().spanFor(tree.node(parameterNode).range);
        if (parameterSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        ZC_IF_SOME(span, parameterSpan) {
          parameters.add(HirParameter{parameter.parameter.clone(), parameter.type, span.clone()});
        }
      }
      zc::Maybe<HirParameter> methodReceiver;
      if (isMethod) {
        ZC_IF_SOME(receiver, callable.receiver) {
          if (methodOwner == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          const auto owner = ZC_ASSERT_NONNULL(methodOwner);
          if (owner == definition.definition) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto built = buildMethodReceiverParameter(definition, owner, receiver, bound, registries,
                                                    semanticTypes);
          if (built == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          methodReceiver = zc::mv(built);
        }
        if (methodReceiver == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
      }
      if (!typeExists(callable.success, checkedModule.semanticTypes()) ||
          (methodReceiver != zc::none &&
           !typeExists(ZC_ASSERT_NONNULL(methodReceiver).type, checkedModule.semanticTypes()))) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      if (isExistentialSemanticType(checkedModule.semanticTypes(), callable.success) &&
          !shape.returnsAggregateConstruction) {
        return rejectHirCapability<HirModuleCandidate>(
            definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
            definition.source.clone());
      }
      HirVisibility visibilityValue = HirVisibility::external();
      HirLinkage linkageValue = HirLinkage::Internal;
      identity::SourceSpan bodySpanValue = definition.source.clone();
      identity::SourceSpan returnSpanValue = definition.source.clone();
      identity::SourceSpan valueSpanValue = definition.source.clone();
      zc::Array<uint8_t> orderingKey;
      ZC_IF_SOME(value, functionVisibility) { visibilityValue = zc::mv(value); }
      ZC_IF_SOME(value, functionLinkage) { linkageValue = value; }
      ZC_IF_SOME(value, bodySpan) { bodySpanValue = value.clone(); }
      ZC_IF_SOME(value, returnSpan) { returnSpanValue = value.clone(); }
      ZC_IF_SOME(value, valueSpan) { valueSpanValue = value.clone(); }
      orderingKey = definition.key.encode();
      if (shape.isVoidBody) {
        // Void mutating-method shape: the sole statement is
        // `this.<field> = <ordinary-parameter>;` and the result is Unit. There
        // is no return value node; the write value names a pooled parameter
        // reference rather than a literal. Member, place, receiver, and
        // parameter facts are validated exactly like the write-read shape.
        if (methodReceiver == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& voidReceiver = ZC_ASSERT_NONNULL(methodReceiver);
        const ast::NodeId writeStatement = shape.receiverWriteStatement;
        const ast::NodeId writeAssignment = shape.receiverWriteAssignment;
        const ast::NodeId writeTarget(
            tree.node(writeAssignment).payload.words[ast::kAssignmentExprLhsWord]);
        const ast::NodeId writeRhs = shape.receiverWriteValue;
        const ast::NodeId writeObject(
            tree.node(writeTarget).payload.words[ast::kMemberExpressionObjectWord]);
        auto thisTypeIndex = factIndex(facts.nodeTypes(), writeObject);
        auto memberIndex = factIndex(facts.members(), writeTarget);
        auto placeIndex = factIndex(facts.places(), writeTarget);
        auto writeValueTypeIndex = factIndex(facts.nodeTypes(), writeRhs);
        auto writeStatementSpan = bound.parsedModule().spanFor(tree.node(writeStatement).range);
        auto writeValueSpan = bound.parsedModule().spanFor(tree.node(writeRhs).range);
        if (thisTypeIndex == zc::none || memberIndex == zc::none || placeIndex == zc::none ||
            writeValueTypeIndex == zc::none || writeStatementSpan == zc::none ||
            writeValueSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t thisTypeSlot = 0;
        size_t memberSlot = 0;
        size_t placeSlot = 0;
        size_t writeValueTypeSlot = 0;
        ZC_IF_SOME(index, thisTypeIndex) { thisTypeSlot = index; }
        ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
        ZC_IF_SOME(index, placeIndex) { placeSlot = index; }
        ZC_IF_SOME(index, writeValueTypeIndex) { writeValueTypeSlot = index; }
        const auto& voidMember = facts.members().entries()[memberSlot].value;
        const auto& voidPlace = facts.places().entries()[placeSlot].value;
        const auto& voidRoot = voidPlace.root.variant();
        if (voidMember.node != writeTarget ||
            facts.nodeTypes().entries()[thisTypeSlot].value != voidReceiver.type ||
            facts.nodeTypes().entries()[writeValueTypeSlot].value != voidMember.memberType ||
            voidPlace.type != voidMember.memberType || !voidPlace.movable ||
            !voidRoot.is<checker::checked::CallableParameterPlaceRoot>() ||
            voidPlace.projections.size() != 1 ||
            !voidPlace.projections[0].variant().is<checker::checked::FieldProjection>() ||
            voidPlace.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                voidMember.member ||
            !typeExists(voidMember.receiverType, checkedModule.semanticTypes())) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto voidRootAuthority = registries.callableParameter(
            voidRoot.get<checker::checked::CallableParameterPlaceRoot>().parameter);
        if (voidRootAuthority == zc::none ||
            ZC_ASSERT_NONNULL(voidRootAuthority).key() != voidReceiver.key) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto voidReceiverLookup = checkedModule.semanticTypes().get(voidReceiver.type);
        if (!voidReceiverLookup.is<type::SemanticTypeLookup>() ||
            !voidReceiverLookup.get<type::SemanticTypeLookup>()
                 .data()
                 .is<type::semantic::ReferenceTypeData>() ||
            voidReceiverLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .referent != voidMember.receiverType ||
            voidReceiverLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .mutability != type::semantic::Mutability::Mutable) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto writeParameter = resolvedCallableParameter(bound.bindings(), writeRhs);
        if (writeParameter == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        identity::CallableParameterId writeParameterHandle;
        ZC_IF_SOME(handle, writeParameter) { writeParameterHandle = handle; }
        auto writeParameterAuthority = registries.callableParameter(writeParameterHandle);
        if (writeParameterAuthority == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        zc::Maybe<HirParameterReferenceExpression> parameterFieldWriteParameter;
        ZC_IF_SOME(entry, writeParameterAuthority) {
          bool parameterMatches = false;
          for (const auto& candidate : parameters) {
            if (candidate.key == entry.key() && candidate.type == voidMember.memberType) {
              parameterMatches = true;
            }
          }
          if (!parameterMatches) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          parameterFieldWriteParameter = HirParameterReferenceExpression{
              HirNodeId(), entry.key().clone(), voidMember.memberType, HirValueCategory::Place,
              ZC_ASSERT_NONNULL(writeValueSpan).clone()};
        }
        zc::Maybe<HirParameterFieldWriteStatement> parameterFieldWrite =
            HirParameterFieldWriteStatement{HirNodeId(),
                                            voidReceiver.key.clone(),
                                            voidMember.receiverType,
                                            voidMember.member,
                                            voidMember.memberType,
                                            HirNodeId(),
                                            ZC_ASSERT_NONNULL(writeStatementSpan).clone(),
                                            ZC_ASSERT_NONNULL(writeValueSpan).clone()};
        pendingFunctions.add(PendingFunctionDeclaration{
            definition.definition, definition.node, callable.success, zc::mv(parameters),
            zc::mv(methodReceiver), zc::mv(visibilityValue), linkageValue,
            definition.source.clone(), bodySpanValue.clone(), returnSpanValue.clone(),
            valueSpanValue.clone(), zc::mv(orderingKey), shape.body, shape.returnStatement,
            shape.value, zc::mv(unsafeBlockSpan),
            VoidMutatingMethod{zc::mv(ZC_ASSERT_NONNULL(parameterFieldWrite)),
                               zc::mv(ZC_ASSERT_NONNULL(parameterFieldWriteParameter))}});
        continue;
      }
      zc::Maybe<checker::checked::CanonicalConstValue> literal;
      zc::Maybe<HirDirectCallExpression> call;
      zc::Maybe<HirReceiverCallExpression> receiverCall;
      zc::Maybe<identity::CallableParameterKey> receiverCallParameterKey;
      zc::Maybe<identity::SourceSpan> receiverCallParameterSpan;
      zc::Maybe<HirLocalBinding> local;
      zc::Maybe<HirNominalAggregateExpression> aggregate;
      zc::Maybe<HirDirectCallExpression> initializerCall;
      bool aggregateIsEnumConstruction = false;
      zc::Maybe<HirErasureRecord> returnErasure;
      zc::Vector<HirLocalWriteStatement> localWrites;
      zc::Vector<PendingLocalWriteValue> localWriteValues;
      zc::Maybe<HirLocalReferenceExpression> localReference;
      zc::Maybe<PendingEqualityCondition> comparisonReturn;
      zc::Maybe<HirLocalFieldProjectionExpression> localFieldProjection;
      zc::Maybe<HirParameterFieldProjectionExpression> parameterFieldProjection;
      bool byValueParameterField = false;
      zc::Maybe<HirParameterFieldWriteStatement> parameterFieldWrite;
      zc::Maybe<checker::checked::CanonicalConstValue> parameterFieldWriteLiteral;
      zc::Maybe<HirReceiverCallExpression> receiverSelfCall;
      zc::Maybe<PendingReceiverFieldArithmetic> receiverFieldArithmetic;
      zc::Maybe<HirReceiverCallExpression> statementReceiverCall;
      zc::Maybe<HirLocalReferenceExpression> statementReceiverReference;
      bool voidBody = false;
      bool returnsArrayIndexFold = false;
      bool returnsArrayLengthFold = false;
      zc::Maybe<HirParameterReferenceExpression> parameterReference;
      zc::Maybe<HirParameterIndexExpression> parameterIndex;
      zc::Maybe<HirParameterReborrowExpression> parameterReborrow;
      zc::Maybe<HirParameterReferenceExpression> rawDereferenceSource;
      zc::Maybe<HirRawPointerWriteStatement> rawDereferenceWrite;
      zc::Maybe<identity::SourceSpan> writeUnsafeBlockSpan;
      zc::Maybe<checker::checked::CanonicalConstValue> rawDereferenceWriteLiteral;
      zc::Maybe<HirLocalBorrowExpression> localBorrow;
      zc::Maybe<PendingDynFieldAccessReturn> dynFieldAccessReturn;
      zc::Vector<SequentialLocalBinding> deadBindings;
      if (shape.isSequentialLocalReturn) {
        auto sequentialShapeMaybe = sequentialLocalShape(tree, shape.body);
        if (sequentialShapeMaybe == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        SequentialLocalShape sequentialShape{};
        ZC_IF_SOME(value, sequentialShapeMaybe) { sequentialShape = zc::mv(value); }
        // Dead-erase slice: skip the erasure cone (the erased local and its
        // transitively-dead concrete source) so only scalar locals reach
        // MIR/LIR. Unrelated dead scalar locals stay in the shape. The
        // verifier performs the same filter, guaranteeing one identical
        // node-id layout. A filtered shape with zero bindings has no
        // initializer to lower and is rejected.
        {
          const auto dead = deadSequentialBindings(tree, sequentialShape);
          // A dead literal-vs-literal arithmetic binding has no reference
          // operand to anchor a carrier in the LIR slice. Filter it before
          // lowering so only reference-anchored scalar locals reach
          // MIR/LIR.
          for (size_t i = 0; i < sequentialShape.bindings.size(); ++i) {
            if (!dead[i]) continue;
            const auto& binding = sequentialShape.bindings[i];
            if (binding.initializerKind != SequentialInitializerKind::PrimitiveBinary) continue;
            bool leftIsLiteral = false;
            bool rightIsLiteral = false;
            ZC_IF_SOME(left, binding.leftOperand) {
              leftIsLiteral = left.kind == SequentialBinaryOperandKind::Literal;
            }
            ZC_IF_SOME(right, binding.rightOperand) {
              rightIsLiteral = right.kind == SequentialBinaryOperandKind::Literal;
            }
            if (leftIsLiteral && rightIsLiteral) { deadEraseInitializers.add(binding.initializer); }
          }
          const auto removed =
              erasureRelatedDeadBindings(tree, sequentialShape, dead, deadEraseInitializers);
          bool anyRemoved = false;
          for (const bool isRemoved : removed) {
            if (isRemoved) anyRemoved = true;
          }
          if (anyRemoved) {
            // Collect removed bindings for the fact count validation. The
            // checker produces facts for every binding; the filter skips
            // removed ones, so the count validation must add their
            // contributions back.
            for (size_t i = 0; i < sequentialShape.bindings.size(); ++i) {
              if (removed[i]) deadBindings.add(sequentialShape.bindings[i]);
            }
            auto filtered = filterDeadSequentialBindings(sequentialShape, removed);
            if (filtered.bindings.empty()) {
              return rejectHirCapability<HirModuleCandidate>(
                  definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
                  definition.source.clone());
            }
            sequentialShape = zc::mv(filtered);
          }
        }
        const auto returnTypeIndex = factIndex(facts.nodeTypes(), sequentialShape.returnValue);
        if (returnTypeIndex == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t returnTypeSlot = 0;
        ZC_IF_SOME(index, returnTypeIndex) { returnTypeSlot = index; }
        const auto sequentialType = facts.nodeTypes().entries()[returnTypeSlot].value;
        if (sequentialType != callable.success ||
            !typeExists(sequentialType, checkedModule.semanticTypes())) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        zc::Vector<PendingSequentialBinding> pendingBindings;
        zc::Vector<binder::OwnerLocalBindingId> localBindingIds;
        bool rejected = false;
        for (size_t bindingIndex = 0; bindingIndex < sequentialShape.bindings.size();
             ++bindingIndex) {
          const auto& binding = sequentialShape.bindings[bindingIndex];
          auto ownerBinding =
              ownerLocalBindingForPattern(bound.definitions(), binding.pattern, tree);
          auto patternSpan = bound.parsedModule().spanFor(tree.node(binding.pattern).range);
          auto initializerSpan = bound.parsedModule().spanFor(tree.node(binding.initializer).range);
          auto initializerTypeIndex = factIndex(facts.nodeTypes(), binding.initializer);
          if (ownerBinding == zc::none || patternSpan == zc::none || initializerSpan == zc::none ||
              initializerTypeIndex == zc::none ||
              !ownerLocalMatches(bound.definitions(), ZC_ASSERT_NONNULL(ownerBinding),
                                 binding.pattern, tree)) {
            rejected = true;
            break;
          }
          // Every binding shares the function result type in this slice.
          size_t initializerTypeSlot = 0;
          ZC_IF_SOME(index, initializerTypeIndex) { initializerTypeSlot = index; }
          // Each leading local keeps its own declared type (the initializer's
          // node-type fact, which the checker has already verified matches the
          // binding pattern's annotation); only the returned local must match
          // the function result type, checked separately above.
          const identity::SemanticTypeId bindingType =
              facts.nodeTypes().entries()[initializerTypeSlot].value;
          if (!typeExists(bindingType, checkedModule.semanticTypes())) {
            rejected = true;
            break;
          }
          // A live enum tuple-variant construction in a sequential binding is
          // not lowered yet; reject the owning definition as a capability
          // rejection (ZOM4099) rather than falling through to the count
          // validation.
          if (binding.initializerKind == SequentialInitializerKind::EnumVariantConstruction) {
            return rejectHirCapability<HirModuleCandidate>(
                definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
                definition.source.clone());
          }
          // Locals must be distinct bindings.
          for (const auto existing : localBindingIds) {
            if (existing == ZC_ASSERT_NONNULL(ownerBinding)) rejected = true;
          }
          if (rejected) break;
          localBindingIds.add(ZC_ASSERT_NONNULL(ownerBinding));
          zc::Maybe<checker::checked::CanonicalConstValue> bindingLiteral;
          zc::Maybe<HirNominalAggregateExpression> bindingAggregate;
          zc::Maybe<identity::CallableParameterKey> bindingParameter;
          zc::Maybe<checker::PrimitiveOperation> bindingOperation;
          identity::SemanticTypeId bindingOperandType = bindingType;
          zc::Maybe<PendingSequentialBinaryOperand> bindingLeftOperand;
          zc::Maybe<PendingSequentialBinaryOperand> bindingRightOperand;
          bool bindingIsUnaryDesugar = false;
          zc::Maybe<identity::CallableParameterKey> bindingTernaryConditionParameter;
          bool bindingTernaryConditionIsLocal = false;
          size_t bindingTernaryConditionLocal = 0;
          bool bindingTernaryConditionIsLiteral = false;
          identity::SemanticTypeId bindingTernaryConditionType = bindingType;
          zc::Maybe<checker::checked::CanonicalConstValue> bindingTernaryConditionLiteral;
          zc::Maybe<checker::checked::CanonicalConstValue> bindingTernaryThenLiteral;
          zc::Maybe<checker::checked::CanonicalConstValue> bindingTernaryElseLiteral;
          zc::Maybe<identity::SourceSpan> bindingTernaryConditionSpan;
          zc::Maybe<identity::SourceSpan> bindingTernaryThenSpan;
          zc::Maybe<identity::SourceSpan> bindingTernaryElseSpan;
          if (binding.initializerKind == SequentialInitializerKind::Literal) {
            auto literalIndex = factIndex(facts.literals(), binding.initializer);
            if (literalIndex == zc::none) {
              rejected = true;
              break;
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            if (literalFact.type != bindingType ||
                !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
              rejected = true;
              break;
            }
            bindingLiteral = literalFact.literal.clone();
          } else if (binding.initializerKind == SequentialInitializerKind::EnumVariant ||
                     binding.initializerKind == SequentialInitializerKind::FoldedStringLength ||
                     binding.initializerKind == SequentialInitializerKind::FoldedStringConcat) {
            // A qualified enum variant access `Enum::Variant` lowers to an
            // integer constant (the variant discriminant), a string-length
            // fold `s.length` lowers to the string's byte length, and a
            // string-concat fold `"a" + "b"` lowers to the concatenated
            // string constant. The body-checker emits a literal fact with
            // the folded value.
            auto literalIndex = factIndex(facts.literals(), binding.initializer);
            if (literalIndex == zc::none) {
              rejected = true;
              break;
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            if (literalFact.type != bindingType ||
                !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
              rejected = true;
              break;
            }
            bindingLiteral = literalFact.literal.clone();
          } else if (binding.initializerKind == SequentialInitializerKind::Cast) {
            // An integer `as` cast over a scalar literal inner expression. The
            // cast is a no-op for widening or identity conversions; validate
            // the cast fact and lower the inner literal with the cast result
            // type. The inner literal's node-type fact carries the target type
            // because the checker adopts it as the literal's expected type.
            auto castIndex = factIndex(facts.casts(), binding.initializer);
            if (castIndex == zc::none) {
              rejected = true;
              break;
            }
            size_t castSlot = 0;
            ZC_IF_SOME(index, castIndex) { castSlot = index; }
            const auto& castFact = facts.casts().entries()[castSlot].value;
            if (castFact.node != binding.initializer || castFact.result != bindingType ||
                castFact.target != bindingType || castFact.source != bindingType) {
              rejected = true;
              break;
            }
            auto literalIndex = factIndex(facts.literals(), binding.castInnerNode);
            if (literalIndex == zc::none) {
              rejected = true;
              break;
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            if (literalFact.type != bindingType) {
              rejected = true;
              break;
            }
            bindingLiteral = literalFact.literal.clone();
          } else if (binding.initializerKind == SequentialInitializerKind::Aggregate) {
            auto aggregateIndex = factIndex(facts.aggregates(), binding.initializer);
            if (aggregateIndex == zc::none) {
              rejected = true;
              break;
            }
            size_t aggregateSlot = 0;
            ZC_IF_SOME(index, aggregateIndex) { aggregateSlot = index; }
            const auto& sourceAggregate = facts.aggregates().entries()[aggregateSlot].value;
            if (sourceAggregate.node != binding.initializer ||
                !sourceAggregate.kind.variant().is<checker::checked::NominalAggregate>() ||
                sourceAggregate.resultType != bindingType ||
                !sameSpan(sourceAggregate.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
              rejected = true;
              break;
            }
            zc::Vector<HirNominalAggregateElement> elements;
            for (const auto& sourceElement : sourceAggregate.elements) {
              if (sourceElement.field == zc::none ||
                  sourceElement.sourceType != sourceElement.destinationType ||
                  sourceElement.adjustment != zc::none) {
                rejected = true;
                break;
              }
              auto elementLiteral = factIndex(facts.literals(), sourceElement.sourceNode);
              auto elementSpan =
                  bound.parsedModule().spanFor(tree.node(sourceElement.sourceNode).range);
              if (elementLiteral == zc::none || elementSpan == zc::none) {
                rejected = true;
                break;
              }
              size_t elementSlot = 0;
              ZC_IF_SOME(index, elementLiteral) { elementSlot = index; }
              const auto& literalFact = facts.literals().entries()[elementSlot].value;
              if (literalFact.type != sourceElement.destinationType ||
                  !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(elementSpan))) {
                rejected = true;
                break;
              }
              ZC_IF_SOME(field, sourceElement.field) {
                elements.add(HirNominalAggregateElement{field, sourceElement.destinationType,
                                                        literalFact.literal.clone(),
                                                        ZC_ASSERT_NONNULL(elementSpan).clone()});
              }
            }
            if (rejected) break;
            bindingAggregate = HirNominalAggregateExpression{
                HirNodeId(),
                sourceAggregate.kind.variant().get<checker::checked::NominalAggregate>().definition,
                bindingType,
                zc::mv(elements),
                HirValueCategory::Value,
                ZC_ASSERT_NONNULL(initializerSpan).clone()};
          } else if (binding.initializerKind == SequentialInitializerKind::LocalReference) {
            // A reference to an earlier local must resolve to that owner binding.
            auto referenceBinding = resolvedOwnerLocal(bound.bindings(), binding.initializer);
            if (referenceBinding == zc::none ||
                ZC_ASSERT_NONNULL(referenceBinding) != localBindingIds[binding.referencedLocal]) {
              rejected = true;
              break;
            }
          } else if (binding.initializerKind == SequentialInitializerKind::PrimitiveUnary) {
            // A primitive unary initializer desugars to an equivalent binary
            // with a synthetic constant operand. Validate the checked call
            // fact, resolve the real operand, and build the synthetic operand.
            if (binding.unaryOperation == zc::none || binding.unaryOperand == zc::none) {
              rejected = true;
              break;
            }
            auto callIndex = factIndex(facts.calls(), binding.initializer);
            if (callIndex == zc::none) {
              rejected = true;
              break;
            }
            size_t callSlot = 0;
            ZC_IF_SOME(index, callIndex) { callSlot = index; }
            const auto& callFact = facts.calls().entries()[callSlot].value;
            const auto& call = callFact.invocation;
            const auto& selected = call.selected.variant();
            if (!selected.is<checker::checked::PrimitiveCallable>()) {
              rejected = true;
              break;
            }
            const auto operation = selected.get<checker::checked::PrimitiveCallable>().operation;
            const auto unaryOperandType =
                call.arguments.size() == 1 ? call.arguments[0].sourceType : bindingType;
            const ast::NodeId unaryOperandNode(
                tree.node(binding.initializer).payload.words[ast::kUnaryExpressionOperandWord]);
            if (!isScalarUnaryOperation(operation) ||
                operation != ZC_ASSERT_NONNULL(binding.unaryOperation) ||
                callFact.node != binding.initializer || call.calleeType != unaryOperandType ||
                call.receiver != zc::none || call.receiverMode != zc::none ||
                call.receiverAdjustment != zc::none || call.arguments.size() != 1 ||
                call.arguments[0].sourceNode != unaryOperandNode ||
                call.arguments[0].sourceType != unaryOperandType ||
                call.successType != bindingType || call.resultType != bindingType ||
                call.substitutions != zc::none || call.witnesses != zc::none ||
                call.raises != zc::none) {
              rejected = true;
              break;
            }
            auto operandKind = primitiveKindOf(checkedModule.semanticTypes(), unaryOperandType);
            if (operandKind == zc::none) {
              rejected = true;
              break;
            }
            auto desugar = desugarPrimitiveUnary(operation, ZC_ASSERT_NONNULL(operandKind));
            if (desugar == zc::none) {
              rejected = true;
              break;
            }
            // Resolve the real operand: a parameter reference or a reference to
            // an earlier local. The operand's source span is the identifier's
            // span, not the enclosing unary expression's span.
            const auto& classified = ZC_ASSERT_NONNULL(binding.unaryOperand);
            auto operandSpan = bound.parsedModule().spanFor(tree.node(classified.node).range);
            if (operandSpan == zc::none) {
              rejected = true;
              break;
            }
            zc::Maybe<PendingSequentialBinaryOperand> resolvedReal;
            zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
            zc::Maybe<identity::CallableParameterKey> noParameter;
            zc::Maybe<checker::PrimitiveOperation> noNested;
            zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedLeft;
            zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedRight;
            if (classified.kind == SequentialBinaryOperandKind::LocalReference) {
              if (classified.referencedLocal >= localBindingIds.size()) {
                rejected = true;
                break;
              }
              auto referenceBinding = resolvedOwnerLocal(bound.bindings(), classified.node);
              if (referenceBinding == zc::none || ZC_ASSERT_NONNULL(referenceBinding) !=
                                                      localBindingIds[classified.referencedLocal]) {
                rejected = true;
                break;
              }
              resolvedReal =
                  PendingSequentialBinaryOperand{SequentialBinaryOperandKind::LocalReference,
                                                 unaryOperandType,
                                                 ZC_ASSERT_NONNULL(operandSpan).clone(),
                                                 zc::mv(noLiteral),
                                                 zc::mv(noParameter),
                                                 classified.referencedLocal,
                                                 zc::mv(noNested),
                                                 zc::mv(noNestedLeft),
                                                 zc::mv(noNestedRight),
                                                 classified.node};
            } else if (classified.kind == SequentialBinaryOperandKind::ParameterReference) {
              auto parameterHandle = resolvedCallableParameter(bound.bindings(), classified.node);
              if (parameterHandle == zc::none) {
                rejected = true;
                break;
              }
              zc::Maybe<identity::CallableParameterKey> resolvedKey;
              ZC_IF_SOME(handle, parameterHandle) {
                auto authority = registries.callableParameter(handle);
                ZC_IF_SOME(entry, authority) {
                  for (const auto& parameter : parameters) {
                    if (parameter.key == entry.key() && parameter.type == unaryOperandType) {
                      resolvedKey = entry.key().clone();
                    }
                  }
                }
              }
              if (resolvedKey == zc::none) {
                rejected = true;
                break;
              }
              resolvedReal =
                  PendingSequentialBinaryOperand{SequentialBinaryOperandKind::ParameterReference,
                                                 unaryOperandType,
                                                 ZC_ASSERT_NONNULL(operandSpan).clone(),
                                                 zc::mv(noLiteral),
                                                 zc::mv(resolvedKey),
                                                 0,
                                                 zc::mv(noNested),
                                                 zc::mv(noNestedLeft),
                                                 zc::mv(noNestedRight),
                                                 classified.node};
            } else {
              rejected = true;
              break;
            }
            // Build the synthetic constant operand.
            zc::Maybe<checker::checked::CanonicalConstValue> noLiteral2;
            zc::Maybe<identity::CallableParameterKey> noParameter2;
            zc::Maybe<checker::PrimitiveOperation> noNested2;
            zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedLeft2;
            zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedRight2;
            auto syntheticValue = ZC_ASSERT_NONNULL(desugar).syntheticValue.clone();
            auto syntheticOperand =
                PendingSequentialBinaryOperand{SequentialBinaryOperandKind::Literal,
                                               unaryOperandType,
                                               ZC_ASSERT_NONNULL(initializerSpan).clone(),
                                               zc::mv(syntheticValue),
                                               zc::mv(noParameter2),
                                               0,
                                               zc::mv(noNested2),
                                               zc::mv(noNestedLeft2),
                                               zc::mv(noNestedRight2)};
            if (ZC_ASSERT_NONNULL(desugar).syntheticOnLeft) {
              bindingLeftOperand = zc::mv(syntheticOperand);
              bindingRightOperand = zc::mv(ZC_ASSERT_NONNULL(resolvedReal));
            } else {
              bindingLeftOperand = zc::mv(ZC_ASSERT_NONNULL(resolvedReal));
              bindingRightOperand = zc::mv(syntheticOperand);
            }
            bindingOperation = ZC_ASSERT_NONNULL(desugar).binaryOperation;
            bindingOperandType = unaryOperandType;
            bindingIsUnaryDesugar = true;
          } else if (binding.initializerKind == SequentialInitializerKind::Increment) {
            // A prefix increment/decrement initializer (`let y = ++x;`)
            // desugars to a binary write (`x = x +/- 1`) followed by a
            // local-reference initializer. Validate the checked call fact,
            // resolve the operand local, and build the synthetic literal 1.
            if (binding.unaryOperation == zc::none || binding.unaryOperand == zc::none) {
              rejected = true;
              break;
            }
            auto callIndex = factIndex(facts.calls(), binding.initializer);
            if (callIndex == zc::none) {
              rejected = true;
              break;
            }
            size_t callSlot = 0;
            ZC_IF_SOME(index, callIndex) { callSlot = index; }
            const auto& callFact = facts.calls().entries()[callSlot].value;
            const auto& call = callFact.invocation;
            const auto& selected = call.selected.variant();
            if (!selected.is<checker::checked::PrimitiveCallable>()) {
              rejected = true;
              break;
            }
            const auto operation = selected.get<checker::checked::PrimitiveCallable>().operation;
            if (operation != checker::PrimitiveOperation::PreIncrement &&
                operation != checker::PrimitiveOperation::PreDecrement) {
              rejected = true;
              break;
            }
            const auto binaryOperation = operation == checker::PrimitiveOperation::PreIncrement
                                             ? checker::PrimitiveOperation::Add
                                             : checker::PrimitiveOperation::Sub;
            const auto unaryOperandType =
                call.arguments.size() == 1 ? call.arguments[0].sourceType : bindingType;
            const ast::NodeId unaryOperandNode(
                tree.node(binding.initializer).payload.words[ast::kUnaryExpressionOperandWord]);
            if (callFact.node != binding.initializer || call.calleeType != unaryOperandType ||
                call.receiver != zc::none || call.receiverMode != zc::none ||
                call.receiverAdjustment != zc::none || call.arguments.size() != 1 ||
                call.arguments[0].sourceNode != unaryOperandNode ||
                call.arguments[0].sourceType != unaryOperandType ||
                call.successType != bindingType || call.resultType != bindingType ||
                call.substitutions != zc::none || call.witnesses != zc::none ||
                call.raises != zc::none) {
              rejected = true;
              break;
            }
            // The operand must be an earlier local (never a parameter).
            const auto& classified = ZC_ASSERT_NONNULL(binding.unaryOperand);
            if (classified.kind != SequentialBinaryOperandKind::LocalReference ||
                classified.referencedLocal >= localBindingIds.size()) {
              rejected = true;
              break;
            }
            auto referenceBinding = resolvedOwnerLocal(bound.bindings(), classified.node);
            if (referenceBinding == zc::none || ZC_ASSERT_NONNULL(referenceBinding) !=
                                                    localBindingIds[classified.referencedLocal]) {
              rejected = true;
              break;
            }
            auto operandSpan = bound.parsedModule().spanFor(tree.node(classified.node).range);
            if (operandSpan == zc::none) {
              rejected = true;
              break;
            }
            zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
            zc::Maybe<identity::CallableParameterKey> noParameter;
            zc::Maybe<checker::PrimitiveOperation> noNested;
            zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedLeft;
            zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedRight;
            auto resolvedReal =
                PendingSequentialBinaryOperand{SequentialBinaryOperandKind::LocalReference,
                                               unaryOperandType,
                                               ZC_ASSERT_NONNULL(operandSpan).clone(),
                                               zc::mv(noLiteral),
                                               zc::mv(noParameter),
                                               classified.referencedLocal,
                                               zc::mv(noNested),
                                               zc::mv(noNestedLeft),
                                               zc::mv(noNestedRight),
                                               classified.node};
            auto unitMagnitude = zc::heapArray<uint8_t>(1);
            unitMagnitude[0] = 1;
            auto syntheticOne =
                checker::checked::CanonicalConstValue::integer(checker::signature::CanonicalInteger{
                    checker::signature::IntegerSign::NonNegative, zc::mv(unitMagnitude)});
            zc::Maybe<checker::checked::CanonicalConstValue> syntheticLiteral =
                zc::mv(syntheticOne);
            zc::Maybe<identity::CallableParameterKey> noParameter2;
            zc::Maybe<checker::PrimitiveOperation> noNested2;
            zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedLeft2;
            zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedRight2;
            auto syntheticOperand =
                PendingSequentialBinaryOperand{SequentialBinaryOperandKind::Literal,
                                               unaryOperandType,
                                               ZC_ASSERT_NONNULL(initializerSpan).clone(),
                                               zc::mv(syntheticLiteral),
                                               zc::mv(noParameter2),
                                               0,
                                               zc::mv(noNested2),
                                               zc::mv(noNestedLeft2),
                                               zc::mv(noNestedRight2),
                                               ast::NodeId()};
            bindingLeftOperand = zc::mv(resolvedReal);
            bindingRightOperand = zc::mv(syntheticOperand);
            bindingOperation = binaryOperation;
            bindingOperandType = unaryOperandType;
            bindingIsUnaryDesugar = true;
          } else if (binding.initializerKind == SequentialInitializerKind::PostfixIncrement) {
            // A postfix increment/decrement initializer (`let y = x++;`)
            // desugars to a local-reference initializer (reading the old
            // value) followed by a binary write (`x = x +/- 1`). Validate the
            // checked call fact, resolve the operand local, and build the
            // synthetic literal 1. The write follows the binding, the reverse
            // of the prefix form.
            if (binding.unaryOperation == zc::none || binding.unaryOperand == zc::none) {
              rejected = true;
              break;
            }
            auto callIndex = factIndex(facts.calls(), binding.initializer);
            if (callIndex == zc::none) {
              rejected = true;
              break;
            }
            size_t callSlot = 0;
            ZC_IF_SOME(index, callIndex) { callSlot = index; }
            const auto& callFact = facts.calls().entries()[callSlot].value;
            const auto& call = callFact.invocation;
            const auto& selected = call.selected.variant();
            if (!selected.is<checker::checked::PrimitiveCallable>()) {
              rejected = true;
              break;
            }
            const auto operation = selected.get<checker::checked::PrimitiveCallable>().operation;
            if (operation != checker::PrimitiveOperation::PostIncrement &&
                operation != checker::PrimitiveOperation::PostDecrement) {
              rejected = true;
              break;
            }
            const auto binaryOperation = operation == checker::PrimitiveOperation::PostIncrement
                                             ? checker::PrimitiveOperation::Add
                                             : checker::PrimitiveOperation::Sub;
            const auto unaryOperandType =
                call.arguments.size() == 1 ? call.arguments[0].sourceType : bindingType;
            const ast::NodeId unaryOperandNode(
                tree.node(binding.initializer).payload.words[ast::kPostfixExpressionOperandWord]);
            if (callFact.node != binding.initializer || call.calleeType != unaryOperandType ||
                call.receiver != zc::none || call.receiverMode != zc::none ||
                call.receiverAdjustment != zc::none || call.arguments.size() != 1 ||
                call.arguments[0].sourceNode != unaryOperandNode ||
                call.arguments[0].sourceType != unaryOperandType ||
                call.successType != bindingType || call.resultType != bindingType ||
                call.substitutions != zc::none || call.witnesses != zc::none ||
                call.raises != zc::none) {
              rejected = true;
              break;
            }
            // The operand must be an earlier local (never a parameter).
            const auto& classified = ZC_ASSERT_NONNULL(binding.unaryOperand);
            if (classified.kind != SequentialBinaryOperandKind::LocalReference ||
                classified.referencedLocal >= localBindingIds.size()) {
              rejected = true;
              break;
            }
            auto referenceBinding = resolvedOwnerLocal(bound.bindings(), classified.node);
            if (referenceBinding == zc::none || ZC_ASSERT_NONNULL(referenceBinding) !=
                                                    localBindingIds[classified.referencedLocal]) {
              rejected = true;
              break;
            }
            auto operandSpan = bound.parsedModule().spanFor(tree.node(classified.node).range);
            if (operandSpan == zc::none) {
              rejected = true;
              break;
            }
            zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
            zc::Maybe<identity::CallableParameterKey> noParameter;
            zc::Maybe<checker::PrimitiveOperation> noNested;
            zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedLeft;
            zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedRight;
            auto resolvedReal =
                PendingSequentialBinaryOperand{SequentialBinaryOperandKind::LocalReference,
                                               unaryOperandType,
                                               ZC_ASSERT_NONNULL(operandSpan).clone(),
                                               zc::mv(noLiteral),
                                               zc::mv(noParameter),
                                               classified.referencedLocal,
                                               zc::mv(noNested),
                                               zc::mv(noNestedLeft),
                                               zc::mv(noNestedRight),
                                               classified.node};
            auto unitMagnitude = zc::heapArray<uint8_t>(1);
            unitMagnitude[0] = 1;
            auto syntheticOne =
                checker::checked::CanonicalConstValue::integer(checker::signature::CanonicalInteger{
                    checker::signature::IntegerSign::NonNegative, zc::mv(unitMagnitude)});
            zc::Maybe<checker::checked::CanonicalConstValue> syntheticLiteral =
                zc::mv(syntheticOne);
            zc::Maybe<identity::CallableParameterKey> noParameter2;
            zc::Maybe<checker::PrimitiveOperation> noNested2;
            zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedLeft2;
            zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedRight2;
            auto syntheticOperand =
                PendingSequentialBinaryOperand{SequentialBinaryOperandKind::Literal,
                                               unaryOperandType,
                                               ZC_ASSERT_NONNULL(initializerSpan).clone(),
                                               zc::mv(syntheticLiteral),
                                               zc::mv(noParameter2),
                                               0,
                                               zc::mv(noNested2),
                                               zc::mv(noNestedLeft2),
                                               zc::mv(noNestedRight2),
                                               ast::NodeId()};
            bindingLeftOperand = zc::mv(resolvedReal);
            bindingRightOperand = zc::mv(syntheticOperand);
            bindingOperation = binaryOperation;
            bindingOperandType = unaryOperandType;
            bindingIsUnaryDesugar = true;
          } else if (binding.initializerKind == SequentialInitializerKind::PrimitiveBinary) {
            // A primitive binary initializer: validate its checked call fact and
            // resolve each operand to a literal, a parameter, or an earlier local.
            auto callIndex = factIndex(facts.calls(), binding.initializer);
            if (callIndex == zc::none || binding.leftOperand == zc::none ||
                binding.rightOperand == zc::none) {
              rejected = true;
              break;
            }
            size_t callSlot = 0;
            ZC_IF_SOME(index, callIndex) { callSlot = index; }
            const auto& callFact = facts.calls().entries()[callSlot].value;
            const auto& call = callFact.invocation;
            const auto& selected = call.selected.variant();
            if (!selected.is<checker::checked::PrimitiveCallable>()) {
              rejected = true;
              break;
            }
            const auto operation = selected.get<checker::checked::PrimitiveCallable>().operation;
            const bool comparison = isScalarComparisonOperation(operation);
            const bool arithmetic = isScalarArithmeticOperation(operation);
            // The operand type is the shared argument type; a comparison yields
            // a bool result while an arithmetic operator yields its operand
            // type. The binding (result) type is the local's own declared type.
            const auto binaryOperandType =
                call.arguments.size() == 2 ? call.arguments[0].sourceType : bindingType;
            const bool operationSupported =
                comparison || (arithmetic && bindingType == binaryOperandType);
            const ast::NodeId binaryLeft(
                tree.node(binding.initializer).payload.words[ast::kBinaryExprLhsWord]);
            const ast::NodeId binaryRight(
                tree.node(binding.initializer).payload.words[ast::kBinaryExprRhsWord]);
            if (!operationSupported || callFact.node != binding.initializer ||
                call.calleeType != binaryOperandType || call.receiver != zc::none ||
                call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
                call.arguments.size() != 2 || call.arguments[0].sourceNode != binaryLeft ||
                call.arguments[0].sourceType != binaryOperandType ||
                call.arguments[1].sourceNode != binaryRight ||
                call.arguments[1].sourceType != binaryOperandType ||
                call.successType != bindingType || call.resultType != bindingType ||
                call.substitutions != zc::none || call.witnesses != zc::none ||
                call.raises != zc::none) {
              rejected = true;
              break;
            }
            // Resolve each classified operand into a materializable operand. A
            // leaf resolver builds a literal/parameter/local operand; the operand
            // resolver additionally handles a nested one-level binary by
            // validating its own checked call fact keyed on the operand node.
            auto resolveLeaf = [&](const SequentialBinaryLeafOperand& leaf,
                                   identity::SemanticTypeId leafType)
                -> zc::Maybe<PendingSequentialBinaryLeafOperand> {
              auto leafSpan = bound.parsedModule().spanFor(tree.node(leaf.node).range);
              if (leafSpan == zc::none) return zc::none;
              if (leaf.kind == SequentialBinaryOperandKind::Literal) {
                auto leafLiteral = factIndex(facts.literals(), leaf.node);
                if (leafLiteral == zc::none) return zc::none;
                size_t leafLiteralSlot = 0;
                ZC_IF_SOME(index, leafLiteral) { leafLiteralSlot = index; }
                const auto& literalFact = facts.literals().entries()[leafLiteralSlot].value;
                if (literalFact.type != leafType) return zc::none;
                zc::Maybe<checker::checked::CanonicalConstValue> literalValue =
                    literalFact.literal.clone();
                zc::Maybe<identity::CallableParameterKey> noParameter;
                return PendingSequentialBinaryLeafOperand{SequentialBinaryOperandKind::Literal,
                                                          leafType,
                                                          ZC_ASSERT_NONNULL(leafSpan).clone(),
                                                          zc::mv(literalValue),
                                                          zc::mv(noParameter),
                                                          0,
                                                          leaf.node};
              }
              if (leaf.kind == SequentialBinaryOperandKind::LocalReference) {
                if (leaf.referencedLocal >= localBindingIds.size()) return zc::none;
                auto referenceBinding = resolvedOwnerLocal(bound.bindings(), leaf.node);
                if (referenceBinding == zc::none ||
                    ZC_ASSERT_NONNULL(referenceBinding) != localBindingIds[leaf.referencedLocal]) {
                  return zc::none;
                }
                zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
                zc::Maybe<identity::CallableParameterKey> noParameter;
                return PendingSequentialBinaryLeafOperand{
                    SequentialBinaryOperandKind::LocalReference,
                    leafType,
                    ZC_ASSERT_NONNULL(leafSpan).clone(),
                    zc::mv(noLiteral),
                    zc::mv(noParameter),
                    leaf.referencedLocal,
                    leaf.node};
              }
              auto parameterHandle = resolvedCallableParameter(bound.bindings(), leaf.node);
              if (parameterHandle == zc::none) return zc::none;
              zc::Maybe<identity::CallableParameterKey> resolvedKey;
              ZC_IF_SOME(handle, parameterHandle) {
                auto authority = registries.callableParameter(handle);
                ZC_IF_SOME(entry, authority) {
                  for (const auto& parameter : parameters) {
                    if (parameter.key == entry.key() && parameter.type == leafType) {
                      resolvedKey = entry.key().clone();
                    }
                  }
                }
              }
              if (resolvedKey == zc::none) return zc::none;
              zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
              return PendingSequentialBinaryLeafOperand{
                  SequentialBinaryOperandKind::ParameterReference,
                  leafType,
                  ZC_ASSERT_NONNULL(leafSpan).clone(),
                  zc::mv(noLiteral),
                  zc::mv(resolvedKey),
                  0,
                  leaf.node};
            };
            auto resolveOperand = [&](const SequentialBinaryOperand& operand)
                -> zc::Maybe<PendingSequentialBinaryOperand> {
              auto operandSpan = bound.parsedModule().spanFor(tree.node(operand.node).range);
              if (operandSpan == zc::none) return zc::none;
              zc::Maybe<checker::PrimitiveOperation> noNestedOperation;
              zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedLeft;
              zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedRight;
              if (operand.kind == SequentialBinaryOperandKind::NestedBinary) {
                // A nested one-level binary carries its own checked call fact keyed
                // on the operand node; validate it as a same-typed arithmetic (or
                // comparison agreeing on type) binary of two same-typed leaves.
                auto nestedCallIndex = factIndex(facts.calls(), operand.node);
                if (nestedCallIndex == zc::none || operand.nestedLeft == zc::none ||
                    operand.nestedRight == zc::none || operand.nestedOperation == zc::none) {
                  return zc::none;
                }
                size_t nestedCallSlot = 0;
                ZC_IF_SOME(index, nestedCallIndex) { nestedCallSlot = index; }
                const auto& nestedFact = facts.calls().entries()[nestedCallSlot].value;
                const auto& nestedCall = nestedFact.invocation;
                const auto& nestedSelected = nestedCall.selected.variant();
                if (!nestedSelected.is<checker::checked::PrimitiveCallable>()) return zc::none;
                const auto nestedOp =
                    nestedSelected.get<checker::checked::PrimitiveCallable>().operation;
                const bool nestedComparison = isScalarComparisonOperation(nestedOp);
                const bool nestedArithmetic = isScalarArithmeticOperation(nestedOp);
                const auto nestedOperandType = nestedCall.arguments.size() == 2
                                                   ? nestedCall.arguments[0].sourceType
                                                   : binaryOperandType;
                // The nested result feeds the parent operand slot, so its result
                // type must equal the parent operand type; a comparison result
                // (bool) can only agree when the parent operand type is bool.
                const bool nestedSupported =
                    (nestedArithmetic && nestedOperandType == binaryOperandType) ||
                    (nestedComparison && binaryOperandType == nestedCall.resultType);
                const ast::NodeId nestedLeftNode(
                    tree.node(operand.node).payload.words[ast::kBinaryExprLhsWord]);
                const ast::NodeId nestedRightNode(
                    tree.node(operand.node).payload.words[ast::kBinaryExprRhsWord]);
                if (!nestedSupported || nestedOp != ZC_ASSERT_NONNULL(operand.nestedOperation) ||
                    nestedFact.node != operand.node || nestedCall.calleeType != nestedOperandType ||
                    nestedCall.receiver != zc::none || nestedCall.receiverMode != zc::none ||
                    nestedCall.receiverAdjustment != zc::none || nestedCall.arguments.size() != 2 ||
                    nestedCall.arguments[0].sourceNode != nestedLeftNode ||
                    nestedCall.arguments[0].sourceType != nestedOperandType ||
                    nestedCall.arguments[1].sourceNode != nestedRightNode ||
                    nestedCall.arguments[1].sourceType != nestedOperandType ||
                    nestedCall.resultType != binaryOperandType ||
                    nestedCall.substitutions != zc::none || nestedCall.witnesses != zc::none ||
                    nestedCall.raises != zc::none) {
                  return zc::none;
                }
                zc::Maybe<PendingSequentialBinaryLeafOperand> resolvedNestedLeft;
                zc::Maybe<PendingSequentialBinaryLeafOperand> resolvedNestedRight;
                ZC_IF_SOME(leaf, operand.nestedLeft) {
                  resolvedNestedLeft = resolveLeaf(leaf, nestedOperandType);
                }
                ZC_IF_SOME(leaf, operand.nestedRight) {
                  resolvedNestedRight = resolveLeaf(leaf, nestedOperandType);
                }
                if (resolvedNestedLeft == zc::none || resolvedNestedRight == zc::none) {
                  return zc::none;
                }
                zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
                zc::Maybe<identity::CallableParameterKey> noParameter;
                zc::Maybe<checker::PrimitiveOperation> nestedOperation = nestedOp;
                return PendingSequentialBinaryOperand{SequentialBinaryOperandKind::NestedBinary,
                                                      binaryOperandType,
                                                      ZC_ASSERT_NONNULL(operandSpan).clone(),
                                                      zc::mv(noLiteral),
                                                      zc::mv(noParameter),
                                                      0,
                                                      zc::mv(nestedOperation),
                                                      zc::mv(resolvedNestedLeft),
                                                      zc::mv(resolvedNestedRight),
                                                      operand.node};
              }
              if (operand.kind == SequentialBinaryOperandKind::Literal) {
                auto operandLiteral = factIndex(facts.literals(), operand.node);
                if (operandLiteral == zc::none) return zc::none;
                size_t operandLiteralSlot = 0;
                ZC_IF_SOME(index, operandLiteral) { operandLiteralSlot = index; }
                const auto& literalFact = facts.literals().entries()[operandLiteralSlot].value;
                if (literalFact.type != binaryOperandType) return zc::none;
                zc::Maybe<checker::checked::CanonicalConstValue> literalValue =
                    literalFact.literal.clone();
                zc::Maybe<identity::CallableParameterKey> noParameter;
                return PendingSequentialBinaryOperand{SequentialBinaryOperandKind::Literal,
                                                      binaryOperandType,
                                                      ZC_ASSERT_NONNULL(operandSpan).clone(),
                                                      zc::mv(literalValue),
                                                      zc::mv(noParameter),
                                                      0,
                                                      zc::mv(noNestedOperation),
                                                      zc::mv(noNestedLeft),
                                                      zc::mv(noNestedRight),
                                                      operand.node};
              }
              if (operand.kind == SequentialBinaryOperandKind::LocalReference) {
                if (operand.referencedLocal >= localBindingIds.size()) return zc::none;
                auto referenceBinding = resolvedOwnerLocal(bound.bindings(), operand.node);
                if (referenceBinding == zc::none || ZC_ASSERT_NONNULL(referenceBinding) !=
                                                        localBindingIds[operand.referencedLocal]) {
                  return zc::none;
                }
                zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
                zc::Maybe<identity::CallableParameterKey> noParameter;
                return PendingSequentialBinaryOperand{SequentialBinaryOperandKind::LocalReference,
                                                      binaryOperandType,
                                                      ZC_ASSERT_NONNULL(operandSpan).clone(),
                                                      zc::mv(noLiteral),
                                                      zc::mv(noParameter),
                                                      operand.referencedLocal,
                                                      zc::mv(noNestedOperation),
                                                      zc::mv(noNestedLeft),
                                                      zc::mv(noNestedRight),
                                                      operand.node};
              }
              auto parameterHandle = resolvedCallableParameter(bound.bindings(), operand.node);
              if (parameterHandle == zc::none) return zc::none;
              zc::Maybe<identity::CallableParameterKey> resolvedKey;
              ZC_IF_SOME(handle, parameterHandle) {
                auto authority = registries.callableParameter(handle);
                ZC_IF_SOME(entry, authority) {
                  for (const auto& parameter : parameters) {
                    if (parameter.key == entry.key() && parameter.type == binaryOperandType) {
                      resolvedKey = entry.key().clone();
                    }
                  }
                }
              }
              if (resolvedKey == zc::none) return zc::none;
              zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
              return PendingSequentialBinaryOperand{SequentialBinaryOperandKind::ParameterReference,
                                                    binaryOperandType,
                                                    ZC_ASSERT_NONNULL(operandSpan).clone(),
                                                    zc::mv(noLiteral),
                                                    zc::mv(resolvedKey),
                                                    0,
                                                    zc::mv(noNestedOperation),
                                                    zc::mv(noNestedLeft),
                                                    zc::mv(noNestedRight),
                                                    operand.node};
            };
            ZC_IF_SOME(leftClassified, binding.leftOperand) {
              ZC_IF_SOME(rightClassified, binding.rightOperand) {
                auto resolvedLeft = resolveOperand(leftClassified);
                auto resolvedRight = resolveOperand(rightClassified);
                if (resolvedLeft == zc::none || resolvedRight == zc::none) {
                  rejected = true;
                } else {
                  bindingOperation = operation;
                  bindingOperandType = binaryOperandType;
                  bindingLeftOperand = zc::mv(resolvedLeft);
                  bindingRightOperand = zc::mv(resolvedRight);
                }
              }
            }
            if (rejected) break;
          } else if (binding.initializerKind == SequentialInitializerKind::Ternary) {
            // A ternary conditional expression `cond ? then : else`. The
            // condition is a bool IdentExpr naming a parameter or an earlier
            // local; both branches are scalar literals of the same type.
            auto condSpan = bound.parsedModule().spanFor(tree.node(binding.ternaryCondNode).range);
            auto thenSpan = bound.parsedModule().spanFor(tree.node(binding.ternaryThenNode).range);
            auto elseSpan = bound.parsedModule().spanFor(tree.node(binding.ternaryElseNode).range);
            if (condSpan == zc::none || thenSpan == zc::none || elseSpan == zc::none) {
              rejected = true;
              break;
            }
            // Resolve the condition to a bool literal, a parameter, or an
            // earlier local.
            if (binding.ternaryConditionIsLiteral) {
              bindingTernaryConditionIsLiteral = true;
              auto condLiteralIndex = factIndex(facts.literals(), binding.ternaryCondNode);
              if (condLiteralIndex == zc::none) {
                rejected = true;
                break;
              }
              size_t condLiteralSlot = 0;
              ZC_IF_SOME(index, condLiteralIndex) { condLiteralSlot = index; }
              const auto& condLiteralFact = facts.literals().entries()[condLiteralSlot].value;
              bindingTernaryConditionLiteral = condLiteralFact.literal.clone();
            } else if (binding.ternaryConditionIsLocal) {
              bindingTernaryConditionIsLocal = true;
              for (size_t earlier = 0; earlier < bindingIndex; ++earlier) {
                if (matchesLocalReference(tree, sequentialShape.bindings[earlier].pattern,
                                          binding.ternaryCondNode)) {
                  bindingTernaryConditionLocal = earlier;
                  break;
                }
              }
            } else {
              auto parameterHandle =
                  resolvedCallableParameter(bound.bindings(), binding.ternaryCondNode);
              if (parameterHandle == zc::none) {
                rejected = true;
                break;
              }
              zc::Maybe<identity::CallableParameterKey> resolvedKey;
              ZC_IF_SOME(handle, parameterHandle) {
                auto authority = registries.callableParameter(handle);
                ZC_IF_SOME(entry, authority) {
                  for (const auto& parameter : parameters) {
                    if (parameter.key == entry.key()) { resolvedKey = entry.key().clone(); }
                  }
                }
              }
              if (resolvedKey == zc::none) {
                rejected = true;
                break;
              }
              bindingTernaryConditionParameter = zc::mv(resolvedKey);
            }
            // The condition type is bool.
            auto condTypeIndex = factIndex(facts.nodeTypes(), binding.ternaryCondNode);
            if (condTypeIndex == zc::none) {
              rejected = true;
              break;
            }
            size_t condTypeSlot = 0;
            ZC_IF_SOME(index, condTypeIndex) { condTypeSlot = index; }
            bindingTernaryConditionType = facts.nodeTypes().entries()[condTypeSlot].value;
            // Both branches are scalar literals; fetch their literal facts.
            for (const auto* branchNode : {&binding.ternaryThenNode, &binding.ternaryElseNode}) {
              auto literalIndex = factIndex(facts.literals(), *branchNode);
              if (literalIndex == zc::none) {
                rejected = true;
                break;
              }
              size_t literalSlot = 0;
              ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
              const auto& literalFact = facts.literals().entries()[literalSlot].value;
              if (literalFact.type != bindingType) {
                rejected = true;
                break;
              }
              if (branchNode == &binding.ternaryThenNode) {
                bindingTernaryThenLiteral = literalFact.literal.clone();
              } else {
                bindingTernaryElseLiteral = literalFact.literal.clone();
              }
            }
            if (rejected) break;
            bindingTernaryConditionSpan = ZC_ASSERT_NONNULL(condSpan).clone();
            bindingTernaryThenSpan = ZC_ASSERT_NONNULL(thenSpan).clone();
            bindingTernaryElseSpan = ZC_ASSERT_NONNULL(elseSpan).clone();
          } else {
            // A reference to a parameter must resolve to a declared parameter.
            auto parameterHandle = resolvedCallableParameter(bound.bindings(), binding.initializer);
            if (parameterHandle == zc::none) {
              rejected = true;
              break;
            }
            zc::Maybe<identity::CallableParameterKey> resolvedKey;
            ZC_IF_SOME(handle, parameterHandle) {
              auto authority = registries.callableParameter(handle);
              ZC_IF_SOME(entry, authority) {
                for (const auto& parameter : parameters) {
                  if (parameter.key == entry.key() && parameter.type == bindingType) {
                    resolvedKey = entry.key().clone();
                  }
                }
              }
            }
            if (resolvedKey == zc::none) {
              rejected = true;
              break;
            }
            bindingParameter = zc::mv(resolvedKey);
          }
          pendingBindings.add(PendingSequentialBinding{
              bindingType,
              ZC_ASSERT_NONNULL(patternSpan).clone(),
              ZC_ASSERT_NONNULL(initializerSpan).clone(),
              binding.initializerKind,
              zc::mv(bindingLiteral),
              zc::mv(bindingAggregate),
              zc::mv(bindingParameter),
              binding.referencedLocal,
              zc::mv(bindingOperation),
              bindingOperandType,
              zc::mv(bindingLeftOperand),
              zc::mv(bindingRightOperand),
              bindingIsUnaryDesugar,
              zc::mv(bindingTernaryConditionParameter),
              bindingTernaryConditionIsLocal,
              bindingTernaryConditionLocal,
              bindingTernaryConditionIsLiteral,
              bindingTernaryConditionType,
              zc::mv(bindingTernaryConditionLiteral),
              zc::mv(bindingTernaryThenLiteral),
              zc::mv(bindingTernaryElseLiteral),
              zc::mv(bindingTernaryConditionSpan),
              zc::mv(bindingTernaryThenSpan),
              zc::mv(bindingTernaryElseSpan),
              tree.node(binding.initializer).kind == ast::SyntaxKind::MatchExpr,
              binding.matchHasDefaultArm,
              binding.declarator,
              binding.initializer});
        }
        if (rejected) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        // The returned value names a parameter or one of the declared locals.
        zc::Maybe<identity::CallableParameterKey> returnParameter;
        size_t returnLocal = 0;
        ZC_IF_SOME(index, sequentialShape.returnsLocal) { returnLocal = index; }
        if (sequentialShape.returnsLocal == zc::none) {
          auto parameterHandle =
              resolvedCallableParameter(bound.bindings(), sequentialShape.returnValue);
          if (parameterHandle == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          ZC_IF_SOME(handle, parameterHandle) {
            auto authority = registries.callableParameter(handle);
            ZC_IF_SOME(entry, authority) {
              for (const auto& parameter : parameters) {
                if (parameter.key == entry.key() && parameter.type == sequentialType) {
                  returnParameter = entry.key().clone();
                }
              }
            }
          }
          if (returnParameter == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
        }
        auto returnValueSpan =
            bound.parsedModule().spanFor(tree.node(sequentialShape.returnValue).range);
        if (returnValueSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        PendingSequentialLocalReturn sequential{zc::mv(pendingBindings),
                                                sequentialType,
                                                zc::mv(returnParameter),
                                                returnLocal,
                                                ZC_ASSERT_NONNULL(returnValueSpan).clone(),
                                                zc::mv(deadBindings)};
        pendingFunctions.add(PendingFunctionDeclaration{
            definition.definition, definition.node, callable.success, zc::mv(parameters), zc::none,
            zc::mv(visibilityValue), linkageValue, definition.source.clone(), bodySpanValue.clone(),
            returnSpanValue.clone(), valueSpanValue.clone(), zc::mv(orderingKey), shape.body,
            shape.returnStatement, shape.value, zc::mv(unsafeBlockSpan),
            SequentialLocalReturn{zc::mv(sequential)}});
        continue;
      }
      if (shape.dynFieldAccessReturn) {
        // Three-statement dyn field-access shape:
        // `let zoo = Zoo { animal: Dog { .. } }; let a: dyn I = zoo.animal;
        //  return a.m();`
        // The aggregate carries a concrete-to-existential erasure on its
        // dyn-typed field; the field read copies the fat-pointer carrier into
        // the second local; the method call dispatches indirectly through the
        // vtable.
        const auto& block = tree.node(shape.body);
        const ast::NodeList statements{block.payload.words[ast::kBlockStmtStmtsFirstWord],
                                       block.payload.words[ast::kBlockStmtStmtsSizeWord]};
        if (!tree.contains(statements) || statements.size != 3) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const ast::NodeId aggregateLetStmt = tree.list(statements)[0];
        const ast::NodeId fieldReadLetStmt = tree.list(statements)[1];
        auto aggregateDeclarator = localDeclarator(tree, aggregateLetStmt);
        auto fieldReadDeclarator = localDeclarator(tree, fieldReadLetStmt);
        if (aggregateDeclarator == zc::none || fieldReadDeclarator == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        ast::NodeId aggregateDecl;
        ZC_IF_SOME(v, aggregateDeclarator) { aggregateDecl = v; }
        ast::NodeId fieldReadDecl;
        ZC_IF_SOME(v, fieldReadDeclarator) { fieldReadDecl = v; }
        const ast::NodeId aggregatePattern(
            tree.node(aggregateDecl).payload.words[ast::kVariableDeclaratorPatternWord]);
        const ast::NodeId aggregateInit(
            tree.node(aggregateDecl).payload.words[ast::kVariableDeclaratorInitWord]);
        const ast::NodeId fieldReadPattern(
            tree.node(fieldReadDecl).payload.words[ast::kVariableDeclaratorPatternWord]);
        const ast::NodeId fieldReadInit(
            tree.node(fieldReadDecl).payload.words[ast::kVariableDeclaratorInitWord]);
        auto aggregateBinding =
            ownerLocalBindingForPattern(bound.definitions(), aggregatePattern, tree);
        auto fieldReadBinding =
            ownerLocalBindingForPattern(bound.definitions(), fieldReadPattern, tree);
        if (aggregateBinding == zc::none || fieldReadBinding == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        auto aggregateLetSpan = bound.parsedModule().spanFor(tree.node(aggregateLetStmt).range);
        auto aggregateInitSpan = bound.parsedModule().spanFor(tree.node(aggregateInit).range);
        auto fieldReadLetSpan = bound.parsedModule().spanFor(tree.node(fieldReadLetStmt).range);
        auto fieldReadInitSpan = bound.parsedModule().spanFor(tree.node(fieldReadInit).range);
        if (aggregateLetSpan == zc::none || aggregateInitSpan == zc::none ||
            fieldReadLetSpan == zc::none || fieldReadInitSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        auto aggregateTypeIndex = factIndex(facts.nodeTypes(), aggregateInit);
        auto fieldReadTypeIndex = factIndex(facts.nodeTypes(), fieldReadInit);
        if (aggregateTypeIndex == zc::none || fieldReadTypeIndex == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t aggregateTypeSlot = 0;
        size_t fieldReadTypeSlot = 0;
        ZC_IF_SOME(index, aggregateTypeIndex) { aggregateTypeSlot = index; }
        ZC_IF_SOME(index, fieldReadTypeIndex) { fieldReadTypeSlot = index; }
        const auto aggregateType = facts.nodeTypes().entries()[aggregateTypeSlot].value;
        const auto fieldReadType = facts.nodeTypes().entries()[fieldReadTypeSlot].value;
        if (!typeExists(aggregateType, checkedModule.semanticTypes()) ||
            !typeExists(fieldReadType, checkedModule.semanticTypes())) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        // Build the aggregate with per-field erasure.
        auto aggregateIndex = factIndex(facts.aggregates(), aggregateInit);
        if (aggregateIndex == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t aggregateSlot = 0;
        ZC_IF_SOME(index, aggregateIndex) { aggregateSlot = index; }
        const auto& sourceAggregate = facts.aggregates().entries()[aggregateSlot].value;
        if (sourceAggregate.node != aggregateInit ||
            !sourceAggregate.kind.variant().is<checker::checked::NominalAggregate>() ||
            sourceAggregate.resultType != aggregateType ||
            !sameSpan(sourceAggregate.sourceSpan, ZC_ASSERT_NONNULL(aggregateInitSpan))) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        zc::Vector<HirNominalAggregateElement> elements;
        for (const auto& sourceElement : sourceAggregate.elements) {
          if (sourceElement.field == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto elementSpan =
              bound.parsedModule().spanFor(tree.node(sourceElement.sourceNode).range);
          if (elementSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          zc::Maybe<HirErasureRecord> elementErasure;
          zc::Vector<HirConcreteAggregateElement> concreteElements;
          zc::Maybe<checker::checked::CanonicalConstValue> elementValue;
          if (sourceElement.sourceType != sourceElement.destinationType) {
            if (sourceElement.adjustment == zc::none ||
                !isSingleDynEraseAdjustment(ZC_ASSERT_NONNULL(sourceElement.adjustment))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto& adj = ZC_ASSERT_NONNULL(sourceElement.adjustment);
            if (adj.site != checker::checked::CoercionSite::AggregateField ||
                adj.source != sourceElement.sourceType ||
                adj.destination != sourceElement.destinationType) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto& eraseStep = adj.steps[0].variant().get<checker::checked::DynEraseStep>();
            auto innerIdx = factIndex(facts.aggregates(), sourceElement.sourceNode);
            if (innerIdx == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t innerSlot = 0;
            ZC_IF_SOME(index, innerIdx) { innerSlot = index; }
            const auto& inner = facts.aggregates().entries()[innerSlot].value;
            if (!inner.kind.variant().is<checker::checked::NominalAggregate>()) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto& innerNominal =
                inner.kind.variant().get<checker::checked::NominalAggregate>();
            elementErasure =
                HirErasureRecord{adj.source, adj.destination, eraseStep.impl,
                                 eraseStep.interface.interface, innerNominal.definition};
            for (const auto& ie : inner.elements) {
              auto litIdx = factIndex(facts.literals(), ie.sourceNode);
              if (litIdx == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              size_t litSlot = 0;
              ZC_IF_SOME(index, litIdx) { litSlot = index; }
              const auto& lit = facts.literals().entries()[litSlot].value;
              ZC_IF_SOME(f, ie.field) {
                concreteElements.add(HirConcreteAggregateElement{f, lit.type, lit.literal.clone()});
              }
            }
            elementValue = checker::checked::CanonicalConstValue::null();
          } else {
            if (sourceElement.adjustment != zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            auto elementLiteral = factIndex(facts.literals(), sourceElement.sourceNode);
            if (elementLiteral == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(index, elementLiteral) { literalSlot = index; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            if (literalFact.type != sourceElement.destinationType ||
                !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(elementSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            elementValue = literalFact.literal.clone();
          }
          ZC_IF_SOME(field, sourceElement.field) {
            elements.add(HirNominalAggregateElement{
                field, sourceElement.destinationType, zc::mv(ZC_ASSERT_NONNULL(elementValue)),
                ZC_ASSERT_NONNULL(elementSpan).clone(), zc::mv(elementErasure),
                zc::mv(concreteElements)});
          }
        }
        HirNominalAggregateExpression aggregateExpr{
            HirNodeId(),
            sourceAggregate.kind.variant().get<checker::checked::NominalAggregate>().definition,
            aggregateType,
            zc::mv(elements),
            HirValueCategory::Value,
            ZC_ASSERT_NONNULL(aggregateInitSpan).clone()};
        // Build the field projection (zoo.animal).
        auto memberIndex = factIndex(facts.members(), fieldReadInit);
        auto placeIndex = factIndex(facts.places(), fieldReadInit);
        if (memberIndex == zc::none || placeIndex == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t memberSlot = 0;
        size_t placeSlot = 0;
        ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
        ZC_IF_SOME(index, placeIndex) { placeSlot = index; }
        const auto& projMember = facts.members().entries()[memberSlot].value;
        const auto& projPlace = facts.places().entries()[placeSlot].value;
        const auto& projRoot = projPlace.root.variant();
        if (projMember.node != fieldReadInit || projMember.receiverType != aggregateType ||
            projMember.memberType != fieldReadType || projMember.adjustment != zc::none ||
            !projRoot.is<checker::checked::OwnerLocalPlaceRoot>() ||
            projRoot.get<checker::checked::OwnerLocalPlaceRoot>().binding !=
                ZC_ASSERT_NONNULL(aggregateBinding) ||
            projPlace.projections.size() != 1 ||
            !projPlace.projections[0].variant().is<checker::checked::FieldProjection>() ||
            projPlace.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                projMember.member ||
            projPlace.type != fieldReadType || !projPlace.movable) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        HirLocalFieldProjectionExpression fieldProjection{
            HirNodeId(),
            HirLocalId(),
            projMember.member,
            projMember.receiverType,
            projMember.memberType,
            HirValueCategory::Place,
            ZC_ASSERT_NONNULL(fieldReadInitSpan).clone()};
        // Build the receiver call (a.speak()).
        const ast::NodeId callNode = shape.value;
        const auto& sourceCall = tree.node(callNode);
        auto callSpan = bound.parsedModule().spanFor(sourceCall.range);
        const ast::NodeId calleeNode(sourceCall.payload.words[ast::kCallExpressionCalleeWord]);
        const ast::NodeList typeArguments{
            sourceCall.payload.words[ast::kCallExpressionTypeArgsFirstWord],
            sourceCall.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
        const ast::NodeList arguments{sourceCall.payload.words[ast::kCallExpressionArgsFirstWord],
                                      sourceCall.payload.words[ast::kCallExpressionArgsSizeWord]};
        if (!tree.contains(calleeNode) ||
            tree.node(calleeNode).kind != ast::SyntaxKind::MemberExpression ||
            static_cast<ast::MemberAccessKind>(
                tree.node(calleeNode).payload.words[ast::kMemberExpressionAccessWord]) !=
                ast::MemberAccessKind::Dot ||
            !tree.contains(typeArguments) || !typeArguments.empty() || !tree.contains(arguments) ||
            !arguments.empty() || callSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const ast::NodeId receiverNode(
            tree.node(calleeNode).payload.words[ast::kMemberExpressionObjectWord]);
        auto receiverTypeIndex = factIndex(facts.nodeTypes(), receiverNode);
        auto calleeMemberIndex = factIndex(facts.members(), calleeNode);
        auto calleeTypeIndex = factIndex(facts.nodeTypes(), calleeNode);
        auto checkedCallIndex = factIndex(facts.calls(), callNode);
        auto callKey = checkedNodeKey(tree, bound.parsedModule(), callNode);
        auto dispatchIndex = callKey == zc::none
                                 ? zc::none
                                 : dispatchFactIndex(checkedModule.dispatchFacts().facts(),
                                                     ZC_ASSERT_NONNULL(callKey));
        if (!tree.contains(receiverNode) ||
            tree.node(receiverNode).kind != ast::SyntaxKind::IdentExpr ||
            receiverTypeIndex == zc::none || calleeMemberIndex == zc::none ||
            calleeTypeIndex == zc::none || checkedCallIndex == zc::none || callKey == zc::none ||
            dispatchIndex == zc::none || callSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t receiverTypeSlot = 0;
        size_t calleeMemberSlot = 0;
        size_t calleeTypeSlot = 0;
        size_t checkedCallSlot = 0;
        size_t dispatchSlot = 0;
        ZC_IF_SOME(index, receiverTypeIndex) { receiverTypeSlot = index; }
        ZC_IF_SOME(index, calleeMemberIndex) { calleeMemberSlot = index; }
        ZC_IF_SOME(index, calleeTypeIndex) { calleeTypeSlot = index; }
        ZC_IF_SOME(index, checkedCallIndex) { checkedCallSlot = index; }
        ZC_IF_SOME(index, dispatchIndex) { dispatchSlot = index; }
        const auto& calleeMember = facts.members().entries()[calleeMemberSlot].value;
        const auto& invocation = facts.calls().entries()[checkedCallSlot].value.invocation;
        const auto& selected = invocation.selected.variant();
        const auto& dispatch = checkedModule.dispatchFacts().facts()[dispatchSlot];
        const auto& target = dispatch.fact.target.variant();
        const auto& transform = dispatch.fact.resultTransform.variant();
        if (!selected.is<checker::checked::DynMethodCallable>() ||
            !target.is<checker::dispatch::DynMethodTarget>() ||
            !transform.is<checker::dispatch::IdentityResultTransform>() ||
            selected.get<checker::checked::DynMethodCallable>().method != calleeMember.member ||
            target.get<checker::dispatch::DynMethodTarget>().method != calleeMember.member ||
            calleeMember.node != calleeNode ||
            calleeMember.memberType != facts.nodeTypes().entries()[calleeTypeSlot].value ||
            calleeMember.adjustment != zc::none ||
            invocation.calleeType != calleeMember.memberType ||
            invocation.successType != nodeType.value || invocation.resultType != nodeType.value ||
            invocation.receiver == zc::none || invocation.receiverMode == zc::none ||
            invocation.receiverAdjustment == zc::none || invocation.arguments.size() != 0 ||
            invocation.substitutions != zc::none || invocation.witnesses != zc::none ||
            invocation.raises != zc::none || dispatch.fact.receiver == zc::none ||
            dispatch.fact.arguments.size() != 0 || dispatch.fact.successType != nodeType.value ||
            dispatch.fact.resultType != nodeType.value || dispatch.fact.substitutions != zc::none ||
            dispatch.fact.witnesses != zc::none || dispatch.fact.raises != zc::none ||
            !sameSpan(facts.calls().entries()[checkedCallSlot].value.sourceSpan,
                      ZC_ASSERT_NONNULL(callSpan)) ||
            !sameSpan(dispatch.fact.sourceSpan, ZC_ASSERT_NONNULL(callSpan))) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        ZC_IF_SOME(receiver, invocation.receiver) {
          ZC_IF_SOME(mode, invocation.receiverMode) {
            ZC_IF_SOME(adjustment, invocation.receiverAdjustment) {
              const bool sharedCall = mode == checker::checked::ReceiverMode::Shared;
              const auto expectedMutability = sharedCall ? type::semantic::Mutability::Const
                                                         : type::semantic::Mutability::Mutable;
              const auto expectedMode = sharedCall ? checker::checked::ReceiverMode::Shared
                                                   : checker::checked::ReceiverMode::Mutable;
              const auto expectedStep =
                  sharedCall ? checker::checked::ReceiverAdjustmentStep::BorrowShared
                             : checker::checked::ReceiverAdjustmentStep::BorrowMutable;
              if (mode != checker::checked::ReceiverMode::Shared &&
                  mode != checker::checked::ReceiverMode::Mutable) {
                return rejectHirCapability<HirModuleCandidate>(
                    definition.definition, registries,
                    ir::IrFailureKind::UnsupportedSourceConstruct,
                    ZC_ASSERT_NONNULL(callSpan).clone());
              }
              auto receiverParameter = checkedModule.semanticTypes().get(receiver.parameterType);
              if (!receiverParameter.is<type::SemanticTypeLookup>() ||
                  !receiverParameter.get<type::SemanticTypeLookup>()
                       .data()
                       .is<type::semantic::ReferenceTypeData>() ||
                  receiverParameter.get<type::SemanticTypeLookup>()
                          .data()
                          .get<type::semantic::ReferenceTypeData>()
                          .mutability != expectedMutability ||
                  receiverParameter.get<type::SemanticTypeLookup>()
                          .data()
                          .get<type::semantic::ReferenceTypeData>()
                          .referent != receiver.sourceType) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              if (receiver.sourceNode != receiverNode ||
                  receiver.sourceType != facts.nodeTypes().entries()[receiverTypeSlot].value ||
                  receiver.adjustment != zc::none || mode != expectedMode ||
                  adjustment.source != receiver.sourceType ||
                  adjustment.destination != receiver.parameterType ||
                  adjustment.steps.size() != 1 || adjustment.steps[0] != expectedStep) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              auto receiverSpan = bound.parsedModule().spanFor(tree.node(receiverNode).range);
              if (receiverSpan == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              zc::Vector<checker::checked::ReceiverAdjustmentStep> steps;
              steps.add(expectedStep);
              HirLocalReferenceExpression receiverReference{
                  HirNodeId(), HirLocalId(), fieldReadType, HirValueCategory::Place,
                  ZC_ASSERT_NONNULL(receiverSpan).clone()};
              HirReceiverCallExpression receiverCall{
                  HirNodeId(),
                  HirNodeId(),
                  calleeMember.member,
                  invocation.calleeType,
                  receiver.sourceType,
                  receiver.parameterType,
                  mode,
                  zc::mv(steps),
                  invocation.resultType,
                  zc::Vector<HirDirectCallArgument>{},
                  ZC_ASSERT_NONNULL(callSpan).clone(),
                  zc::Maybe<identity::DefId>(
                      selected.get<checker::checked::DynMethodCallable>().interface)};
              dynFieldAccessReturn = PendingDynFieldAccessReturn{
                  HirLocalBinding{HirNodeId(), HirLocalId(), aggregateType, HirNodeId(),
                                  ZC_ASSERT_NONNULL(aggregateLetSpan).clone(),
                                  ZC_ASSERT_NONNULL(aggregateInitSpan).clone()},
                  zc::mv(aggregateExpr),
                  HirLocalBinding{HirNodeId(), HirLocalId(), fieldReadType, HirNodeId(),
                                  ZC_ASSERT_NONNULL(fieldReadLetSpan).clone(),
                                  ZC_ASSERT_NONNULL(fieldReadInitSpan).clone()},
                  zc::mv(fieldProjection),
                  zc::mv(receiverReference),
                  zc::mv(receiverCall)};
            }
          }
        }
        if (dynFieldAccessReturn == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        pendingFunctions.add(PendingFunctionDeclaration{
            definition.definition, definition.node, callable.success, zc::mv(parameters),
            zc::mv(methodReceiver), zc::mv(visibilityValue), linkageValue,
            definition.source.clone(), bodySpanValue.clone(), returnSpanValue.clone(),
            valueSpanValue.clone(), zc::mv(orderingKey), shape.body, shape.returnStatement,
            shape.value, zc::mv(unsafeBlockSpan),
            DynFieldAccessReturn{zc::mv(ZC_ASSERT_NONNULL(dynFieldAccessReturn))}});
        continue;
      }
      if (shape.returnsLocal) {  // A scalar-local method body is admitted only through a shared
                                 // receiver.
        // A mutable receiver without an admitted receiver-field write is
        // well-formed source the current lowering does not emit; drain the
        // owning definition with the capability code rather than an invariant.
        if (methodReceiver != zc::none) {
          const auto& methodReceiverValue = ZC_ASSERT_NONNULL(methodReceiver);
          auto receiverTypeLookup = checkedModule.semanticTypes().get(methodReceiverValue.type);
          if (!receiverTypeLookup.is<type::SemanticTypeLookup>() ||
              !receiverTypeLookup.get<type::SemanticTypeLookup>()
                   .data()
                   .is<type::semantic::ReferenceTypeData>() ||
              receiverTypeLookup.get<type::SemanticTypeLookup>()
                      .data()
                      .get<type::semantic::ReferenceTypeData>()
                      .mutability != type::semantic::Mutability::Const) {
            return rejectHirCapability<HirModuleCandidate>(
                definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
                definition.source.clone());
          }
        }
        auto localBinding = resolvedOwnerLocal(bound.bindings(), shape.localReference);
        auto patternSpan = bound.parsedModule().spanFor(tree.node(shape.localPattern).range);
        if (localBinding == zc::none || patternSpan == zc::none ||
            !ownerLocalMatches(bound.definitions(), ZC_ASSERT_NONNULL(localBinding),
                               shape.localPattern, tree)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        identity::SemanticTypeId localType = nodeType.value;
        if (shape.returnsLocalBorrow) {
          auto borrowTypeLookup = checkedModule.semanticTypes().get(nodeType.value);
          if (!borrowTypeLookup.is<type::SemanticTypeLookup>() ||
              !borrowTypeLookup.get<type::SemanticTypeLookup>()
                   .data()
                   .is<type::semantic::ReferenceTypeData>()) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          localType = borrowTypeLookup.get<type::SemanticTypeLookup>()
                          .data()
                          .get<type::semantic::ReferenceTypeData>()
                          .referent;
        }
        if (shape.returnsLocalField) {
          auto memberIndex = factIndex(facts.members(), shape.value);
          if (memberIndex == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t memberSlot = 0;
          ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
          const auto& member = facts.members().entries()[memberSlot].value;
          if (member.node != shape.value || member.memberType != nodeType.value ||
              member.adjustment != zc::none ||
              !typeExists(member.receiverType, checkedModule.semanticTypes())) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          localType = member.receiverType;
        }
        zc::Maybe<HirNodeId> noInitializer;
        zc::Maybe<identity::SourceSpan> noInitializerSpan;
        local = HirLocalBinding{HirNodeId(),
                                HirLocalId(),
                                localType,
                                zc::mv(noInitializer),
                                ZC_ASSERT_NONNULL(patternSpan).clone(),
                                zc::mv(noInitializerSpan)};
        if (!shape.returnsLocalField && !shape.returnsLocalReborrow && !shape.returnsLocalBorrow &&
            !shape.returnsDirectAggregateCall && !shape.returnsDirectScalarLocalCall &&
            !shape.returnsDirectExistentialCall && !shape.returnsArrayIndexFold &&
            !shape.returnsArrayLengthFold && !shape.returnsPrimitiveUnary) {
          localReference =
              HirLocalReferenceExpression{HirNodeId(), HirLocalId(), nodeType.value,
                                          HirValueCategory::Place, valueSpanValue.clone()};
        }
        ZC_IF_SOME(initializer, shape.localInitializer) {
          auto initializerTypeIndex = factIndex(facts.nodeTypes(), initializer);
          auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
          if (initializerTypeIndex == zc::none || initializerSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t initializerTypeSlot = 0;
          ZC_IF_SOME(index, initializerTypeIndex) { initializerTypeSlot = index; }
          const auto& initializerType = facts.nodeTypes().entries()[initializerTypeSlot].value;
          if ((!shape.returnsLocalField && !shape.returnsReceiverCall &&
               !shape.returnsDirectAggregateCall && !shape.returnsDirectScalarLocalCall &&
               !shape.returnsDirectExistentialCall && !shape.returnsLocalBorrow &&
               !shape.returnsArrayIndexFold && !shape.returnsArrayLengthFold &&
               initializerType != nodeType.value) ||
              (!shape.returnsLocalField && !shape.returnsReceiverCall &&
               !shape.returnsDirectAggregateCall && !shape.returnsDirectScalarLocalCall &&
               !shape.returnsDirectExistentialCall && !shape.returnsLocalBorrow &&
               !shape.returnsArrayIndexFold && !shape.returnsArrayLengthFold &&
               initializerType != callable.success) ||
              (shape.returnsLocalBorrow && initializerType != localType)) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          zc::Maybe<HirNodeId> initializerNode;
          initializerNode = HirNodeId();
          zc::Maybe<identity::SourceSpan> initializerSource;
          ZC_IF_SOME(value, initializerSpan) { initializerSource = value.clone(); }
          // A call-argument-erase initializer carries a single DynErase
          // coercion: the binding stores the existential (destination) type
          // while the aggregate expression keeps the concrete (source) type.
          identity::SemanticTypeId bindingType = initializerType;
          zc::Maybe<HirErasureRecord> erasure;
          for (const auto& erased : callArgumentEraseInitializers) {
            if (erased != initializer) continue;
            auto coercionIndex = factIndex(facts.coercions(), initializer);
            if (coercionIndex == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            const auto& coercion =
                facts.coercions().entries()[ZC_ASSERT_NONNULL(coercionIndex)].value;
            if (!isSingleDynEraseAdjustment(coercion) || coercion.source != initializerType) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto& eraseStep =
                coercion.steps[0].variant().get<checker::checked::DynEraseStep>();
            bindingType = coercion.destination;
            erasure = HirErasureRecord{coercion.source, coercion.destination, eraseStep.impl,
                                       eraseStep.interface.interface};
            break;
          }
          local = HirLocalBinding{HirNodeId(),
                                  HirLocalId(),
                                  bindingType,
                                  zc::mv(initializerNode),
                                  ZC_ASSERT_NONNULL(patternSpan).clone(),
                                  zc::mv(initializerSource),
                                  zc::mv(erasure)};
          if (!shape.returnsLocalField && !shape.returnsLocalReborrow &&
              !shape.returnsLocalBorrow && !shape.returnsDirectAggregateCall &&
              !shape.returnsDirectScalarLocalCall && !shape.returnsDirectExistentialCall &&
              !shape.returnsArrayIndexFold && !shape.returnsArrayLengthFold &&
              !shape.returnsPrimitiveUnary) {
            identity::SourceSpan referenceSpan = valueSpanValue.clone();
            if (shape.returnsReceiverCall) {
              const auto& sourceCall = tree.node(shape.value);
              const ast::NodeId calleeNode(
                  sourceCall.payload.words[ast::kCallExpressionCalleeWord]);
              const ast::NodeId receiverNode(
                  tree.node(calleeNode).payload.words[ast::kMemberExpressionObjectWord]);
              auto receiverSpan = bound.parsedModule().spanFor(tree.node(receiverNode).range);
              if (receiverSpan == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              referenceSpan = ZC_ASSERT_NONNULL(receiverSpan).clone();
            }
            localReference =
                HirLocalReferenceExpression{HirNodeId(), HirLocalId(), initializerType,
                                            HirValueCategory::Place, zc::mv(referenceSpan)};
          }
          if (isScalarLiteral(tree.node(initializer).kind)) {
            auto literalIndex = factIndex(facts.literals(), initializer);
            if (literalIndex == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& sourceLiteral = facts.literals().entries()[literalSlot].value;
            if (sourceLiteral.type != initializerType ||
                !sameSpan(sourceLiteral.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            literal = sourceLiteral.literal.clone();
          } else if (tree.node(initializer).kind == ast::SyntaxKind::MemberExpression &&
                     static_cast<ast::MemberAccessKind>(
                         tree.node(initializer).payload.words[ast::kMemberExpressionAccessWord]) ==
                         ast::MemberAccessKind::Qualified) {
            // A qualified enum variant access (`Color::Red`) produces a scalar
            // literal fact (the discriminant) in the body checker, so the
            // receiver-call path lowers it exactly like a scalar literal.
            auto literalIndex = factIndex(facts.literals(), initializer);
            if (literalIndex == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& sourceLiteral = facts.literals().entries()[literalSlot].value;
            if (sourceLiteral.type != initializerType ||
                !sameSpan(sourceLiteral.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            literal = sourceLiteral.literal.clone();
          } else if (tree.node(initializer).kind == ast::SyntaxKind::ArrayLiteral) {
            // Array literal initializer: the body checker produces an
            // ArrayAggregate fact. Validate the fact and the element literals;
            // the aggregate itself is not lowered to a HIR expression because
            // the return value is a folded constant index read.
            auto aggregateIndex = factIndex(facts.aggregates(), initializer);
            if (aggregateIndex == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t aggregateSlot = 0;
            ZC_IF_SOME(index, aggregateIndex) { aggregateSlot = index; }
            const auto& sourceAggregate = facts.aggregates().entries()[aggregateSlot].value;
            if (sourceAggregate.node != initializer ||
                !sourceAggregate.kind.variant().is<checker::checked::ArrayAggregate>() ||
                sourceAggregate.resultType != initializerType ||
                !sameSpan(sourceAggregate.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            for (const auto& sourceElement : sourceAggregate.elements) {
              if (sourceElement.field != zc::none ||
                  sourceElement.sourceType != sourceElement.destinationType ||
                  sourceElement.adjustment != zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              auto elementLiteral = factIndex(facts.literals(), sourceElement.sourceNode);
              if (elementLiteral == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              size_t literalSlot = 0;
              ZC_IF_SOME(index, elementLiteral) { literalSlot = index; }
              const auto& literalFact = facts.literals().entries()[literalSlot].value;
              if (literalFact.type != sourceElement.destinationType) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
            }
          } else if (tree.node(initializer).kind == ast::SyntaxKind::StructLiteralExpr ||
                     isEnumConstructionCall(tree, initializer)) {
            auto aggregateIndex = factIndex(facts.aggregates(), initializer);
            if (aggregateIndex == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t aggregateSlot = 0;
            ZC_IF_SOME(index, aggregateIndex) { aggregateSlot = index; }
            const auto& sourceAggregate = facts.aggregates().entries()[aggregateSlot].value;
            if (sourceAggregate.node != initializer ||
                !sourceAggregate.kind.variant().is<checker::checked::NominalAggregate>() ||
                sourceAggregate.resultType != initializerType ||
                !sameSpan(sourceAggregate.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            // The pure aggregate field-return and by-value aggregate-call
            // native slices admit i32 stored fields only; every other element
            // type keeps the owning definition on the per-definition capability
            // drain (ZOM4099). The whole-aggregate return and the field-
            // overwrite arms accept a mixed-type aggregate through their own
            // construction and are not restricted here.
            if ((shape.returnsLocalField && shape.localWrites.size == 0) ||
                shape.returnsDirectAggregateCall) {
              for (const auto& sourceElement : sourceAggregate.elements) {
                if (!isI32SemanticType(checkedModule.semanticTypes(),
                                       sourceElement.destinationType)) {
                  return rejectHirCapability<HirModuleCandidate>(
                      definition.definition, registries,
                      ir::IrFailureKind::UnsupportedSourceConstruct,
                      ZC_ASSERT_NONNULL(initializerSpan).clone());
                }
              }
            }
            zc::Vector<HirNominalAggregateElement> elements;
            for (const auto& sourceElement : sourceAggregate.elements) {
              if (sourceElement.field == zc::none ||
                  sourceElement.sourceType != sourceElement.destinationType ||
                  sourceElement.adjustment != zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              auto elementLiteral = factIndex(facts.literals(), sourceElement.sourceNode);
              auto elementSpan =
                  bound.parsedModule().spanFor(tree.node(sourceElement.sourceNode).range);
              if (elementLiteral == zc::none || elementSpan == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              size_t literalSlot = 0;
              ZC_IF_SOME(index, elementLiteral) { literalSlot = index; }
              const auto& literalFact = facts.literals().entries()[literalSlot].value;
              if (literalFact.type != sourceElement.destinationType ||
                  !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(elementSpan))) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              ZC_IF_SOME(field, sourceElement.field) {
                elements.add(HirNominalAggregateElement{field, sourceElement.destinationType,
                                                        literalFact.literal.clone(),
                                                        ZC_ASSERT_NONNULL(elementSpan).clone()});
              }
            }
            aggregate = HirNominalAggregateExpression{
                HirNodeId(),
                sourceAggregate.kind.variant().get<checker::checked::NominalAggregate>().definition,
                initializerType,
                zc::mv(elements),
                HirValueCategory::Value,
                ZC_ASSERT_NONNULL(initializerSpan).clone()};
            aggregateIsEnumConstruction = isEnumConstructionCall(tree, initializer);
          } else if (shape.returnsDirectExistentialCall &&
                     tree.node(initializer).kind == ast::SyntaxKind::CallExpression) {
            // The existential-call initializer is a direct call returning the
            // existential type. The call already produces the fat-pointer
            // carrier, so no erasure coercion is needed.
            const auto& initCallNode = tree.node(initializer);
            const ast::NodeId initCalleeNode(
                initCallNode.payload.words[ast::kCallExpressionCalleeWord]);
            const ast::NodeList initTypeArguments{
                initCallNode.payload.words[ast::kCallExpressionTypeArgsFirstWord],
                initCallNode.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
            const ast::NodeList initArguments{
                initCallNode.payload.words[ast::kCallExpressionArgsFirstWord],
                initCallNode.payload.words[ast::kCallExpressionArgsSizeWord]};
            if (!tree.contains(initCalleeNode) ||
                tree.node(initCalleeNode).kind != ast::SyntaxKind::IdentExpr ||
                !tree.contains(initTypeArguments) || !initTypeArguments.empty() ||
                !tree.contains(initArguments) || tree.list(initArguments).size() != 0) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            auto initCallIndex = factIndex(facts.calls(), initializer);
            auto initCalleeTypeIndex = factIndex(facts.nodeTypes(), initCalleeNode);
            auto initCallKey = checkedNodeKey(tree, bound.parsedModule(), initializer);
            if (initCallIndex == zc::none || initCalleeTypeIndex == zc::none ||
                initCallKey == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            auto initDispatchIndex = dispatchFactIndex(checkedModule.dispatchFacts().facts(),
                                                       ZC_ASSERT_NONNULL(initCallKey));
            auto initCallee = resolvedDefinition(bound.bindings(), initCalleeNode);
            if (initDispatchIndex == zc::none || initCallee == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t initCallSlot = 0;
            size_t initCalleeTypeSlot = 0;
            size_t initDispatchSlot = 0;
            ZC_IF_SOME(index, initCallIndex) { initCallSlot = index; }
            ZC_IF_SOME(index, initCalleeTypeIndex) { initCalleeTypeSlot = index; }
            ZC_IF_SOME(index, initDispatchIndex) { initDispatchSlot = index; }
            const auto& initCheckedCall = facts.calls().entries()[initCallSlot].value;
            const auto& initInvocation = initCheckedCall.invocation;
            const auto& initSelected = initInvocation.selected.variant();
            const auto& initDispatch = checkedModule.dispatchFacts().facts()[initDispatchSlot];
            const auto& initTarget = initDispatch.fact.target.variant();
            const auto& initTransform = initDispatch.fact.resultTransform.variant();
            if (!initSelected.is<checker::checked::DirectCallable>() ||
                initSelected.get<checker::checked::DirectCallable>().callee !=
                    ZC_ASSERT_NONNULL(initCallee) ||
                initInvocation.calleeType !=
                    facts.nodeTypes().entries()[initCalleeTypeSlot].value ||
                initInvocation.resultType != initializerType ||
                initInvocation.receiver != zc::none || initInvocation.receiverMode != zc::none ||
                initInvocation.receiverAdjustment != zc::none ||
                initInvocation.arguments.size() != 0 || initInvocation.substitutions != zc::none ||
                initInvocation.witnesses != zc::none || initInvocation.raises != zc::none ||
                !sameSpan(initCheckedCall.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan)) ||
                !initTarget.is<checker::dispatch::DirectTarget>() ||
                initTarget.get<checker::dispatch::DirectTarget>().callee !=
                    ZC_ASSERT_NONNULL(initCallee) ||
                !initTransform.is<checker::dispatch::IdentityResultTransform>() ||
                initDispatch.fact.receiver != zc::none || initDispatch.fact.arguments.size() != 0 ||
                initDispatch.fact.substitutions != zc::none ||
                initDispatch.fact.witnesses != zc::none || initDispatch.fact.raises != zc::none ||
                !sameSpan(initDispatch.fact.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan)) ||
                !isExistentialSemanticType(checkedModule.semanticTypes(), initializerType)) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            zc::Vector<HirDirectCallArgument> noArgs;
            initializerCall = HirDirectCallExpression{HirNodeId(),
                                                      ZC_ASSERT_NONNULL(initCallee),
                                                      initInvocation.calleeType,
                                                      initializerType,
                                                      zc::mv(noArgs),
                                                      ZC_ASSERT_NONNULL(initializerSpan).clone()};
          } else if (tree.node(initializer).kind == ast::SyntaxKind::IdentExpr) {
            auto parameter = resolvedCallableParameter(bound.bindings(), initializer);
            if (parameter == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            ZC_IF_SOME(handle, parameter) {
              auto authority = registries.callableParameter(handle);
              if (authority == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              ZC_IF_SOME(entry, authority) {
                bool matches = false;
                for (const auto& candidate : parameters) {
                  if (candidate.key == entry.key() && candidate.type == initializerType) {
                    matches = true;
                  }
                }
                if (!matches) {
                  return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                       ir::IrFailureKind::InvalidFact, module,
                                                       registries, ordinal + 2);
                }
                parameterReference = HirParameterReferenceExpression{
                    HirNodeId(), entry.key().clone(), initializerType, HirValueCategory::Place,
                    ZC_ASSERT_NONNULL(initializerSpan).clone()};
              }
            }
          }
        }
        if (shape.returnsLocalField) {
          auto memberIndex = factIndex(facts.members(), shape.value);
          auto placeIndex = factIndex(facts.places(), shape.value);
          if (memberIndex == zc::none || placeIndex == zc::none || local == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t memberSlot = 0;
          size_t placeSlot = 0;
          ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
          ZC_IF_SOME(index, placeIndex) { placeSlot = index; }
          const auto& member = facts.members().entries()[memberSlot].value;
          const auto& place = facts.places().entries()[placeSlot].value;
          const auto& root = place.root.variant();
          if (member.node != shape.value || member.receiverType != ZC_ASSERT_NONNULL(local).type ||
              member.memberType != nodeType.value || member.adjustment != zc::none ||
              !root.is<checker::checked::OwnerLocalPlaceRoot>() ||
              root.get<checker::checked::OwnerLocalPlaceRoot>().binding !=
                  ZC_ASSERT_NONNULL(localBinding) ||
              place.projections.size() != 1 ||
              !place.projections[0].variant().is<checker::checked::FieldProjection>() ||
              place.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                  member.member ||
              place.type != nodeType.value || !place.movable) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          localFieldProjection = HirLocalFieldProjectionExpression{
              HirNodeId(),           HirLocalId(),      member.member,
              member.receiverType,   member.memberType, HirValueCategory::Place,
              valueSpanValue.clone()};
        }
        for (size_t writeIndex = 0; writeIndex < shape.localWrites.size; ++writeIndex) {
          auto writeStatement = statementItem(tree, tree.list(shape.localWrites)[writeIndex]);
          if (writeStatement == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ast::NodeId writeStatementNode;
          ZC_IF_SOME(value, writeStatement) { writeStatementNode = value; }
          const ast::NodeId write(
              tree.node(writeStatementNode).payload.words[ast::kExpressionStatementExpressionWord]);
          // A constant-index array element write (`a[0] = <literal>`) is dead
          // when the enclosing return folds from the same array: the read
          // folds to a constant and the write elides at the HIR level. Skip
          // it without contributing to localWrites/localWriteValues.
          if (tree.node(write).kind == ast::SyntaxKind::AssignmentExpr) {
            const ast::NodeId writeTarget(
                tree.node(write).payload.words[ast::kAssignmentExprLhsWord]);
            if (tree.contains(writeTarget) &&
                tree.node(writeTarget).kind == ast::SyntaxKind::IndexExpression) {
              continue;
            }
          }
          // A postfix increment/decrement (`x++` / `x--`) desugars to a binary
          // write (`x = x + 1` / `x = x - 1`). The PostfixExpression node
          // carries the call fact; the binary and its literal operand are
          // synthetic, so the write contributes two node-type facts (the
          // postfix node and its operand) instead of five and no literal fact.
          if (tree.node(write).kind == ast::SyntaxKind::PostfixExpression) {
            const auto postfixOp = static_cast<ast::PostfixOperatorKind>(
                tree.node(write).payload.words[ast::kPostfixExpressionOpWord]);
            if (postfixOp != ast::PostfixOperatorKind::Increment &&
                postfixOp != ast::PostfixOperatorKind::Decrement) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const ast::NodeId postfixTarget(
                tree.node(write).payload.words[ast::kPostfixExpressionOperandWord]);
            auto postfixTypeIndex = factIndex(facts.nodeTypes(), write);
            auto postfixTargetTypeIndex = factIndex(facts.nodeTypes(), postfixTarget);
            auto postfixCallIndex = factIndex(facts.calls(), write);
            auto postfixSpan = bound.parsedModule().spanFor(tree.node(write).range);
            auto postfixTargetBinding = resolvedOwnerLocal(bound.bindings(), postfixTarget);
            auto postfixReturnBinding = resolvedOwnerLocal(bound.bindings(), shape.localReference);
            if (postfixTypeIndex == zc::none || postfixTargetTypeIndex == zc::none ||
                postfixCallIndex == zc::none || postfixSpan == zc::none ||
                postfixTargetBinding == zc::none || postfixReturnBinding == zc::none ||
                postfixTargetBinding != postfixReturnBinding) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t postfixTypeSlot = 0;
            size_t postfixTargetSlot = 0;
            ZC_IF_SOME(index, postfixTypeIndex) { postfixTypeSlot = index; }
            ZC_IF_SOME(index, postfixTargetTypeIndex) { postfixTargetSlot = index; }
            const auto& postfixType = facts.nodeTypes().entries()[postfixTypeSlot].value;
            const auto& postfixTargetType = facts.nodeTypes().entries()[postfixTargetSlot].value;
            if (local == zc::none || postfixType != postfixTargetType) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            size_t postfixCallSlot = 0;
            ZC_IF_SOME(index, postfixCallIndex) { postfixCallSlot = index; }
            const auto& postfixCallFact = facts.calls().entries()[postfixCallSlot].value;
            const auto& postfixCall = postfixCallFact.invocation;
            const auto& postfixSelected = postfixCall.selected.variant();
            if (!postfixSelected.is<checker::checked::PrimitiveCallable>() ||
                postfixCallFact.node != write || postfixCall.calleeType != postfixTargetType ||
                postfixCall.receiver != zc::none || postfixCall.receiverMode != zc::none ||
                postfixCall.receiverAdjustment != zc::none || postfixCall.arguments.size() != 1 ||
                postfixCall.arguments[0].sourceNode != postfixTarget ||
                postfixCall.arguments[0].sourceType != postfixTargetType ||
                postfixCall.successType != postfixTargetType ||
                postfixCall.resultType != postfixTargetType ||
                postfixCall.substitutions != zc::none || postfixCall.witnesses != zc::none ||
                postfixCall.raises != zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto postfixOperation =
                postfixSelected.get<checker::checked::PrimitiveCallable>().operation;
            if (postfixOperation != checker::PrimitiveOperation::PostIncrement &&
                postfixOperation != checker::PrimitiveOperation::PostDecrement) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto binaryOperation =
                postfixOperation == checker::PrimitiveOperation::PostIncrement
                    ? checker::PrimitiveOperation::Add
                    : checker::PrimitiveOperation::Sub;
            // The synthetic literal 1 of the operand's integer type. It has no
            // AST node and therefore no checked literal fact.
            auto unitMagnitude = zc::heapArray<uint8_t>(1);
            unitMagnitude[0] = 1;
            auto syntheticOne =
                checker::checked::CanonicalConstValue::integer(checker::signature::CanonicalInteger{
                    checker::signature::IntegerSign::NonNegative, zc::mv(unitMagnitude)});
            auto postfixTargetSpan = bound.parsedModule().spanFor(tree.node(postfixTarget).range);
            if (postfixTargetSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            auto leftReference = HirLocalReferenceExpression{
                HirNodeId(), hirLocalId(1), postfixTargetType, HirValueCategory::Place,
                ZC_ASSERT_NONNULL(postfixTargetSpan).clone()};
            auto leftArm = PendingConditionalArm{zc::none,
                                                 zc::none,
                                                 zc::mv(leftReference),
                                                 postfixTargetType,
                                                 ZC_ASSERT_NONNULL(postfixTargetSpan).clone(),
                                                 zc::none,
                                                 postfixTarget};
            auto rightArm =
                PendingConditionalArm{zc::mv(syntheticOne), zc::none, zc::none, postfixTargetType,
                                      ZC_ASSERT_NONNULL(postfixSpan).clone()};
            PendingLocalWriteValue postfixWriteValue;
            postfixWriteValue.binary =
                PendingLocalWriteBinary{zc::mv(leftArm),
                                        zc::mv(rightArm),
                                        postfixTargetType,
                                        postfixTargetType,
                                        binaryOperation,
                                        ZC_ASSERT_NONNULL(postfixSpan).clone(),
                                        false,
                                        false,
                                        write};
            ZC_ASSERT_NONNULL(postfixWriteValue.binary).isIncrementDesugar = true;
            localWrites.add(HirLocalWriteStatement{
                HirNodeId(), HirLocalId(), zc::none, postfixTargetType, HirNodeId(),
                HirLocalWriteKind::Overwrite, ZC_ASSERT_NONNULL(postfixSpan).clone(),
                ZC_ASSERT_NONNULL(postfixSpan).clone()});
            localWriteValues.add(zc::mv(postfixWriteValue));
            continue;
          }
          // A prefix increment/decrement (`++x` / `--x`) desugars to a binary
          // write (`x = x + 1` / `x = x - 1`), the same as the postfix form.
          // The UnaryExpression node carries the call fact; the binary and its
          // literal operand are synthetic, so the write contributes two
          // node-type facts (the unary node and its operand) instead of five
          // and no literal fact.
          if (tree.node(write).kind == ast::SyntaxKind::UnaryExpression) {
            const auto unaryOp = static_cast<ast::UnaryOperatorKind>(
                tree.node(write).payload.words[ast::kUnaryExpressionOpWord]);
            if (unaryOp != ast::UnaryOperatorKind::PreIncrement &&
                unaryOp != ast::UnaryOperatorKind::PreDecrement) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const ast::NodeId unaryTarget(
                tree.node(write).payload.words[ast::kUnaryExpressionOperandWord]);
            auto unaryTypeIndex = factIndex(facts.nodeTypes(), write);
            auto unaryTargetTypeIndex = factIndex(facts.nodeTypes(), unaryTarget);
            auto unaryCallIndex = factIndex(facts.calls(), write);
            auto unarySpan = bound.parsedModule().spanFor(tree.node(write).range);
            auto unaryTargetBinding = resolvedOwnerLocal(bound.bindings(), unaryTarget);
            auto unaryReturnBinding = resolvedOwnerLocal(bound.bindings(), shape.localReference);
            if (unaryTypeIndex == zc::none || unaryTargetTypeIndex == zc::none ||
                unaryCallIndex == zc::none || unarySpan == zc::none ||
                unaryTargetBinding == zc::none || unaryReturnBinding == zc::none ||
                unaryTargetBinding != unaryReturnBinding) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t unaryTypeSlot = 0;
            size_t unaryTargetSlot = 0;
            ZC_IF_SOME(index, unaryTypeIndex) { unaryTypeSlot = index; }
            ZC_IF_SOME(index, unaryTargetTypeIndex) { unaryTargetSlot = index; }
            const auto& unaryType = facts.nodeTypes().entries()[unaryTypeSlot].value;
            const auto& unaryTargetType = facts.nodeTypes().entries()[unaryTargetSlot].value;
            if (local == zc::none || unaryType != unaryTargetType) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            size_t unaryCallSlot = 0;
            ZC_IF_SOME(index, unaryCallIndex) { unaryCallSlot = index; }
            const auto& unaryCallFact = facts.calls().entries()[unaryCallSlot].value;
            const auto& unaryCall = unaryCallFact.invocation;
            const auto& unarySelected = unaryCall.selected.variant();
            if (!unarySelected.is<checker::checked::PrimitiveCallable>() ||
                unaryCallFact.node != write || unaryCall.calleeType != unaryTargetType ||
                unaryCall.receiver != zc::none || unaryCall.receiverMode != zc::none ||
                unaryCall.receiverAdjustment != zc::none || unaryCall.arguments.size() != 1 ||
                unaryCall.arguments[0].sourceNode != unaryTarget ||
                unaryCall.arguments[0].sourceType != unaryTargetType ||
                unaryCall.successType != unaryTargetType ||
                unaryCall.resultType != unaryTargetType || unaryCall.substitutions != zc::none ||
                unaryCall.witnesses != zc::none || unaryCall.raises != zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto unaryOperation =
                unarySelected.get<checker::checked::PrimitiveCallable>().operation;
            if (unaryOperation != checker::PrimitiveOperation::PreIncrement &&
                unaryOperation != checker::PrimitiveOperation::PreDecrement) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto binaryOperation = unaryOperation == checker::PrimitiveOperation::PreIncrement
                                             ? checker::PrimitiveOperation::Add
                                             : checker::PrimitiveOperation::Sub;
            // The synthetic literal 1 of the operand's integer type. It has no
            // AST node and therefore no checked literal fact.
            auto unitMagnitude = zc::heapArray<uint8_t>(1);
            unitMagnitude[0] = 1;
            auto syntheticOne =
                checker::checked::CanonicalConstValue::integer(checker::signature::CanonicalInteger{
                    checker::signature::IntegerSign::NonNegative, zc::mv(unitMagnitude)});
            auto unaryTargetSpan = bound.parsedModule().spanFor(tree.node(unaryTarget).range);
            if (unaryTargetSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            auto leftReference = HirLocalReferenceExpression{
                HirNodeId(), hirLocalId(1), unaryTargetType, HirValueCategory::Place,
                ZC_ASSERT_NONNULL(unaryTargetSpan).clone()};
            auto leftArm = PendingConditionalArm{zc::none,
                                                 zc::none,
                                                 zc::mv(leftReference),
                                                 unaryTargetType,
                                                 ZC_ASSERT_NONNULL(unaryTargetSpan).clone(),
                                                 zc::none,
                                                 unaryTarget};
            auto rightArm =
                PendingConditionalArm{zc::mv(syntheticOne), zc::none, zc::none, unaryTargetType,
                                      ZC_ASSERT_NONNULL(unarySpan).clone()};
            PendingLocalWriteValue unaryWriteValue;
            unaryWriteValue.binary = PendingLocalWriteBinary{zc::mv(leftArm),
                                                             zc::mv(rightArm),
                                                             unaryTargetType,
                                                             unaryTargetType,
                                                             binaryOperation,
                                                             ZC_ASSERT_NONNULL(unarySpan).clone(),
                                                             false,
                                                             false,
                                                             write};
            ZC_ASSERT_NONNULL(unaryWriteValue.binary).isIncrementDesugar = true;
            localWrites.add(HirLocalWriteStatement{
                HirNodeId(), HirLocalId(), zc::none, unaryTargetType, HirNodeId(),
                HirLocalWriteKind::Overwrite, ZC_ASSERT_NONNULL(unarySpan).clone(),
                ZC_ASSERT_NONNULL(unarySpan).clone()});
            localWriteValues.add(zc::mv(unaryWriteValue));
            continue;
          }
          ast::NodeId target;
          ast::NodeId writeValue;
          target = ast::NodeId(tree.node(write).payload.words[ast::kAssignmentExprLhsWord]);
          writeValue = ast::NodeId(tree.node(write).payload.words[ast::kAssignmentExprRhsWord]);
          auto assignmentTypeIndex = factIndex(facts.nodeTypes(), write);
          auto targetTypeIndex = factIndex(facts.nodeTypes(), target);
          auto valueTypeIndex = factIndex(facts.nodeTypes(), writeValue);
          auto assignmentSpan = bound.parsedModule().spanFor(tree.node(write).range);
          auto valueSpan = bound.parsedModule().spanFor(tree.node(writeValue).range);
          // A write value is a scalar literal, an identifier reference (a
          // parameter, resolved below), or a primitive binary operation. Only a
          // literal write consumes a checked literal fact; a reference or binary
          // write consumes none.
          const bool referenceValue = tree.node(writeValue).kind == ast::SyntaxKind::IdentExpr;
          const bool binaryWriteValue = tree.node(writeValue).kind == ast::SyntaxKind::BinaryExpr;
          auto literalIndex = (referenceValue || binaryWriteValue)
                                  ? zc::none
                                  : factIndex(facts.literals(), writeValue);
          ast::NodeId targetReference = target;
          if (tree.node(target).kind == ast::SyntaxKind::MemberExpression) {
            targetReference =
                ast::NodeId(tree.node(target).payload.words[ast::kMemberExpressionObjectWord]);
          }
          auto targetBinding = resolvedOwnerLocal(bound.bindings(), targetReference);
          auto returnBinding = resolvedOwnerLocal(bound.bindings(), shape.localReference);
          if (assignmentTypeIndex == zc::none || targetTypeIndex == zc::none ||
              valueTypeIndex == zc::none ||
              (!referenceValue && !binaryWriteValue && literalIndex == zc::none) ||
              assignmentSpan == zc::none || valueSpan == zc::none || targetBinding == zc::none ||
              returnBinding == zc::none || targetBinding != returnBinding) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t assignmentSlot = 0;
          size_t targetSlot = 0;
          size_t valueSlot = 0;
          ZC_IF_SOME(index, assignmentTypeIndex) { assignmentSlot = index; }
          ZC_IF_SOME(index, targetTypeIndex) { targetSlot = index; }
          ZC_IF_SOME(index, valueTypeIndex) { valueSlot = index; }
          const auto& assignmentType = facts.nodeTypes().entries()[assignmentSlot].value;
          const auto& targetType = facts.nodeTypes().entries()[targetSlot].value;
          const auto& valueType = facts.nodeTypes().entries()[valueSlot].value;
          identity::SemanticTypeId writeType = targetType;
          zc::Maybe<identity::DefId> field;
          if (tree.node(target).kind == ast::SyntaxKind::MemberExpression) {
            auto memberIndex = factIndex(facts.members(), target);
            auto placeIndex = factIndex(facts.places(), target);
            if (memberIndex == zc::none || placeIndex == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t memberSlot = 0;
            size_t placeSlot = 0;
            ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
            ZC_IF_SOME(index, placeIndex) { placeSlot = index; }
            const auto& member = facts.members().entries()[memberSlot].value;
            const auto& place = facts.places().entries()[placeSlot].value;
            if (member.node != target || member.memberType != targetType || place.node != target ||
                place.type != targetType || !place.mutablePlace || place.projections.size() != 1 ||
                !place.projections[0].variant().is<checker::checked::FieldProjection>() ||
                place.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                    member.member) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            field = member.member;
          }
          if (local == zc::none || assignmentType != writeType || targetType != writeType ||
              valueType != writeType) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          // A compound assignment (`x += 1`) desugars to a binary write
          // (`x = x + 1`). The AssignmentExpr node carries the target and value
          // node-type facts (three instead of five) and the binary operation
          // has no checked call fact, so the digest equations subtract two
          // node-type facts and one call fact per desugared write.
          const auto writeOp = static_cast<ast::AssignmentOperatorKind>(
              tree.node(write).payload.words[ast::kAssignmentExprOpWord]);
          auto compoundOperation = compoundAssignmentBinaryOperation(writeOp);
          // Build the per-write value: a scalar literal consumes its checked
          // literal fact; a parameter reference resolves the parameter and
          // matches its type; a primitive binary validates its own checked call
          // fact (keyed on the write value node) and builds its two operands,
          // exactly like the primitive-binary initializer path.
          PendingLocalWriteValue writeValueRecord;
          if (compoundOperation != zc::none) {
            if (field != zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            auto targetSpan = bound.parsedModule().spanFor(tree.node(target).range);
            if (targetSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            auto leftReference = HirLocalReferenceExpression{HirNodeId(), hirLocalId(1), writeType,
                                                             HirValueCategory::Place,
                                                             ZC_ASSERT_NONNULL(targetSpan).clone()};
            auto leftArm = PendingConditionalArm{zc::none,
                                                 zc::none,
                                                 zc::mv(leftReference),
                                                 writeType,
                                                 ZC_ASSERT_NONNULL(targetSpan).clone(),
                                                 zc::none,
                                                 target};
            zc::Maybe<PendingConditionalArm> rightArm;
            if (referenceValue) {
              auto parameter = resolvedCallableParameter(bound.bindings(), writeValue);
              if (parameter == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              ZC_IF_SOME(handle, parameter) {
                auto authority = registries.callableParameter(handle);
                if (authority == zc::none) {
                  return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                       ir::IrFailureKind::MissingRequiredFact,
                                                       module, registries, ordinal + 2);
                }
                ZC_IF_SOME(entry, authority) {
                  bool matches = false;
                  for (const auto& candidate : parameters) {
                    if (candidate.key == entry.key() && candidate.type == writeType) {
                      matches = true;
                    }
                  }
                  if (!matches) {
                    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                         ir::IrFailureKind::InvalidFact, module,
                                                         registries, ordinal + 2);
                  }
                  auto reference = HirParameterReferenceExpression{
                      HirNodeId(), entry.key().clone(), writeType, HirValueCategory::Place,
                      ZC_ASSERT_NONNULL(valueSpan).clone()};
                  rightArm = PendingConditionalArm{zc::none,
                                                   zc::mv(reference),
                                                   zc::none,
                                                   writeType,
                                                   ZC_ASSERT_NONNULL(valueSpan).clone(),
                                                   zc::none,
                                                   writeValue};
                }
              }
            } else {
              if (literalIndex == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              size_t rhsLiteralSlot = 0;
              ZC_IF_SOME(index, literalIndex) { rhsLiteralSlot = index; }
              const auto& rhsLiteral = facts.literals().entries()[rhsLiteralSlot].value;
              if (rhsLiteral.type != writeType) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              rightArm = PendingConditionalArm{
                  rhsLiteral.literal.clone(),           zc::none, zc::none,  writeType,
                  ZC_ASSERT_NONNULL(valueSpan).clone(), zc::none, writeValue};
            }
            if (rightArm == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            writeValueRecord.binary =
                PendingLocalWriteBinary{zc::mv(leftArm),
                                        zc::mv(ZC_ASSERT_NONNULL(rightArm)),
                                        writeType,
                                        writeType,
                                        ZC_ASSERT_NONNULL(compoundOperation),
                                        ZC_ASSERT_NONNULL(assignmentSpan).clone(),
                                        false,
                                        false,
                                        write};
            ZC_ASSERT_NONNULL(writeValueRecord.binary).isCompoundAssignmentDesugar = true;
          } else if (binaryWriteValue) {
            if (field != zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const ast::NodeId binaryLeft(
                tree.node(writeValue).payload.words[ast::kBinaryExprLhsWord]);
            const ast::NodeId binaryRight(
                tree.node(writeValue).payload.words[ast::kBinaryExprRhsWord]);
            auto callIndex = factIndex(facts.calls(), writeValue);
            auto leftSpan = bound.parsedModule().spanFor(tree.node(binaryLeft).range);
            auto rightSpan = bound.parsedModule().spanFor(tree.node(binaryRight).range);
            if (callIndex == zc::none || leftSpan == zc::none || rightSpan == zc::none ||
                !tree.contains(binaryLeft) || !tree.contains(binaryRight)) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t callSlot = 0;
            ZC_IF_SOME(index, callIndex) { callSlot = index; }
            const auto& callFact = facts.calls().entries()[callSlot].value;
            const auto& call = callFact.invocation;
            const auto& selected = call.selected.variant();
            if (!selected.is<checker::checked::PrimitiveCallable>()) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto operation = selected.get<checker::checked::PrimitiveCallable>().operation;
            const bool comparison = isScalarComparisonOperation(operation);
            const bool arithmetic = isScalarArithmeticOperation(operation);
            // The operand type is the shared argument type; a comparison yields
            // the (bool) result while an arithmetic operator yields the operand
            // type. The write type is the target local type, so an arithmetic
            // write requires operandType == writeType.
            const auto binaryOperandType =
                call.arguments.size() == 2 ? call.arguments[0].sourceType : writeType;
            const bool operationSupported =
                comparison || (arithmetic && writeType == binaryOperandType);
            if (!operationSupported || callFact.node != writeValue ||
                call.calleeType != binaryOperandType || call.receiver != zc::none ||
                call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
                call.arguments.size() != 2 || call.arguments[0].sourceNode != binaryLeft ||
                call.arguments[0].sourceType != binaryOperandType ||
                call.arguments[1].sourceNode != binaryRight ||
                call.arguments[1].sourceType != binaryOperandType ||
                call.successType != writeType || call.resultType != writeType ||
                call.substitutions != zc::none || call.witnesses != zc::none ||
                call.raises != zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            // Build one binary operand recursively. A leaf operand is a scalar
            // literal, a resolved parameter, or the written user local
            // (`x = x + 1`). A nested binary operand (`x = (a + b) * c`)
            // validates its own call fact and recurses into its children,
            // producing a PendingConditionalArmBinary. Both operands share the
            // derived operand type; at least one is a non-literal (a
            // literal-vs-literal binary is rejected at admission).
            bool operandRejected = false;
            auto buildWriteOperandImpl =
                [&](auto&& self, ast::NodeId operandNode,
                    const identity::SourceSpan& operandSpan) -> zc::Maybe<PendingConditionalArm> {
              const bool operandIsLiteral = isScalarLiteral(tree.node(operandNode).kind);
              if (!operandIsLiteral) {
                // A nested binary operand: validate its call fact and recurse
                // into its operands.
                if (tree.node(operandNode).kind == ast::SyntaxKind::BinaryExpr) {
                  auto nestedCallIndex = factIndex(facts.calls(), operandNode);
                  if (nestedCallIndex == zc::none) {
                    operandRejected = true;
                    return zc::none;
                  }
                  size_t nestedCallSlot = 0;
                  ZC_IF_SOME(index, nestedCallIndex) { nestedCallSlot = index; }
                  const auto& nestedCallFact = facts.calls().entries()[nestedCallSlot].value;
                  const auto& nestedCall = nestedCallFact.invocation;
                  const auto& nestedSelected = nestedCall.selected.variant();
                  if (!nestedSelected.is<checker::checked::PrimitiveCallable>()) {
                    operandRejected = true;
                    return zc::none;
                  }
                  const auto nestedOperation =
                      nestedSelected.get<checker::checked::PrimitiveCallable>().operation;
                  const bool nestedComparison = isScalarComparisonOperation(nestedOperation);
                  const bool nestedArithmetic = isScalarArithmeticOperation(nestedOperation);
                  if (!nestedComparison && !nestedArithmetic) {
                    operandRejected = true;
                    return zc::none;
                  }
                  const auto nestedOperandType = nestedCall.arguments.size() == 2
                                                     ? nestedCall.arguments[0].sourceType
                                                     : binaryOperandType;
                  const bool nestedOperationSupported =
                      nestedComparison ||
                      (nestedArithmetic && binaryOperandType == nestedOperandType);
                  if (!nestedOperationSupported || nestedCallFact.node != operandNode ||
                      nestedCall.calleeType != nestedOperandType ||
                      nestedCall.receiver != zc::none || nestedCall.receiverMode != zc::none ||
                      nestedCall.receiverAdjustment != zc::none ||
                      nestedCall.arguments.size() != 2 ||
                      nestedCall.arguments[0].sourceType != nestedOperandType ||
                      nestedCall.arguments[1].sourceType != nestedOperandType ||
                      nestedCall.successType != binaryOperandType ||
                      nestedCall.resultType != binaryOperandType ||
                      nestedCall.substitutions != zc::none || nestedCall.witnesses != zc::none ||
                      nestedCall.raises != zc::none) {
                    operandRejected = true;
                    return zc::none;
                  }
                  const ast::NodeId nestedLeft(
                      tree.node(operandNode).payload.words[ast::kBinaryExprLhsWord]);
                  const ast::NodeId nestedRight(
                      tree.node(operandNode).payload.words[ast::kBinaryExprRhsWord]);
                  auto nestedLeftSpan = bound.parsedModule().spanFor(tree.node(nestedLeft).range);
                  auto nestedRightSpan = bound.parsedModule().spanFor(tree.node(nestedRight).range);
                  if (nestedLeftSpan == zc::none || nestedRightSpan == zc::none ||
                      !tree.contains(nestedLeft) || !tree.contains(nestedRight)) {
                    operandRejected = true;
                    return zc::none;
                  }
                  auto nestedLeftArm = self(self, nestedLeft, ZC_ASSERT_NONNULL(nestedLeftSpan));
                  auto nestedRightArm = self(self, nestedRight, ZC_ASSERT_NONNULL(nestedRightSpan));
                  if (operandRejected || nestedLeftArm == zc::none || nestedRightArm == zc::none) {
                    operandRejected = true;
                    return zc::none;
                  }
                  auto nestedBinary = PendingConditionalArmBinary{
                      zc::heap<PendingConditionalArm>(zc::mv(ZC_ASSERT_NONNULL(nestedLeftArm))),
                      zc::heap<PendingConditionalArm>(zc::mv(ZC_ASSERT_NONNULL(nestedRightArm))),
                      nestedOperandType, nestedOperation};
                  return PendingConditionalArm{zc::none,
                                               zc::none,
                                               zc::none,
                                               binaryOperandType,
                                               operandSpan.clone(),
                                               zc::mv(nestedBinary),
                                               operandNode};
                }
                auto parameter = resolvedCallableParameter(bound.bindings(), operandNode);
                if (parameter != zc::none) {
                  identity::CallableParameterId handle;
                  ZC_IF_SOME(value, parameter) { handle = value; }
                  auto authority = registries.callableParameter(handle);
                  if (authority == zc::none) {
                    operandRejected = true;
                    return zc::none;
                  }
                  zc::Maybe<PendingConditionalArm> built;
                  ZC_IF_SOME(entry, authority) {
                    bool matches = false;
                    for (const auto& parameterCandidate : parameters) {
                      if (parameterCandidate.key == entry.key() &&
                          parameterCandidate.type == binaryOperandType) {
                        matches = true;
                      }
                    }
                    if (!matches) {
                      operandRejected = true;
                    } else {
                      auto reference = HirParameterReferenceExpression{
                          HirNodeId(), entry.key().clone(), binaryOperandType,
                          HirValueCategory::Place, operandSpan.clone()};
                      built =
                          PendingConditionalArm{zc::none,          zc::mv(reference),   zc::none,
                                                binaryOperandType, operandSpan.clone(), zc::none,
                                                operandNode};
                    }
                  }
                  return built;
                }
                // A non-literal, non-parameter, non-binary operand resolves to
                // the written user local (`x = x + 1`): build a local-reference
                // arm.
                auto ownerBinding = resolvedOwnerLocal(bound.bindings(), operandNode);
                if (ownerBinding != zc::none &&
                    ZC_ASSERT_NONNULL(ownerBinding) == ZC_ASSERT_NONNULL(targetBinding)) {
                  auto reference =
                      HirLocalReferenceExpression{HirNodeId(), hirLocalId(1), binaryOperandType,
                                                  HirValueCategory::Place, operandSpan.clone()};
                  return PendingConditionalArm{zc::none,
                                               zc::none,
                                               zc::mv(reference),
                                               binaryOperandType,
                                               operandSpan.clone(),
                                               zc::none,
                                               operandNode};
                }
                operandRejected = true;
                return zc::none;
              }
              auto operandLiteralIndex = factIndex(facts.literals(), operandNode);
              if (operandLiteralIndex == zc::none) {
                operandRejected = true;
                return zc::none;
              }
              size_t operandLiteralSlot = 0;
              ZC_IF_SOME(index, operandLiteralIndex) { operandLiteralSlot = index; }
              const auto& literalFact = facts.literals().entries()[operandLiteralSlot].value;
              if (literalFact.type != binaryOperandType) {
                operandRejected = true;
                return zc::none;
              }
              return PendingConditionalArm{
                  literalFact.literal.clone(), zc::none, zc::none,   binaryOperandType,
                  operandSpan.clone(),         zc::none, operandNode};
            };
            auto buildWriteOperand = [&](ast::NodeId node, const identity::SourceSpan& span) {
              return buildWriteOperandImpl(buildWriteOperandImpl, node, span);
            };
            auto leftOperand = buildWriteOperand(binaryLeft, ZC_ASSERT_NONNULL(leftSpan));
            auto rightOperand = buildWriteOperand(binaryRight, ZC_ASSERT_NONNULL(rightSpan));
            if (operandRejected || leftOperand == zc::none || rightOperand == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            writeValueRecord.binary =
                PendingLocalWriteBinary{zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                        zc::mv(ZC_ASSERT_NONNULL(rightOperand)),
                                        binaryOperandType,
                                        writeType,
                                        operation,
                                        ZC_ASSERT_NONNULL(valueSpan).clone(),
                                        false,
                                        false,
                                        writeValue};
          } else if (referenceValue) {
            auto parameter = resolvedCallableParameter(bound.bindings(), writeValue);
            if (parameter == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            ZC_IF_SOME(handle, parameter) {
              auto authority = registries.callableParameter(handle);
              if (authority == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              ZC_IF_SOME(entry, authority) {
                bool matches = false;
                for (const auto& candidate : parameters) {
                  if (candidate.key == entry.key() && candidate.type == writeType) {
                    matches = true;
                  }
                }
                if (!matches) {
                  return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                       ir::IrFailureKind::InvalidFact, module,
                                                       registries, ordinal + 2);
                }
                writeValueRecord.parameter = HirParameterReferenceExpression{
                    HirNodeId(), entry.key().clone(), writeType, HirValueCategory::Place,
                    ZC_ASSERT_NONNULL(valueSpan).clone()};
              }
            }
          } else {
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& sourceLiteral = facts.literals().entries()[literalSlot].value;
            if (sourceLiteral.type != writeType ||
                !sameSpan(sourceLiteral.sourceSpan, ZC_ASSERT_NONNULL(valueSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            writeValueRecord.literal = sourceLiteral.literal.clone();
          }
          bool firstFieldWrite = false;
          ZC_IF_SOME(currentField, field) {
            firstFieldWrite = true;
            for (const auto& previous : localWrites) {
              ZC_IF_SOME(previousField, previous.field) {
                if (previousField == currentField) {
                  firstFieldWrite = false;
                  break;
                }
              }
            }
          }
          const auto kind =
              shape.localInitializer == zc::none
                  ? (field != zc::none ? (firstFieldWrite ? HirLocalWriteKind::Initialize
                                                          : HirLocalWriteKind::Overwrite)
                                       : (writeIndex == 0 ? HirLocalWriteKind::Initialize
                                                          : HirLocalWriteKind::Overwrite))
                  : HirLocalWriteKind::Overwrite;
          localWrites.add(HirLocalWriteStatement{
              HirNodeId(), HirLocalId(), zc::mv(field), writeType, HirNodeId(), kind,
              ZC_ASSERT_NONNULL(assignmentSpan).clone(), ZC_ASSERT_NONNULL(valueSpan).clone()});
          localWriteValues.add(zc::mv(writeValueRecord));
        }
        // A prefix increment/decrement in return position (`return ++x;`)
        // desugars to a binary write (`x = x +/- 1`) followed by a local
        // reference return. The UnaryExpression node carries the call fact;
        // the binary and its literal operand are synthetic, so the write
        // pools into the same increment-write digest as the
        // expression-statement form.
        if (shape.returnsLocalIncrement) {
          const auto unaryOp = static_cast<ast::UnaryOperatorKind>(
              tree.node(shape.value).payload.words[ast::kUnaryExpressionOpWord]);
          if (unaryOp != ast::UnaryOperatorKind::PreIncrement &&
              unaryOp != ast::UnaryOperatorKind::PreDecrement) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          const ast::NodeId unaryTarget(
              tree.node(shape.value).payload.words[ast::kUnaryExpressionOperandWord]);
          auto unaryTypeIndex = factIndex(facts.nodeTypes(), shape.value);
          auto unaryTargetTypeIndex = factIndex(facts.nodeTypes(), unaryTarget);
          auto unaryCallIndex = factIndex(facts.calls(), shape.value);
          auto unarySpan = bound.parsedModule().spanFor(tree.node(shape.value).range);
          auto unaryTargetBinding = resolvedOwnerLocal(bound.bindings(), unaryTarget);
          auto unaryReturnBinding = resolvedOwnerLocal(bound.bindings(), shape.localReference);
          if (unaryTypeIndex == zc::none || unaryTargetTypeIndex == zc::none ||
              unaryCallIndex == zc::none || unarySpan == zc::none ||
              unaryTargetBinding == zc::none || unaryReturnBinding == zc::none ||
              unaryTargetBinding != unaryReturnBinding) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t unaryTypeSlot = 0;
          size_t unaryTargetSlot = 0;
          ZC_IF_SOME(index, unaryTypeIndex) { unaryTypeSlot = index; }
          ZC_IF_SOME(index, unaryTargetTypeIndex) { unaryTargetSlot = index; }
          const auto& unaryType = facts.nodeTypes().entries()[unaryTypeSlot].value;
          const auto& unaryTargetType = facts.nodeTypes().entries()[unaryTargetSlot].value;
          if (local == zc::none || unaryType != unaryTargetType) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          size_t unaryCallSlot = 0;
          ZC_IF_SOME(index, unaryCallIndex) { unaryCallSlot = index; }
          const auto& unaryCallFact = facts.calls().entries()[unaryCallSlot].value;
          const auto& unaryCall = unaryCallFact.invocation;
          const auto& unarySelected = unaryCall.selected.variant();
          if (!unarySelected.is<checker::checked::PrimitiveCallable>() ||
              unaryCallFact.node != shape.value || unaryCall.calleeType != unaryTargetType ||
              unaryCall.receiver != zc::none || unaryCall.receiverMode != zc::none ||
              unaryCall.receiverAdjustment != zc::none || unaryCall.arguments.size() != 1 ||
              unaryCall.arguments[0].sourceNode != unaryTarget ||
              unaryCall.arguments[0].sourceType != unaryTargetType ||
              unaryCall.successType != unaryTargetType || unaryCall.resultType != unaryTargetType ||
              unaryCall.substitutions != zc::none || unaryCall.witnesses != zc::none ||
              unaryCall.raises != zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          const auto unaryOperation =
              unarySelected.get<checker::checked::PrimitiveCallable>().operation;
          if (unaryOperation != checker::PrimitiveOperation::PreIncrement &&
              unaryOperation != checker::PrimitiveOperation::PreDecrement) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          const auto binaryOperation = unaryOperation == checker::PrimitiveOperation::PreIncrement
                                           ? checker::PrimitiveOperation::Add
                                           : checker::PrimitiveOperation::Sub;
          auto unitMagnitude = zc::heapArray<uint8_t>(1);
          unitMagnitude[0] = 1;
          auto syntheticOne =
              checker::checked::CanonicalConstValue::integer(checker::signature::CanonicalInteger{
                  checker::signature::IntegerSign::NonNegative, zc::mv(unitMagnitude)});
          auto unaryTargetSpan = bound.parsedModule().spanFor(tree.node(unaryTarget).range);
          if (unaryTargetSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          auto leftReference = HirLocalReferenceExpression{
              HirNodeId(), hirLocalId(1), unaryTargetType, HirValueCategory::Place,
              ZC_ASSERT_NONNULL(unaryTargetSpan).clone()};
          auto leftArm = PendingConditionalArm{zc::none,
                                               zc::none,
                                               zc::mv(leftReference),
                                               unaryTargetType,
                                               ZC_ASSERT_NONNULL(unaryTargetSpan).clone(),
                                               zc::none,
                                               unaryTarget};
          auto rightArm =
              PendingConditionalArm{zc::mv(syntheticOne), zc::none, zc::none, unaryTargetType,
                                    ZC_ASSERT_NONNULL(unarySpan).clone()};
          PendingLocalWriteValue unaryWriteValue;
          unaryWriteValue.binary =
              PendingLocalWriteBinary{zc::mv(leftArm), zc::mv(rightArm),
                                      unaryTargetType, unaryTargetType,
                                      binaryOperation, ZC_ASSERT_NONNULL(unarySpan).clone(),
                                      false,           false,
                                      shape.value};
          ZC_ASSERT_NONNULL(unaryWriteValue.binary).isIncrementDesugar = true;
          localWrites.add(HirLocalWriteStatement{
              HirNodeId(), HirLocalId(), zc::none, unaryTargetType, HirNodeId(),
              HirLocalWriteKind::Overwrite, ZC_ASSERT_NONNULL(unarySpan).clone(),
              ZC_ASSERT_NONNULL(unarySpan).clone()});
          localWriteValues.add(zc::mv(unaryWriteValue));
        }
        // A postfix increment/decrement in return position (`return x++;`)
        // returns the old value. The increment write is dead (the local is
        // destroyed after return) and elides to a local reference return. The
        // PostfixExpression node carries the call fact; validate it without
        // emitting a write.
        if (shape.returnsLocalPostfixIncrement) {
          const auto postfixOp = static_cast<ast::PostfixOperatorKind>(
              tree.node(shape.value).payload.words[ast::kPostfixExpressionOpWord]);
          if (postfixOp != ast::PostfixOperatorKind::Increment &&
              postfixOp != ast::PostfixOperatorKind::Decrement) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          const ast::NodeId postfixTarget(
              tree.node(shape.value).payload.words[ast::kPostfixExpressionOperandWord]);
          auto postfixTypeIndex = factIndex(facts.nodeTypes(), shape.value);
          auto postfixTargetTypeIndex = factIndex(facts.nodeTypes(), postfixTarget);
          auto postfixCallIndex = factIndex(facts.calls(), shape.value);
          auto postfixSpan = bound.parsedModule().spanFor(tree.node(shape.value).range);
          auto postfixTargetBinding = resolvedOwnerLocal(bound.bindings(), postfixTarget);
          auto postfixReturnBinding = resolvedOwnerLocal(bound.bindings(), shape.localReference);
          if (postfixTypeIndex == zc::none || postfixTargetTypeIndex == zc::none ||
              postfixCallIndex == zc::none || postfixSpan == zc::none ||
              postfixTargetBinding == zc::none || postfixReturnBinding == zc::none ||
              postfixTargetBinding != postfixReturnBinding) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t postfixTypeSlot = 0;
          size_t postfixTargetSlot = 0;
          ZC_IF_SOME(index, postfixTypeIndex) { postfixTypeSlot = index; }
          ZC_IF_SOME(index, postfixTargetTypeIndex) { postfixTargetSlot = index; }
          const auto& postfixType = facts.nodeTypes().entries()[postfixTypeSlot].value;
          const auto& postfixTargetType = facts.nodeTypes().entries()[postfixTargetSlot].value;
          if (local == zc::none || postfixType != postfixTargetType) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          size_t postfixCallSlot = 0;
          ZC_IF_SOME(index, postfixCallIndex) { postfixCallSlot = index; }
          const auto& postfixCallFact = facts.calls().entries()[postfixCallSlot].value;
          const auto& postfixCall = postfixCallFact.invocation;
          const auto& postfixSelected = postfixCall.selected.variant();
          if (!postfixSelected.is<checker::checked::PrimitiveCallable>() ||
              postfixCallFact.node != shape.value || postfixCall.calleeType != postfixTargetType ||
              postfixCall.receiver != zc::none || postfixCall.receiverMode != zc::none ||
              postfixCall.receiverAdjustment != zc::none || postfixCall.arguments.size() != 1 ||
              postfixCall.arguments[0].sourceNode != postfixTarget ||
              postfixCall.arguments[0].sourceType != postfixTargetType ||
              postfixCall.successType != postfixTargetType ||
              postfixCall.resultType != postfixTargetType ||
              postfixCall.substitutions != zc::none || postfixCall.witnesses != zc::none ||
              postfixCall.raises != zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          const auto postfixOperation =
              postfixSelected.get<checker::checked::PrimitiveCallable>().operation;
          if (postfixOperation != checker::PrimitiveOperation::PostIncrement &&
              postfixOperation != checker::PrimitiveOperation::PostDecrement) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
        }
        // A primitive unary return (`return !x;` / `return +x;` / etc.)
        // desugars to an equivalent binary with a synthetic constant operand
        // (e.g. `!x` -> `x == false`). The UnaryExpression node carries the
        // call fact; the binary and its synthetic operand have no AST nodes.
        // The return value is the desugared binary, not a local reference.
        if (shape.returnsPrimitiveUnary) {
          auto unaryTypeIndex = factIndex(facts.nodeTypes(), shape.value);
          auto unaryCallIndex = factIndex(facts.calls(), shape.value);
          auto unarySpan = bound.parsedModule().spanFor(tree.node(shape.value).range);
          if (unaryTypeIndex == zc::none || unaryCallIndex == zc::none || unarySpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t unaryTypeSlot = 0;
          ZC_IF_SOME(index, unaryTypeIndex) { unaryTypeSlot = index; }
          const auto& unaryType = facts.nodeTypes().entries()[unaryTypeSlot].value;
          size_t unaryCallSlot = 0;
          ZC_IF_SOME(index, unaryCallIndex) { unaryCallSlot = index; }
          const auto& unaryCallFact = facts.calls().entries()[unaryCallSlot].value;
          const auto& unaryCall = unaryCallFact.invocation;
          const auto& unarySelected = unaryCall.selected.variant();
          if (!unarySelected.is<checker::checked::PrimitiveCallable>() ||
              unaryCallFact.node != shape.value || unaryCall.receiver != zc::none ||
              unaryCall.receiverMode != zc::none || unaryCall.receiverAdjustment != zc::none ||
              unaryCall.arguments.size() != 1 ||
              unaryCall.arguments[0].sourceNode != shape.localReference ||
              unaryCall.successType != unaryType || unaryCall.resultType != unaryType ||
              unaryCall.substitutions != zc::none || unaryCall.witnesses != zc::none ||
              unaryCall.raises != zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          const auto unaryOperation =
              unarySelected.get<checker::checked::PrimitiveCallable>().operation;
          if (!isScalarUnaryOperation(unaryOperation)) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto operandKind = primitiveKindOf(checkedModule.semanticTypes(), unaryType);
          if (operandKind == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto desugar = desugarPrimitiveUnary(unaryOperation, ZC_ASSERT_NONNULL(operandKind));
          if (desugar == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto operandSpan = bound.parsedModule().spanFor(tree.node(shape.localReference).range);
          if (operandSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          auto localArm = PendingConditionalArm{
              zc::none,
              zc::none,
              HirLocalReferenceExpression{HirNodeId(), hirLocalId(1), unaryType,
                                          HirValueCategory::Place,
                                          ZC_ASSERT_NONNULL(operandSpan).clone()},
              unaryType,
              ZC_ASSERT_NONNULL(operandSpan).clone(),
              zc::none,
              shape.localReference};
          auto syntheticArm =
              PendingConditionalArm{ZC_ASSERT_NONNULL(desugar).syntheticValue.clone(), zc::none,
                                    zc::none, unaryType, ZC_ASSERT_NONNULL(unarySpan).clone()};
          if (ZC_ASSERT_NONNULL(desugar).syntheticOnLeft) {
            comparisonReturn = PendingEqualityCondition{zc::mv(syntheticArm),
                                                        zc::mv(localArm),
                                                        unaryType,
                                                        unaryType,
                                                        ZC_ASSERT_NONNULL(desugar).binaryOperation,
                                                        ZC_ASSERT_NONNULL(unarySpan).clone(),
                                                        true,
                                                        ast::NodeId()};
          } else {
            comparisonReturn = PendingEqualityCondition{zc::mv(localArm),
                                                        zc::mv(syntheticArm),
                                                        unaryType,
                                                        unaryType,
                                                        ZC_ASSERT_NONNULL(desugar).binaryOperation,
                                                        ZC_ASSERT_NONNULL(unarySpan).clone(),
                                                        true,
                                                        ast::NodeId()};
          }
        }
        if (shape.returnsArrayIndexFold || shape.returnsArrayLengthFold) {
          // Array index or length fold: `return a[0];` or `return a.length;`
          // where `a` is an array-literal-initialized local. The body checker
          // folds the constant read to a scalar literal fact, so the return
          // value lowers to that literal. The array aggregate itself is
          // validated above but not lowered to a HIR expression.
          auto literalIndex = factIndex(facts.literals(), shape.value);
          if (literalIndex == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t literalSlot = 0;
          ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
          const auto& sourceLiteral = facts.literals().entries()[literalSlot].value;
          if (sourceLiteral.type != nodeType.value ||
              !sameSpan(valueSpanValue, sourceLiteral.sourceSpan)) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          literal = sourceLiteral.literal.clone();
          returnsArrayIndexFold = shape.returnsArrayIndexFold;
          returnsArrayLengthFold = shape.returnsArrayLengthFold;
        }
      } else if (shape.returnsEnumConstruction) {
        // Single-statement enum construction return: `return Result::Ok(41);`.
        // The body checker produces an aggregate fact for the construction;
        // build the HirNominalAggregateExpression wired directly to the return
        // value (no intermediate local).
        auto aggregateIndex = factIndex(facts.aggregates(), shape.value);
        if (aggregateIndex == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t aggregateSlot = 0;
        ZC_IF_SOME(index, aggregateIndex) { aggregateSlot = index; }
        const auto& sourceAggregate = facts.aggregates().entries()[aggregateSlot].value;
        if (sourceAggregate.node != shape.value ||
            !sourceAggregate.kind.variant().is<checker::checked::NominalAggregate>() ||
            sourceAggregate.resultType != nodeType.value ||
            !sameSpan(sourceAggregate.sourceSpan, valueSpanValue)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        zc::Vector<HirNominalAggregateElement> elements;
        for (const auto& sourceElement : sourceAggregate.elements) {
          if (sourceElement.field == zc::none ||
              sourceElement.sourceType != sourceElement.destinationType ||
              sourceElement.adjustment != zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto elementLiteral = factIndex(facts.literals(), sourceElement.sourceNode);
          auto elementSpan =
              bound.parsedModule().spanFor(tree.node(sourceElement.sourceNode).range);
          if (elementLiteral == zc::none || elementSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t literalSlot = 0;
          ZC_IF_SOME(index, elementLiteral) { literalSlot = index; }
          const auto& literalFact = facts.literals().entries()[literalSlot].value;
          if (literalFact.type != sourceElement.destinationType ||
              !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(elementSpan))) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          ZC_IF_SOME(field, sourceElement.field) {
            elements.add(HirNominalAggregateElement{field, sourceElement.destinationType,
                                                    literalFact.literal.clone(),
                                                    ZC_ASSERT_NONNULL(elementSpan).clone()});
          }
        }
        aggregate = HirNominalAggregateExpression{
            HirNodeId(),
            sourceAggregate.kind.variant().get<checker::checked::NominalAggregate>().definition,
            nodeType.value,
            zc::mv(elements),
            HirValueCategory::Value,
            valueSpanValue.clone()};
        aggregateIsEnumConstruction = true;
      } else if (shape.returnsAggregateConstruction) {
        // Single-statement aggregate construction return:
        // `fn f() -> dyn I { return P { ..constants.. }; }`. The body checker
        // produces an aggregate fact for the construction and a DynErase
        // coercion from the concrete type to the existential return type.
        // Build the HirNominalAggregateExpression and attach a
        // HirErasureRecord to the function declaration.
        auto aggregateIndex = factIndex(facts.aggregates(), shape.value);
        if (aggregateIndex == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t aggregateSlot = 0;
        ZC_IF_SOME(index, aggregateIndex) { aggregateSlot = index; }
        const auto& sourceAggregate = facts.aggregates().entries()[aggregateSlot].value;
        if (sourceAggregate.node != shape.value ||
            !sourceAggregate.kind.variant().is<checker::checked::NominalAggregate>() ||
            sourceAggregate.resultType != nodeType.value ||
            !sameSpan(sourceAggregate.sourceSpan, valueSpanValue)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        zc::Vector<HirNominalAggregateElement> elements;
        for (const auto& sourceElement : sourceAggregate.elements) {
          if (sourceElement.field == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto elementSpan =
              bound.parsedModule().spanFor(tree.node(sourceElement.sourceNode).range);
          if (elementSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          zc::Maybe<HirErasureRecord> elementErasure;
          zc::Vector<HirConcreteAggregateElement> concreteElements;
          zc::Maybe<checker::checked::CanonicalConstValue> elementValue;
          if (sourceElement.sourceType != sourceElement.destinationType) {
            // DynErase element: a concrete value erased to an existential
            // field type. The adjustment carries the witness-backed coercion.
            if (sourceElement.adjustment == zc::none ||
                !isSingleDynEraseAdjustment(ZC_ASSERT_NONNULL(sourceElement.adjustment))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto& adj = ZC_ASSERT_NONNULL(sourceElement.adjustment);
            if (adj.site != checker::checked::CoercionSite::AggregateField ||
                adj.source != sourceElement.sourceType ||
                adj.destination != sourceElement.destinationType) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto& eraseStep = adj.steps[0].variant().get<checker::checked::DynEraseStep>();
            // Look up the inner aggregate fact (the concrete value's own
            // aggregate) so the MIR builder can construct the concrete
            // temporary without the semantic type store.
            auto innerIdx = factIndex(facts.aggregates(), sourceElement.sourceNode);
            if (innerIdx == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t innerSlot = 0;
            ZC_IF_SOME(index, innerIdx) { innerSlot = index; }
            const auto& inner = facts.aggregates().entries()[innerSlot].value;
            if (!inner.kind.variant().is<checker::checked::NominalAggregate>()) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto& innerNominal =
                inner.kind.variant().get<checker::checked::NominalAggregate>();
            elementErasure =
                HirErasureRecord{adj.source, adj.destination, eraseStep.impl,
                                 eraseStep.interface.interface, innerNominal.definition};
            for (const auto& ie : inner.elements) {
              auto litIdx = factIndex(facts.literals(), ie.sourceNode);
              if (litIdx == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              size_t litSlot = 0;
              ZC_IF_SOME(index, litIdx) { litSlot = index; }
              const auto& lit = facts.literals().entries()[litSlot].value;
              ZC_IF_SOME(f, ie.field) {
                concreteElements.add(HirConcreteAggregateElement{f, lit.type, lit.literal.clone()});
              }
            }
            elementValue = checker::checked::CanonicalConstValue::null();
          } else {
            if (sourceElement.adjustment != zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            auto elementLiteral = factIndex(facts.literals(), sourceElement.sourceNode);
            if (elementLiteral == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(index, elementLiteral) { literalSlot = index; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            if (literalFact.type != sourceElement.destinationType ||
                !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(elementSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            elementValue = literalFact.literal.clone();
          }
          ZC_IF_SOME(field, sourceElement.field) {
            elements.add(HirNominalAggregateElement{
                field, sourceElement.destinationType, zc::mv(ZC_ASSERT_NONNULL(elementValue)),
                ZC_ASSERT_NONNULL(elementSpan).clone(), zc::mv(elementErasure),
                zc::mv(concreteElements)});
          }
        }
        aggregate = HirNominalAggregateExpression{
            HirNodeId(),
            sourceAggregate.kind.variant().get<checker::checked::NominalAggregate>().definition,
            nodeType.value,
            zc::mv(elements),
            HirValueCategory::Value,
            valueSpanValue.clone()};
        // Only existential returns require a return-site DynErase coercion.
        // Concrete aggregate returns (e.g. Zoo containing an erased field)
        // carry per-field erasures on the elements themselves.
        if (isExistentialSemanticType(checkedModule.semanticTypes(), callable.success)) {
          auto coercionIndex = factIndex(facts.coercions(), shape.value);
          if (coercionIndex == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t coercionSlot = 0;
          ZC_IF_SOME(index, coercionIndex) { coercionSlot = index; }
          const auto& coercion = facts.coercions().entries()[coercionSlot].value;
          if (!isSingleDynEraseAdjustment(coercion) ||
              coercion.site != checker::checked::CoercionSite::Return ||
              coercion.source != nodeType.value || coercion.destination != callable.success) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          const auto& eraseStep = coercion.steps[0].variant().get<checker::checked::DynEraseStep>();
          returnErasure = HirErasureRecord{coercion.source, coercion.destination, eraseStep.impl,
                                           eraseStep.interface.interface};
        }
      } else if (isScalarLiteral(tree.node(shape.value).kind) || shape.returnsFoldedStringConcat ||
                 shape.returnsFoldedFloatCast || shape.returnsArrayIndexFold ||
                 shape.returnsArrayLengthFold) {
        auto literalIndex = factIndex(facts.literals(), shape.value);
        if (literalIndex == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t literalSlot = 0;
        ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
        const auto& sourceLiteral = facts.literals().entries()[literalSlot].value;
        if (sourceLiteral.type != nodeType.value ||
            !sameSpan(valueSpanValue, sourceLiteral.sourceSpan)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        literal = sourceLiteral.literal.clone();
        returnsArrayIndexFold = shape.returnsArrayIndexFold;
        returnsArrayLengthFold = shape.returnsArrayLengthFold;
      } else if (shape.returnsRawDereference) {
        // Raw-pointer dereference return: `return unsafe { *ptr };` where
        // `ptr` is a raw-pointer parameter. The operand resolves to a callable
        // parameter whose type is a raw pointer whose pointee matches the
        // dereference result type. The lowering function allocates a parameter
        // reference wrapped in a trailing unsafe block.
        auto parameter = resolvedCallableParameter(bound.bindings(), shape.rawDereferenceOperand);
        if (parameter == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        ZC_IF_SOME(handle, parameter) {
          auto authority = registries.callableParameter(handle);
          if (authority == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(entry, authority) {
            bool matches = false;
            identity::SemanticTypeId parameterType;
            for (const auto& candidate : parameters) {
              if (candidate.key == entry.key()) {
                matches = true;
                parameterType = candidate.type;
                break;
              }
            }
            if (!matches) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            // The parameter must carry a raw-pointer type whose pointee matches
            // the dereference result type.
            auto parameterLookup = checkedModule.semanticTypes().get(parameterType);
            if (!parameterLookup.is<type::SemanticTypeLookup>() ||
                !parameterLookup.get<type::SemanticTypeLookup>()
                     .data()
                     .is<type::semantic::RawPointerTypeData>() ||
                parameterLookup.get<type::SemanticTypeLookup>()
                        .data()
                        .get<type::semantic::RawPointerTypeData>()
                        .pointee != nodeType.value) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            auto operandSpan =
                bound.parsedModule().spanFor(tree.node(shape.rawDereferenceOperand).range);
            if (operandSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            rawDereferenceSource = HirParameterReferenceExpression{
                HirNodeId(), entry.key().clone(), parameterType, HirValueCategory::Place,
                ZC_ASSERT_NONNULL(operandSpan).clone()};
          }
        }
        // Raw-pointer write-read shape: additionally populate the write side
        // (the `unsafe { *ptr = <lit>; };` statement that precedes the
        // trailing raw-deref return).
        if (shape.writesRawDereference) {
          const ast::NodeId writeLhs(tree.node(shape.rawDereferenceWriteAssignment)
                                         .payload.words[ast::kAssignmentExprLhsWord]);
          if (!tree.contains(writeLhs) ||
              tree.node(writeLhs).kind != ast::SyntaxKind::UnaryExpression) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          const ast::NodeId writeOperand(
              tree.node(writeLhs).payload.words[ast::kUnaryExpressionOperandWord]);
          auto writeParam = resolvedCallableParameter(bound.bindings(), writeOperand);
          if (writeParam == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(writeHandle, writeParam) {
            auto writeAuthority = registries.callableParameter(writeHandle);
            if (writeAuthority == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            ZC_IF_SOME(writeEntry, writeAuthority) {
              bool writeMatches = false;
              identity::SemanticTypeId writeParamType;
              for (const auto& candidate : parameters) {
                if (candidate.key == writeEntry.key()) {
                  writeMatches = true;
                  writeParamType = candidate.type;
                  break;
                }
              }
              if (!writeMatches) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              auto writeParamLookup = checkedModule.semanticTypes().get(writeParamType);
              if (!writeParamLookup.is<type::SemanticTypeLookup>() ||
                  !writeParamLookup.get<type::SemanticTypeLookup>()
                       .data()
                       .is<type::semantic::RawPointerTypeData>() ||
                  writeParamLookup.get<type::SemanticTypeLookup>()
                          .data()
                          .get<type::semantic::RawPointerTypeData>()
                          .pointee != nodeType.value) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              const ast::NodeId writeValueNode(tree.node(shape.rawDereferenceWriteAssignment)
                                                   .payload.words[ast::kAssignmentExprRhsWord]);
              auto writeLiteralIndex = factIndex(facts.literals(), writeValueNode);
              if (writeLiteralIndex == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              size_t writeLiteralSlot = 0;
              ZC_IF_SOME(index, writeLiteralIndex) { writeLiteralSlot = index; }
              const auto& writeLiteralFact = facts.literals().entries()[writeLiteralSlot].value;
              if (writeLiteralFact.type != nodeType.value) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              rawDereferenceWriteLiteral = writeLiteralFact.literal.clone();
              auto writeStmtSpan =
                  bound.parsedModule().spanFor(tree.node(shape.rawDereferenceWriteStatement).range);
              if (writeStmtSpan == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              auto writeValueSpan = bound.parsedModule().spanFor(tree.node(writeValueNode).range);
              if (writeValueSpan == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              auto writeUnsafeSpan = bound.parsedModule().spanFor(
                  tree.node(shape.rawDereferenceWriteUnsafeBlock).range);
              if (writeUnsafeSpan == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              writeUnsafeBlockSpan = ZC_ASSERT_NONNULL(writeUnsafeSpan).clone();
              rawDereferenceWrite =
                  HirRawPointerWriteStatement{HirNodeId(),
                                              writeEntry.key().clone(),
                                              writeParamType,
                                              nodeType.value,
                                              HirNodeId(),
                                              ZC_ASSERT_NONNULL(writeStmtSpan).clone(),
                                              ZC_ASSERT_NONNULL(writeValueSpan).clone()};
            }
          }
        }
      } else if (!shape.isForLoopAccumulator && !shape.isNestedForLoopAccumulator &&
                 !shape.isForInLoopAccumulator &&
                 tree.node(shape.value).kind == ast::SyntaxKind::IdentExpr) {
        auto parameter = resolvedCallableParameter(bound.bindings(), shape.value);
        if (parameter == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        ZC_IF_SOME(handle, parameter) {
          auto authority = registries.callableParameter(handle);
          if (authority == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(entry, authority) {
            bool matches = false;
            for (const auto& candidate : parameters) {
              if (candidate.key == entry.key() && candidate.type == nodeType.value) {
                matches = true;
              }
            }
            if (!matches) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            parameterReference =
                HirParameterReferenceExpression{HirNodeId(), entry.key().clone(), nodeType.value,
                                                HirValueCategory::Place, valueSpanValue.clone()};
          }
        }
      } else if (shape.returnsParameterField) {
        const ast::NodeId object(
            tree.node(shape.value).payload.words[ast::kMemberExpressionObjectWord]);
        auto objectTypeIndex = factIndex(facts.nodeTypes(), object);
        auto memberIndex = factIndex(facts.members(), shape.value);
        auto placeIndex = factIndex(facts.places(), shape.value);
        if (objectTypeIndex == zc::none || memberIndex == zc::none || placeIndex == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t objectTypeSlot = 0;
        size_t memberSlot = 0;
        size_t placeSlot = 0;
        ZC_IF_SOME(index, objectTypeIndex) { objectTypeSlot = index; }
        ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
        ZC_IF_SOME(index, placeIndex) { placeSlot = index; }
        const auto& member = facts.members().entries()[memberSlot].value;
        const auto& place = facts.places().entries()[placeSlot].value;
        const auto& root = place.root.variant();
        if (member.node != shape.value || member.memberType != nodeType.value ||
            member.adjustment != zc::none ||
            facts.nodeTypes().entries()[objectTypeSlot].value != member.receiverType ||
            place.type != nodeType.value || !place.movable ||
            !root.is<checker::checked::CallableParameterPlaceRoot>() ||
            place.projections.size() != 1 ||
            !place.projections[0].variant().is<checker::checked::FieldProjection>() ||
            place.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                member.member ||
            !typeExists(member.receiverType, checkedModule.semanticTypes())) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& parameterRoot = root.get<checker::checked::CallableParameterPlaceRoot>();
        auto rootAuthority = registries.callableParameter(parameterRoot.parameter);
        if (rootAuthority == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        ZC_IF_SOME(entry, rootAuthority) {
          bool matches = false;
          for (const auto& candidate : parameters) {
            if (candidate.key == entry.key() && candidate.type == member.receiverType) {
              matches = true;
            }
          }
          if (!matches) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          // A by-value struct parameter is a nominal, not a reference.
          auto parameterLookup = checkedModule.semanticTypes().get(member.receiverType);
          if (!parameterLookup.is<type::SemanticTypeLookup>() ||
              !parameterLookup.get<type::SemanticTypeLookup>()
                   .data()
                   .is<type::semantic::NominalTypeData>()) {
            return rejectHirCapability<HirModuleCandidate>(
                definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
                valueSpanValue.clone());
          }
          parameterFieldProjection = HirParameterFieldProjectionExpression{
              HirNodeId(),    entry.key().clone(),     member.receiverType,   member.member,
              nodeType.value, HirValueCategory::Place, valueSpanValue.clone()};
          byValueParameterField = true;
        }
      } else if (shape.returnsReceiverField) {
        const ast::NodeId object(
            tree.node(shape.value).payload.words[ast::kMemberExpressionObjectWord]);
        auto thisTypeIndex = factIndex(facts.nodeTypes(), object);
        auto memberIndex = factIndex(facts.members(), shape.value);
        auto placeIndex = factIndex(facts.places(), shape.value);
        if (thisTypeIndex == zc::none || memberIndex == zc::none || placeIndex == zc::none ||
            methodReceiver == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t thisTypeSlot = 0;
        size_t memberSlot = 0;
        size_t placeSlot = 0;
        ZC_IF_SOME(index, thisTypeIndex) { thisTypeSlot = index; }
        ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
        ZC_IF_SOME(index, placeIndex) { placeSlot = index; }
        const auto& receiver = ZC_ASSERT_NONNULL(methodReceiver);
        const auto& member = facts.members().entries()[memberSlot].value;
        const auto& place = facts.places().entries()[placeSlot].value;
        const auto& root = place.root.variant();
        if (member.node != shape.value || member.memberType != nodeType.value ||
            member.adjustment != zc::none ||
            facts.nodeTypes().entries()[thisTypeSlot].value != receiver.type ||
            place.type != nodeType.value || !place.movable ||
            !root.is<checker::checked::CallableParameterPlaceRoot>() ||
            place.projections.size() != 1 ||
            !place.projections[0].variant().is<checker::checked::FieldProjection>() ||
            place.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                member.member ||
            !typeExists(member.receiverType, checkedModule.semanticTypes())) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto rootAuthority = registries.callableParameter(
            root.get<checker::checked::CallableParameterPlaceRoot>().parameter);
        if (rootAuthority == zc::none || ZC_ASSERT_NONNULL(rootAuthority).key() != receiver.key) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto receiverLookup = checkedModule.semanticTypes().get(receiver.type);
        if (!receiverLookup.is<type::SemanticTypeLookup>() ||
            !receiverLookup.get<type::SemanticTypeLookup>()
                 .data()
                 .is<type::semantic::ReferenceTypeData>() ||
            receiverLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .referent != member.receiverType) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        parameterFieldProjection = HirParameterFieldProjectionExpression{
            HirNodeId(),    receiver.key.clone(),    member.receiverType,   member.member,
            nodeType.value, HirValueCategory::Place, valueSpanValue.clone()};
        if (shape.writesReceiverField) {
          if ((receiverLookup.get<type::SemanticTypeLookup>()
                   .data()
                   .get<type::semantic::ReferenceTypeData>()
                   .mutability == type::semantic::Mutability::Mutable) != true) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto writeLiteralIndex = factIndex(facts.literals(), shape.receiverWriteValue);
          auto writeNodeTypeIndex = factIndex(facts.nodeTypes(), shape.receiverWriteValue);
          auto writeStatementSpan =
              bound.parsedModule().spanFor(tree.node(shape.receiverWriteStatement).range);
          auto writeValueSpan =
              bound.parsedModule().spanFor(tree.node(shape.receiverWriteValue).range);
          if (writeLiteralIndex == zc::none || writeNodeTypeIndex == zc::none ||
              writeStatementSpan == zc::none || writeValueSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t writeLiteralSlot = 0;
          size_t writeTypeSlot = 0;
          ZC_IF_SOME(index, writeLiteralIndex) { writeLiteralSlot = index; }
          ZC_IF_SOME(index, writeNodeTypeIndex) { writeTypeSlot = index; }
          const auto& writeLiteral = facts.literals().entries()[writeLiteralSlot].value;
          if (writeLiteral.type != nodeType.value ||
              facts.nodeTypes().entries()[writeTypeSlot].value != nodeType.value ||
              !sameSpan(writeLiteral.sourceSpan, ZC_ASSERT_NONNULL(writeValueSpan))) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          parameterFieldWrite =
              HirParameterFieldWriteStatement{HirNodeId(),
                                              receiver.key.clone(),
                                              member.receiverType,
                                              member.member,
                                              nodeType.value,
                                              HirNodeId(),
                                              ZC_ASSERT_NONNULL(writeStatementSpan).clone(),
                                              ZC_ASSERT_NONNULL(writeValueSpan).clone()};
          parameterFieldWriteLiteral = writeLiteral.literal.clone();
        }
        // A field READ copies the projected value out through the receiver
        // pointer and is sound for both shared and mutable receivers. The write
        // arm above is the only mutability gate on this shape.
      } else if (shape.returnsReceiverSelfCall) {
        // Shared-receiver self-call: `return this.<method>();` forwards the
        // implicit receiver parameter to a zero-argument method of the same
        // owner. Unlike the owner-local receiver call there is no binding and
        // no aggregate; the receiver is a header parameter and the adjustment
        // is a shared reborrow rather than a fresh borrow temporary.
        const ast::NodeId callNode = shape.value;
        const ast::NodeId calleeNode(
            tree.node(callNode).payload.words[ast::kCallExpressionCalleeWord]);
        const ast::NodeList selfTypeArguments{
            tree.node(callNode).payload.words[ast::kCallExpressionTypeArgsFirstWord],
            tree.node(callNode).payload.words[ast::kCallExpressionTypeArgsSizeWord]};
        const ast::NodeList selfArguments{
            tree.node(callNode).payload.words[ast::kCallExpressionArgsFirstWord],
            tree.node(callNode).payload.words[ast::kCallExpressionArgsSizeWord]};
        if (!tree.contains(calleeNode) ||
            tree.node(calleeNode).kind != ast::SyntaxKind::MemberExpression ||
            static_cast<ast::MemberAccessKind>(
                tree.node(calleeNode).payload.words[ast::kMemberExpressionAccessWord]) !=
                ast::MemberAccessKind::Dot ||
            !tree.contains(selfTypeArguments) || !selfTypeArguments.empty() ||
            !tree.contains(selfArguments) || !selfArguments.empty()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const ast::NodeId selfNode(
            tree.node(calleeNode).payload.words[ast::kMemberExpressionObjectWord]);
        auto thisTypeIndex = factIndex(facts.nodeTypes(), selfNode);
        auto memberIndex = factIndex(facts.members(), calleeNode);
        auto checkedCallIndex = factIndex(facts.calls(), callNode);
        auto callKey = checkedNodeKey(tree, bound.parsedModule(), callNode);
        auto callSpan = bound.parsedModule().spanFor(tree.node(callNode).range);
        auto dispatchIndex = callKey == zc::none
                                 ? zc::none
                                 : dispatchFactIndex(checkedModule.dispatchFacts().facts(),
                                                     ZC_ASSERT_NONNULL(callKey));
        if (!tree.contains(selfNode) || tree.node(selfNode).kind != ast::SyntaxKind::ThisExpr ||
            thisTypeIndex == zc::none || memberIndex == zc::none || checkedCallIndex == zc::none ||
            callKey == zc::none || dispatchIndex == zc::none || callSpan == zc::none ||
            methodReceiver == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t thisTypeSlot = 0;
        size_t memberSlot = 0;
        size_t checkedCallSlot = 0;
        size_t dispatchSlot = 0;
        ZC_IF_SOME(index, thisTypeIndex) { thisTypeSlot = index; }
        ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
        ZC_IF_SOME(index, checkedCallIndex) { checkedCallSlot = index; }
        ZC_IF_SOME(index, dispatchIndex) { dispatchSlot = index; }
        const auto& receiver = ZC_ASSERT_NONNULL(methodReceiver);
        const auto& member = facts.members().entries()[memberSlot].value;
        const auto& invocation = facts.calls().entries()[checkedCallSlot].value.invocation;
        const auto& selected = invocation.selected.variant();
        const auto& dispatch = checkedModule.dispatchFacts().facts()[dispatchSlot];
        const auto& target = dispatch.fact.target.variant();
        const auto& transform = dispatch.fact.resultTransform.variant();
        bool dispatchOwnerMatches = false;
        ZC_IF_SOME(owner, dispatch.owner) { dispatchOwnerMatches = owner == definition.definition; }
        // A self-call resolves to either a concrete method or a devirtualized
        // impl method; both lower through the same direct-call carrier.
        const auto selfSelectedMethod = [&]() -> identity::DefId {
          if (selected.is<checker::checked::ConcreteMethodCallable>()) {
            return selected.get<checker::checked::ConcreteMethodCallable>().method;
          }
          return selected.get<checker::checked::ImplMethodCallable>().method;
        };
        const auto selfTargetMethod = [&]() -> identity::DefId {
          if (target.is<checker::dispatch::ConcreteMethodTarget>()) {
            return target.get<checker::dispatch::ConcreteMethodTarget>().method;
          }
          return target.get<checker::dispatch::ImplMethodTarget>().method;
        };
        if (facts.nodeTypes().entries()[thisTypeSlot].value != receiver.type ||
            !(selected.is<checker::checked::ConcreteMethodCallable>() ||
              selected.is<checker::checked::ImplMethodCallable>()) ||
            selfSelectedMethod() != member.member ||
            !(target.is<checker::dispatch::ConcreteMethodTarget>() ||
              target.is<checker::dispatch::ImplMethodTarget>()) ||
            selfTargetMethod() != member.member ||
            !transform.is<checker::dispatch::IdentityResultTransform>() ||
            member.node != calleeNode || member.receiverType != receiver.type ||
            member.memberType != invocation.calleeType || member.adjustment != zc::none ||
            invocation.successType != nodeType.value || invocation.resultType != nodeType.value ||
            invocation.arguments.size() != 0 || invocation.substitutions != zc::none ||
            invocation.witnesses != zc::none || invocation.raises != zc::none ||
            !dispatchOwnerMatches || dispatch.fact.receiver == zc::none ||
            dispatch.fact.arguments.size() != 0 || dispatch.fact.successType != nodeType.value ||
            dispatch.fact.resultType != nodeType.value || dispatch.fact.substitutions != zc::none ||
            dispatch.fact.witnesses != zc::none || dispatch.fact.raises != zc::none ||
            !sameSpan(facts.calls().entries()[checkedCallSlot].value.sourceSpan,
                      ZC_ASSERT_NONNULL(callSpan)) ||
            !sameSpan(dispatch.fact.sourceSpan, ZC_ASSERT_NONNULL(callSpan))) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        ZC_IF_SOME(callReceiver, invocation.receiver) {
          ZC_IF_SOME(mode, invocation.receiverMode) {
            ZC_IF_SOME(adjustment, invocation.receiverAdjustment) {
              if (callReceiver.sourceNode != selfNode || callReceiver.sourceType != receiver.type ||
                  callReceiver.parameterType != receiver.type ||
                  callReceiver.adjustment != zc::none ||
                  mode != checker::checked::ReceiverMode::Shared ||
                  adjustment.source != receiver.type || adjustment.destination != receiver.type ||
                  adjustment.steps.size() != 1 ||
                  adjustment.steps[0] != checker::checked::ReceiverAdjustmentStep::ReborrowShared) {
                return rejectHirCapability<HirModuleCandidate>(
                    definition.definition, registries,
                    ir::IrFailureKind::UnsupportedSourceConstruct,
                    ZC_ASSERT_NONNULL(callSpan).clone());
              }
              zc::Vector<checker::checked::ReceiverAdjustmentStep> steps;
              steps.add(checker::checked::ReceiverAdjustmentStep::ReborrowShared);
              zc::Vector<HirDirectCallArgument> callArguments;
              receiverSelfCall = HirReceiverCallExpression{HirNodeId(),
                                                           HirNodeId(),
                                                           member.member,
                                                           invocation.calleeType,
                                                           receiver.type,
                                                           receiver.type,
                                                           mode,
                                                           zc::mv(steps),
                                                           invocation.resultType,
                                                           zc::mv(callArguments),
                                                           ZC_ASSERT_NONNULL(callSpan).clone(),
                                                           zc::Maybe<identity::DefId>()};
            }
          }
        }
        if (receiverSelfCall == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
      } else if (shape.returnsReceiverFieldArithmetic) {
        // Receiver-field arithmetic: `return this.<field> OP <literal>;`
        // (or the mirrored operand order). The binary result type is the field
        // type. The field operand reuses the receiver-field projection facts;
        // the literal operand consumes its checked literal fact and the binary
        // its checked primitive-arithmetic call fact. Both shared and mutable
        // receivers are admitted: the body only reads the field, so the
        // receiver mutability does not affect the arithmetic lowering.
        const bool fieldIsLeft = !shape.comparisonLeftIsLiteral;
        const ast::NodeId fieldNode = fieldIsLeft ? shape.comparisonLeft : shape.comparisonRight;
        const ast::NodeId literalNode = fieldIsLeft ? shape.comparisonRight : shape.comparisonLeft;
        const ast::NodeId fieldObject(
            tree.node(fieldNode).payload.words[ast::kMemberExpressionObjectWord]);
        auto thisTypeIndex = factIndex(facts.nodeTypes(), fieldObject);
        auto memberIndex = factIndex(facts.members(), fieldNode);
        auto placeIndex = factIndex(facts.places(), fieldNode);
        auto binaryCallIndex = factIndex(facts.calls(), shape.value);
        auto literalIndex = factIndex(facts.literals(), literalNode);
        auto literalTypeIndex = factIndex(facts.nodeTypes(), literalNode);
        auto fieldSpan = bound.parsedModule().spanFor(tree.node(fieldNode).range);
        auto literalSpan = bound.parsedModule().spanFor(tree.node(literalNode).range);
        if (thisTypeIndex == zc::none || memberIndex == zc::none || placeIndex == zc::none ||
            binaryCallIndex == zc::none || literalIndex == zc::none ||
            literalTypeIndex == zc::none || fieldSpan == zc::none || literalSpan == zc::none ||
            methodReceiver == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t thisTypeSlot = 0;
        size_t memberSlot = 0;
        size_t placeSlot = 0;
        size_t binaryCallSlot = 0;
        size_t literalSlot = 0;
        size_t literalTypeSlot = 0;
        ZC_IF_SOME(index, thisTypeIndex) { thisTypeSlot = index; }
        ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
        ZC_IF_SOME(index, placeIndex) { placeSlot = index; }
        ZC_IF_SOME(index, binaryCallIndex) { binaryCallSlot = index; }
        ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
        ZC_IF_SOME(index, literalTypeIndex) { literalTypeSlot = index; }
        const auto& receiver = ZC_ASSERT_NONNULL(methodReceiver);
        const auto& member = facts.members().entries()[memberSlot].value;
        const auto& place = facts.places().entries()[placeSlot].value;
        const auto& root = place.root.variant();
        const auto& literalFact = facts.literals().entries()[literalSlot].value;
        const auto& binaryCallFact = facts.calls().entries()[binaryCallSlot].value;
        const auto& binaryCall = binaryCallFact.invocation;
        const auto& binarySelected = binaryCall.selected.variant();
        const auto operation =
            binarySelected.is<checker::checked::PrimitiveCallable>()
                ? zc::Maybe<checker::PrimitiveOperation>(
                      binarySelected.get<checker::checked::PrimitiveCallable>().operation)
                : zc::Maybe<checker::PrimitiveOperation>(zc::none);
        bool operationIsArithmetic = false;
        ZC_IF_SOME(op, operation) { operationIsArithmetic = isScalarArithmeticOperation(op); }
        if (member.node != fieldNode || member.memberType != nodeType.value ||
            member.adjustment != zc::none ||
            facts.nodeTypes().entries()[thisTypeSlot].value != receiver.type ||
            place.type != nodeType.value || !place.movable ||
            !root.is<checker::checked::CallableParameterPlaceRoot>() ||
            place.projections.size() != 1 ||
            !place.projections[0].variant().is<checker::checked::FieldProjection>() ||
            place.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                member.member ||
            !typeExists(member.receiverType, checkedModule.semanticTypes()) ||
            literalFact.node != literalNode || literalFact.type != nodeType.value ||
            facts.nodeTypes().entries()[literalTypeSlot].value != nodeType.value ||
            binaryCallFact.node != shape.value || !operationIsArithmetic ||
            binaryCall.calleeType != nodeType.value || binaryCall.receiver != zc::none ||
            binaryCall.receiverMode != zc::none || binaryCall.receiverAdjustment != zc::none ||
            binaryCall.arguments.size() != 2 ||
            binaryCall.arguments[0].sourceNode != shape.comparisonLeft ||
            binaryCall.arguments[0].sourceType != nodeType.value ||
            binaryCall.arguments[1].sourceNode != shape.comparisonRight ||
            binaryCall.arguments[1].sourceType != nodeType.value ||
            binaryCall.successType != nodeType.value || binaryCall.resultType != nodeType.value ||
            binaryCall.substitutions != zc::none || binaryCall.witnesses != zc::none ||
            binaryCall.raises != zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto rootAuthority = registries.callableParameter(
            root.get<checker::checked::CallableParameterPlaceRoot>().parameter);
        if (rootAuthority == zc::none || ZC_ASSERT_NONNULL(rootAuthority).key() != receiver.key) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto receiverLookup = checkedModule.semanticTypes().get(receiver.type);
        if (!receiverLookup.is<type::SemanticTypeLookup>() ||
            !receiverLookup.get<type::SemanticTypeLookup>()
                 .data()
                 .is<type::semantic::ReferenceTypeData>() ||
            receiverLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .referent != member.receiverType) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto fieldProjection =
            HirParameterFieldProjectionExpression{HirNodeId(),
                                                  receiver.key.clone(),
                                                  member.receiverType,
                                                  member.member,
                                                  nodeType.value,
                                                  HirValueCategory::Place,
                                                  ZC_ASSERT_NONNULL(fieldSpan).clone()};
        receiverFieldArithmetic =
            PendingReceiverFieldArithmetic{zc::mv(fieldProjection),
                                           literalFact.literal.clone(),
                                           nodeType.value,
                                           ZC_ASSERT_NONNULL(operation),
                                           ZC_ASSERT_NONNULL(literalSpan).clone(),
                                           ZC_ASSERT_NONNULL(fieldSpan).clone(),
                                           valueSpanValue.clone(),
                                           fieldIsLeft,
                                           fieldNode,
                                           literalNode,
                                           shape.value};
      } else if (tree.node(shape.value).kind == ast::SyntaxKind::IndexExpression) {
        const auto& sourceIndex = tree.node(shape.value);
        const ast::NodeId base(sourceIndex.payload.words[ast::kIndexExpressionObjectWord]);
        const ast::NodeId index(sourceIndex.payload.words[ast::kIndexExpressionIndexWord]);
        auto parameter = resolvedCallableParameter(bound.bindings(), base);
        auto baseType = factIndex(facts.nodeTypes(), base);
        auto indexType = factIndex(facts.nodeTypes(), index);
        auto indexLiteral = factIndex(facts.literals(), index);
        auto callIndex = factIndex(facts.calls(), shape.value);
        auto placeIndex = factIndex(facts.places(), shape.value);
        auto checkedIndex = factIndex(facts.indexes(), shape.value);
        auto marker = factIndex(facts.markerObligations(), shape.value);
        auto indexSpan = bound.parsedModule().spanFor(tree.node(index).range);
        if (!tree.contains(base) || !tree.contains(index) ||
            tree.node(base).kind != ast::SyntaxKind::IdentExpr ||
            tree.node(index).kind != ast::SyntaxKind::IntLiteral || parameter == zc::none ||
            baseType == zc::none || indexType == zc::none || indexLiteral == zc::none ||
            callIndex == zc::none || placeIndex == zc::none || checkedIndex == zc::none ||
            marker == zc::none || indexSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t baseTypeSlot = 0;
        size_t indexTypeSlot = 0;
        size_t literalSlot = 0;
        size_t callSlot = 0;
        size_t placeSlot = 0;
        size_t indexSlot = 0;
        size_t markerSlot = 0;
        ZC_IF_SOME(value, baseType) { baseTypeSlot = value; }
        ZC_IF_SOME(value, indexType) { indexTypeSlot = value; }
        ZC_IF_SOME(value, indexLiteral) { literalSlot = value; }
        ZC_IF_SOME(value, callIndex) { callSlot = value; }
        ZC_IF_SOME(value, placeIndex) { placeSlot = value; }
        ZC_IF_SOME(value, checkedIndex) { indexSlot = value; }
        ZC_IF_SOME(value, marker) { markerSlot = value; }
        auto authority = registries.callableParameter(ZC_ASSERT_NONNULL(parameter));
        if (authority == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const auto& literalFact = facts.literals().entries()[literalSlot].value;
        const auto& callFact = facts.calls().entries()[callSlot].value;
        const auto& call = callFact.invocation;
        const auto& selected = call.selected.variant();
        const auto& place = facts.places().entries()[placeSlot].value;
        const auto& root = place.root.variant();
        const auto& indexFact = facts.indexes().entries()[indexSlot].value;
        const auto& markerFact = facts.markerObligations().entries()[markerSlot].value;
        ZC_IF_SOME(entry, authority) {
          bool matches = false;
          for (const auto& candidate : parameters) {
            if (candidate.key == entry.key() &&
                candidate.type == facts.nodeTypes().entries()[baseTypeSlot].value) {
              matches = true;
            }
          }
          if (!matches || literalFact.node != index ||
              literalFact.type != facts.nodeTypes().entries()[indexTypeSlot].value ||
              !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(indexSpan)) ||
              callFact.node != shape.value || !selected.is<checker::checked::PrimitiveCallable>() ||
              selected.get<checker::checked::PrimitiveCallable>().operation !=
                  checker::PrimitiveOperation::Index ||
              call.calleeType != facts.nodeTypes().entries()[baseTypeSlot].value ||
              call.receiver == zc::none || call.receiverMode == zc::none ||
              call.receiverAdjustment == zc::none || call.arguments.size() != 1 ||
              call.successType != nodeType.value || call.resultType != nodeType.value ||
              call.substitutions != zc::none || call.witnesses != zc::none ||
              call.raises != zc::none || !root.is<checker::checked::CallableParameterPlaceRoot>() ||
              root.get<checker::checked::CallableParameterPlaceRoot>().parameter !=
                  ZC_ASSERT_NONNULL(parameter) ||
              place.projections.size() != 1 ||
              !place.projections[0].variant().is<checker::checked::IndexProjection>() ||
              place.projections[0].variant().get<checker::checked::IndexProjection>().index !=
                  index ||
              place.type != nodeType.value || place.mutablePlace || place.movable ||
              indexFact.node != shape.value ||
              indexFact.collectionType != facts.nodeTypes().entries()[baseTypeSlot].value ||
              indexFact.indexType != facts.nodeTypes().entries()[indexTypeSlot].value ||
              indexFact.elementType != nodeType.value ||
              indexFact.accessMode != checker::checked::IndexAccessMode::Read ||
              indexFact.accessResultType != nodeType.value || markerFact.node != shape.value ||
              markerFact.subject != nodeType.value ||
              markerFact.polarity != checker::checked::Polarity::Positive) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          const auto& receiver = ZC_ASSERT_NONNULL(call.receiver);
          const auto& adjustment = ZC_ASSERT_NONNULL(call.receiverAdjustment);
          const auto& argument = call.arguments[0];
          const auto mode = ZC_ASSERT_NONNULL(call.receiverMode);
          if (receiver.sourceNode != base ||
              receiver.sourceType != facts.nodeTypes().entries()[baseTypeSlot].value ||
              receiver.parameterType != receiver.sourceType || receiver.adjustment != zc::none ||
              mode != checker::checked::ReceiverMode::Shared ||
              adjustment.source != receiver.sourceType ||
              adjustment.destination != receiver.parameterType || adjustment.steps.size() != 1 ||
              adjustment.steps[0] != checker::checked::ReceiverAdjustmentStep::BorrowShared ||
              argument.sourceNode != index ||
              argument.sourceType != facts.nodeTypes().entries()[indexTypeSlot].value ||
              argument.parameterType != argument.sourceType || argument.adjustment != zc::none ||
              !sameSpan(callFact.sourceSpan, valueSpanValue)) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          parameterIndex = HirParameterIndexExpression{HirNodeId(),
                                                       entry.key().clone(),
                                                       receiver.sourceType,
                                                       argument.sourceType,
                                                       literalFact.literal.clone(),
                                                       nodeType.value,
                                                       HirValueCategory::Place,
                                                       valueSpanValue.clone(),
                                                       ZC_ASSERT_NONNULL(indexSpan).clone()};
        }
      }
      if (!shape.returnsLocalBorrow &&
          tree.node(shape.value).kind == ast::SyntaxKind::UnaryExpression &&
          (static_cast<ast::UnaryOperatorKind>(
               tree.node(shape.value).payload.words[ast::kUnaryExpressionOpWord]) ==
               ast::UnaryOperatorKind::Ref ||
           static_cast<ast::UnaryOperatorKind>(
               tree.node(shape.value).payload.words[ast::kUnaryExpressionOpWord]) ==
               ast::UnaryOperatorKind::RefMut)) {
        const ast::NodeId dereference(
            tree.node(shape.value).payload.words[ast::kUnaryExpressionOperandWord]);
        if (!tree.contains(dereference) ||
            tree.node(dereference).kind != ast::SyntaxKind::UnaryExpression ||
            static_cast<ast::UnaryOperatorKind>(
                tree.node(dereference).payload.words[ast::kUnaryExpressionOpWord]) !=
                ast::UnaryOperatorKind::Deref) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const ast::NodeId parameter(
            tree.node(dereference).payload.words[ast::kUnaryExpressionOperandWord]);
        auto parameterTypeIndex = factIndex(facts.nodeTypes(), parameter);
        auto dereferenceTypeIndex = factIndex(facts.nodeTypes(), dereference);
        auto parameterHandle = resolvedCallableParameter(bound.bindings(), parameter);
        zc::Maybe<HirLocalId> sourceAlias;
        if (parameterHandle == zc::none && shape.returnsLocalReborrow) {
          auto alias = resolvedOwnerLocal(bound.bindings(), parameter);
          auto localRoot = resolvedOwnerLocal(bound.bindings(), shape.localReference);
          if (alias == zc::none || localRoot == zc::none || local == zc::none ||
              shape.localInitializer == zc::none ||
              ZC_ASSERT_NONNULL(alias) != ZC_ASSERT_NONNULL(localRoot)) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(initializer, shape.localInitializer) {
            parameterHandle = resolvedCallableParameter(bound.bindings(), initializer);
          }
          if (parameterHandle == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          sourceAlias = HirLocalId();
        }
        if (!tree.contains(parameter) || tree.node(parameter).kind != ast::SyntaxKind::IdentExpr ||
            parameterTypeIndex == zc::none || dereferenceTypeIndex == zc::none ||
            parameterHandle == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t parameterTypeSlot = 0;
        size_t dereferenceTypeSlot = 0;
        ZC_IF_SOME(index, parameterTypeIndex) { parameterTypeSlot = index; }
        ZC_IF_SOME(index, dereferenceTypeIndex) { dereferenceTypeSlot = index; }
        const auto sourceType = facts.nodeTypes().entries()[parameterTypeSlot].value;
        const auto referentType = facts.nodeTypes().entries()[dereferenceTypeSlot].value;
        const auto operation = static_cast<ast::UnaryOperatorKind>(
            tree.node(shape.value).payload.words[ast::kUnaryExpressionOpWord]);
        const auto expectedMutability = operation == ast::UnaryOperatorKind::Ref
                                            ? type::semantic::Mutability::Const
                                            : type::semantic::Mutability::Mutable;
        auto typeLookup = checkedModule.semanticTypes().get(sourceType);
        if (!typeLookup.is<type::SemanticTypeLookup>() ||
            !typeLookup.get<type::SemanticTypeLookup>()
                 .data()
                 .is<type::semantic::ReferenceTypeData>() ||
            typeLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .mutability != expectedMutability ||
            typeLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .referent != referentType ||
            sourceType != nodeType.value) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        ZC_IF_SOME(handle, parameterHandle) {
          auto authority = registries.callableParameter(handle);
          if (authority == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(entry, authority) {
            bool matches = false;
            for (const auto& candidate : parameters) {
              if (candidate.key == entry.key() && candidate.type == sourceType) { matches = true; }
            }
            if (!matches) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            if (sourceAlias != zc::none) {
              ast::NodeId initializer;
              ZC_IF_SOME(value, shape.localInitializer) { initializer = value; }
              auto initializerTypeIndex = factIndex(facts.nodeTypes(), initializer);
              auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
              if (initializerTypeIndex == zc::none || initializerSpan == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              size_t initializerTypeSlot = 0;
              ZC_IF_SOME(index, initializerTypeIndex) { initializerTypeSlot = index; }
              if (facts.nodeTypes().entries()[initializerTypeSlot].value != sourceType) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              parameterReference = HirParameterReferenceExpression{
                  HirNodeId(), entry.key().clone(), sourceType, HirValueCategory::Place,
                  ZC_ASSERT_NONNULL(initializerSpan).clone()};
            }
            parameterReborrow = HirParameterReborrowExpression{
                HirNodeId(),    entry.key().clone(), zc::mv(sourceAlias),   sourceType,
                nodeType.value, expectedMutability,  valueSpanValue.clone()};
          }
        }
      }
      if (shape.returnsLocalBorrow) {
        auto sourceTypeIndex = factIndex(facts.nodeTypes(), shape.localReference);
        if (sourceTypeIndex == zc::none || local == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t sourceTypeSlot = 0;
        ZC_IF_SOME(index, sourceTypeIndex) { sourceTypeSlot = index; }
        const auto sourceType = facts.nodeTypes().entries()[sourceTypeSlot].value;
        const auto operation = static_cast<ast::UnaryOperatorKind>(
            tree.node(shape.value).payload.words[ast::kUnaryExpressionOpWord]);
        const auto expectedMutability = operation == ast::UnaryOperatorKind::Ref
                                            ? type::semantic::Mutability::Const
                                            : type::semantic::Mutability::Mutable;
        auto borrowTypeLookup = checkedModule.semanticTypes().get(nodeType.value);
        if (!borrowTypeLookup.is<type::SemanticTypeLookup>() ||
            !borrowTypeLookup.get<type::SemanticTypeLookup>()
                 .data()
                 .is<type::semantic::ReferenceTypeData>() ||
            borrowTypeLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .mutability != expectedMutability ||
            borrowTypeLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .referent != sourceType ||
            ZC_ASSERT_NONNULL(local).type != sourceType) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        localBorrow =
            HirLocalBorrowExpression{HirNodeId(),    HirLocalId(),       sourceType,
                                     nodeType.value, expectedMutability, valueSpanValue.clone()};
      }
      ast::NodeId callNode = shape.value;
      if (!shape.returnsReceiverCall && !shape.returnsDirectAggregateCall &&
          !shape.returnsDirectScalarLocalCall && !shape.returnsDirectExistentialCall) {
        ZC_IF_SOME(initializer, shape.localInitializer) { callNode = initializer; }
      }
      if (!tree.contains(callNode)) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      // The receiver self-call branch already assembles its call record from
      // the implicit header receiver; it must not re-enter the owner-local call
      // machinery below. An enum tuple-variant construction has no Call fact
      // (the constructor is not invoked); its aggregate is already built above,
      // so it skips the call machinery as well.
      if (!shape.returnsReceiverSelfCall && !isEnumConstructionCall(tree, callNode) &&
          tree.node(callNode).kind == ast::SyntaxKind::CallExpression) {
        const auto& sourceCall = tree.node(callNode);
        auto callSpan = bound.parsedModule().spanFor(sourceCall.range);
        const ast::NodeId calleeNode(sourceCall.payload.words[ast::kCallExpressionCalleeWord]);
        const ast::NodeList typeArguments{
            sourceCall.payload.words[ast::kCallExpressionTypeArgsFirstWord],
            sourceCall.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
        const ast::NodeList arguments{sourceCall.payload.words[ast::kCallExpressionArgsFirstWord],
                                      sourceCall.payload.words[ast::kCallExpressionArgsSizeWord]};
        auto calleeTypeIndex = factIndex(facts.nodeTypes(), calleeNode);
        auto checkedCallIndex = factIndex(facts.calls(), callNode);
        auto callKey = checkedNodeKey(tree, bound.parsedModule(), callNode);
        if (shape.returnsReceiverCall || shape.returnsParameterReceiverCall) {
          if (!shape.returnsParameterReceiverCall) {
            const ast::NodeId receiverNode(
                tree.node(calleeNode).payload.words[ast::kMemberExpressionObjectWord]);
            auto receiverTypeIndex = factIndex(facts.nodeTypes(), receiverNode);
            auto memberIndex = factIndex(facts.members(), calleeNode);
            auto receiverBinding = resolvedOwnerLocal(bound.bindings(), receiverNode);
            auto dispatchIndex = dispatchFactIndex(checkedModule.dispatchFacts().facts(),
                                                   ZC_ASSERT_NONNULL(callKey));
            // A devirtualized-erase local carries the concrete type as its
            // initializer type, while the receiver IdentExpr reads the erased
            // `dyn Interface` type. The type-match guard below verifies
            // local-vs-receiver type identity for ordinary receiver calls; skip
            // it for devirtualized erases where the two types differ by design.
            bool devirtualizedErase = false;
            ZC_IF_SOME(initializer, shape.localInitializer) {
              for (const auto& erased : devirtualizedEraseInitializers) {
                if (erased == initializer) {
                  devirtualizedErase = true;
                  break;
                }
              }
            }
            if (!tree.contains(calleeNode) ||
                tree.node(calleeNode).kind != ast::SyntaxKind::MemberExpression ||
                static_cast<ast::MemberAccessKind>(
                    tree.node(calleeNode).payload.words[ast::kMemberExpressionAccessWord]) !=
                    ast::MemberAccessKind::Dot ||
                !tree.contains(receiverNode) ||
                tree.node(receiverNode).kind != ast::SyntaxKind::IdentExpr ||
                receiverTypeIndex == zc::none || memberIndex == zc::none ||
                receiverBinding == zc::none || calleeTypeIndex == zc::none ||
                checkedCallIndex == zc::none || callKey == zc::none || dispatchIndex == zc::none ||
                callSpan == zc::none || local == zc::none ||
                (!devirtualizedErase &&
                 ZC_ASSERT_NONNULL(local).type !=
                     facts.nodeTypes().entries()[ZC_ASSERT_NONNULL(receiverTypeIndex)].value)) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t receiverTypeSlot = 0;
            size_t memberSlot = 0;
            size_t calleeTypeSlot = 0;
            size_t checkedCallSlot = 0;
            size_t dispatchSlot = 0;
            ZC_IF_SOME(index, receiverTypeIndex) { receiverTypeSlot = index; }
            ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
            ZC_IF_SOME(index, calleeTypeIndex) { calleeTypeSlot = index; }
            ZC_IF_SOME(index, checkedCallIndex) { checkedCallSlot = index; }
            ZC_IF_SOME(index, dispatchIndex) { dispatchSlot = index; }
            const auto& member = facts.members().entries()[memberSlot].value;
            const auto& invocation = facts.calls().entries()[checkedCallSlot].value.invocation;
            const auto& selected = invocation.selected.variant();
            const auto& dispatch = checkedModule.dispatchFacts().facts()[dispatchSlot];
            const auto& target = dispatch.fact.target.variant();
            const auto& transform = dispatch.fact.resultTransform.variant();
            // A receiver call selects either an inherent method
            // (ConcreteMethodCallable) or a devirtualized impl method
            // (ImplMethodCallable). Both lower through the same direct-call
            // carrier; only the method definition is needed.
            const auto selectedMethod = [&]() -> identity::DefId {
              if (selected.is<checker::checked::ConcreteMethodCallable>()) {
                return selected.get<checker::checked::ConcreteMethodCallable>().method;
              }
              if (selected.is<checker::checked::DynMethodCallable>()) {
                return selected.get<checker::checked::DynMethodCallable>().method;
              }
              return selected.get<checker::checked::ImplMethodCallable>().method;
            };
            const auto targetMethod = [&]() -> identity::DefId {
              if (target.is<checker::dispatch::ConcreteMethodTarget>()) {
                return target.get<checker::dispatch::ConcreteMethodTarget>().method;
              }
              if (target.is<checker::dispatch::DynMethodTarget>()) {
                return target.get<checker::dispatch::DynMethodTarget>().method;
              }
              return target.get<checker::dispatch::ImplMethodTarget>().method;
            };
            // A live-erase receiver carries the existential type rather than the
            // concrete type, so the same type-identity guards that
            // devirtualizedErase skips must also be skipped here.
            const bool liveErase = selected.is<checker::checked::DynMethodCallable>();
            const bool erasedReceiver = devirtualizedErase || liveErase;
            bool dispatchOwnerMatches = false;
            ZC_IF_SOME(owner, dispatch.owner) {
              dispatchOwnerMatches = owner == definition.definition;
            }
            if (!(selected.is<checker::checked::ConcreteMethodCallable>() ||
                  selected.is<checker::checked::ImplMethodCallable>() ||
                  selected.is<checker::checked::DynMethodCallable>()) ||
                !(target.is<checker::dispatch::ConcreteMethodTarget>() ||
                  target.is<checker::dispatch::ImplMethodTarget>() ||
                  target.is<checker::dispatch::DynMethodTarget>()) ||
                !transform.is<checker::dispatch::IdentityResultTransform>() ||
                selectedMethod() != member.member || targetMethod() != member.member ||
                member.node != calleeNode ||
                (!erasedReceiver &&
                 member.receiverType != facts.nodeTypes().entries()[receiverTypeSlot].value) ||
                member.memberType != facts.nodeTypes().entries()[calleeTypeSlot].value ||
                member.adjustment != zc::none || invocation.calleeType != member.memberType ||
                invocation.successType != nodeType.value ||
                invocation.resultType != nodeType.value || invocation.receiver == zc::none ||
                invocation.receiverMode == zc::none || invocation.receiverAdjustment == zc::none ||
                invocation.arguments.size() != arguments.size ||
                invocation.substitutions != zc::none || invocation.witnesses != zc::none ||
                invocation.raises != zc::none || !dispatchOwnerMatches ||
                dispatch.fact.receiver == zc::none ||
                dispatch.fact.arguments.size() != arguments.size ||
                dispatch.fact.successType != nodeType.value ||
                dispatch.fact.resultType != nodeType.value ||
                dispatch.fact.substitutions != zc::none || dispatch.fact.witnesses != zc::none ||
                dispatch.fact.raises != zc::none ||
                !sameSpan(facts.calls().entries()[checkedCallSlot].value.sourceSpan,
                          ZC_ASSERT_NONNULL(callSpan)) ||
                !sameSpan(dispatch.fact.sourceSpan, ZC_ASSERT_NONNULL(callSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            ZC_IF_SOME(receiver, invocation.receiver) {
              ZC_IF_SOME(mode, invocation.receiverMode) {
                ZC_IF_SOME(adjustment, invocation.receiverAdjustment) {
                  // Both receiver modes lower through the same carrier: a
                  // shared call borrows a shared reference into the receiver
                  // temporary and leaves it unactivated; a mutable call borrows
                  // a mutable reference and activates the temporary.
                  const bool sharedCall = mode == checker::checked::ReceiverMode::Shared;
                  const auto expectedMutability = sharedCall ? type::semantic::Mutability::Const
                                                             : type::semantic::Mutability::Mutable;
                  const auto expectedMode = sharedCall ? checker::checked::ReceiverMode::Shared
                                                       : checker::checked::ReceiverMode::Mutable;
                  const auto expectedStep =
                      sharedCall ? checker::checked::ReceiverAdjustmentStep::BorrowShared
                                 : checker::checked::ReceiverAdjustmentStep::BorrowMutable;
                  if (mode != checker::checked::ReceiverMode::Shared &&
                      mode != checker::checked::ReceiverMode::Mutable) {
                    return rejectHirCapability<HirModuleCandidate>(
                        definition.definition, registries,
                        ir::IrFailureKind::UnsupportedSourceConstruct,
                        ZC_ASSERT_NONNULL(callSpan).clone());
                  }
                  auto receiverParameter =
                      checkedModule.semanticTypes().get(receiver.parameterType);
                  if (!receiverParameter.is<type::SemanticTypeLookup>() ||
                      !receiverParameter.get<type::SemanticTypeLookup>()
                           .data()
                           .is<type::semantic::ReferenceTypeData>() ||
                      receiverParameter.get<type::SemanticTypeLookup>()
                              .data()
                              .get<type::semantic::ReferenceTypeData>()
                              .mutability != expectedMutability ||
                      (!erasedReceiver && receiverParameter.get<type::SemanticTypeLookup>()
                                                  .data()
                                                  .get<type::semantic::ReferenceTypeData>()
                                                  .referent != receiver.sourceType)) {
                    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                         ir::IrFailureKind::InvalidFact, module,
                                                         registries, ordinal + 2);
                  }
                  if (receiver.sourceNode != receiverNode ||
                      (!erasedReceiver &&
                       receiver.sourceType !=
                           facts.nodeTypes().entries()[receiverTypeSlot].value) ||
                      receiver.adjustment != zc::none || mode != expectedMode ||
                      adjustment.source != receiver.sourceType ||
                      adjustment.destination != receiver.parameterType ||
                      adjustment.steps.size() != 1 || adjustment.steps[0] != expectedStep) {
                    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                         ir::IrFailureKind::InvalidFact, module,
                                                         registries, ordinal + 2);
                  }
                  zc::Vector<checker::checked::ReceiverAdjustmentStep> steps;
                  steps.add(expectedStep);
                  zc::Vector<HirDirectCallArgument> callArguments;
                  const auto argumentNodes = tree.list(arguments);
                  for (size_t index = 0; index < argumentNodes.size(); ++index) {
                    const auto argument = argumentNodes[index];
                    auto argumentTypeIndex = factIndex(facts.nodeTypes(), argument);
                    auto literalIndex = factIndex(facts.literals(), argument);
                    auto argumentSpan = bound.parsedModule().spanFor(tree.node(argument).range);
                    // A field-projection argument on the receiver local
                    // (`cell.echo(cell.value)`) lowers through a local+field
                    // carrier rather than a literal constant. The two `cell`
                    // identifiers are distinct AST nodes, so the match is by
                    // owner-local binding, not by NodeId.
                    const ast::NodeId argumentObject(
                        tree.node(argument).payload.words[ast::kMemberExpressionObjectWord]);
                    const bool isReceiverFieldArgument =
                        tree.contains(argument) &&
                        tree.node(argument).kind == ast::SyntaxKind::MemberExpression &&
                        resolvedOwnerLocal(bound.bindings(), argumentObject) == receiverBinding;
                    if (isReceiverFieldArgument) {
                      auto argumentMemberIndex = factIndex(facts.members(), argument);
                      if (argumentTypeIndex == zc::none || argumentMemberIndex == zc::none ||
                          argumentSpan == zc::none) {
                        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                             ir::IrFailureKind::MissingRequiredFact,
                                                             module, registries, ordinal + 2);
                      }
                      size_t argumentTypeSlot = 0;
                      size_t argumentMemberSlot = 0;
                      ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
                      ZC_IF_SOME(value, argumentMemberIndex) { argumentMemberSlot = value; }
                      const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
                      const auto& argumentMember =
                          facts.members().entries()[argumentMemberSlot].value;
                      const auto& checkedArgument = invocation.arguments[index];
                      if (checkedArgument.sourceNode != argument ||
                          checkedArgument.sourceType != argumentType ||
                          checkedArgument.parameterType != argumentType ||
                          checkedArgument.adjustment != zc::none ||
                          argumentMember.node != argument ||
                          argumentMember.memberType != argumentType ||
                          argumentMember.receiverType != receiver.sourceType ||
                          argumentMember.adjustment != zc::none) {
                        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                             ir::IrFailureKind::InvalidFact, module,
                                                             registries, ordinal + 2);
                      }
                      zc::Maybe<checker::checked::CanonicalConstValue> noValue;
                      zc::Maybe<identity::CallableParameterKey> noParameter;
                      zc::Maybe<HirLocalId> receiverLocal = hirLocalId(1);
                      callArguments.add(
                          HirDirectCallArgument{argumentType, zc::mv(noValue), zc::mv(noParameter),
                                                zc::mv(receiverLocal), argumentMember.member,
                                                zc::none, ZC_ASSERT_NONNULL(argumentSpan).clone()});
                      continue;
                    }
                    // A field-comparison argument on the receiver local
                    // (`cell.compare(cell.value > 0)`) lowers through a
                    // local+field+operator+literal carrier: the caller computes
                    // the comparison at runtime and passes the bool result.
                    if (tree.contains(argument) &&
                        tree.node(argument).kind == ast::SyntaxKind::BinaryExpr) {
                      const auto syntaxOp = static_cast<ast::BinaryOperatorKind>(
                          tree.node(argument).payload.words[ast::kBinaryExprOpWord]);
                      auto operatorKind = checker::OperatorKind::fromBinary(syntaxOp);
                      if (operatorKind != zc::none) {
                        const auto& variant = ZC_ASSERT_NONNULL(operatorKind).variant();
                        if (variant.is<checker::PrimitiveOperation>()) {
                          const auto operation = variant.get<checker::PrimitiveOperation>();
                          if (operation == checker::PrimitiveOperation::Eq ||
                              operation == checker::PrimitiveOperation::Ne ||
                              operation == checker::PrimitiveOperation::Lt ||
                              operation == checker::PrimitiveOperation::Le ||
                              operation == checker::PrimitiveOperation::Gt ||
                              operation == checker::PrimitiveOperation::Ge) {
                            const ast::NodeId left(
                                tree.node(argument).payload.words[ast::kBinaryExprLhsWord]);
                            const ast::NodeId right(
                                tree.node(argument).payload.words[ast::kBinaryExprRhsWord]);
                            if (tree.contains(left) && tree.contains(right)) {
                              auto isReceiverField = [&](ast::NodeId operand) {
                                if (!tree.contains(operand) ||
                                    tree.node(operand).kind != ast::SyntaxKind::MemberExpression) {
                                  return false;
                                }
                                const ast::NodeId fieldObject(
                                    tree.node(operand)
                                        .payload.words[ast::kMemberExpressionObjectWord]);
                                return resolvedOwnerLocal(bound.bindings(), fieldObject) ==
                                       receiverBinding;
                              };
                              const bool leftField = isReceiverField(left);
                              const bool rightField = isReceiverField(right);
                              if (leftField != rightField) {
                                const ast::NodeId fieldOperand = leftField ? left : right;
                                const ast::NodeId literalOperand = leftField ? right : left;
                                if (isScalarLiteral(tree.node(literalOperand).kind)) {
                                  auto argumentMemberIndex =
                                      factIndex(facts.members(), fieldOperand);
                                  auto comparisonLiteralIndex =
                                      factIndex(facts.literals(), literalOperand);
                                  if (argumentTypeIndex != zc::none &&
                                      argumentMemberIndex != zc::none &&
                                      comparisonLiteralIndex != zc::none &&
                                      argumentSpan != zc::none) {
                                    size_t argumentTypeSlot = 0;
                                    size_t argumentMemberSlot = 0;
                                    size_t comparisonLiteralSlot = 0;
                                    ZC_IF_SOME(value, argumentTypeIndex) {
                                      argumentTypeSlot = value;
                                    }
                                    ZC_IF_SOME(value, argumentMemberIndex) {
                                      argumentMemberSlot = value;
                                    }
                                    ZC_IF_SOME(value, comparisonLiteralIndex) {
                                      comparisonLiteralSlot = value;
                                    }
                                    const auto comparisonType =
                                        facts.nodeTypes().entries()[argumentTypeSlot].value;
                                    const auto& comparisonMember =
                                        facts.members().entries()[argumentMemberSlot].value;
                                    const auto& comparisonLiteral =
                                        facts.literals().entries()[comparisonLiteralSlot].value;
                                    const auto& checkedArgument = invocation.arguments[index];
                                    if (checkedArgument.sourceNode == argument &&
                                        checkedArgument.sourceType == comparisonType &&
                                        checkedArgument.parameterType == comparisonType &&
                                        checkedArgument.adjustment == zc::none &&
                                        comparisonMember.node == fieldOperand &&
                                        comparisonMember.receiverType == receiver.sourceType &&
                                        comparisonMember.adjustment == zc::none &&
                                        comparisonLiteral.node == literalOperand) {
                                      zc::Maybe<identity::CallableParameterKey> noParameter;
                                      zc::Maybe<HirLocalId> receiverLocal = hirLocalId(1);
                                      callArguments.add(HirDirectCallArgument{
                                          comparisonType, comparisonLiteral.literal.clone(),
                                          zc::mv(noParameter), zc::mv(receiverLocal),
                                          comparisonMember.member, operation,
                                          ZC_ASSERT_NONNULL(argumentSpan).clone()});
                                      continue;
                                    }
                                  }
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                    if (!tree.contains(argument) || !isScalarLiteral(tree.node(argument).kind) ||
                        argumentTypeIndex == zc::none || literalIndex == zc::none ||
                        argumentSpan == zc::none) {
                      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                           ir::IrFailureKind::MissingRequiredFact,
                                                           module, registries, ordinal + 2);
                    }
                    size_t argumentTypeSlot = 0;
                    size_t literalSlot = 0;
                    ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
                    ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
                    const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
                    const auto& checkedArgument = invocation.arguments[index];
                    const auto& literal = facts.literals().entries()[literalSlot].value;
                    if (checkedArgument.sourceNode != argument ||
                        checkedArgument.sourceType != argumentType ||
                        checkedArgument.parameterType != argumentType ||
                        checkedArgument.adjustment != zc::none || literal.node != argument ||
                        literal.type != argumentType ||
                        !sameSpan(literal.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
                      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                           ir::IrFailureKind::InvalidFact, module,
                                                           registries, ordinal + 2);
                    }
                    callArguments.add(HirDirectCallArgument{
                        argumentType, literal.literal.clone(),
                        zc::Maybe<identity::CallableParameterKey>(), zc::none, zc::none, zc::none,
                        ZC_ASSERT_NONNULL(argumentSpan).clone()});
                  }
                  receiverCall = HirReceiverCallExpression{
                      HirNodeId(),
                      HirNodeId(),
                      member.member,
                      invocation.calleeType,
                      receiver.sourceType,
                      receiver.parameterType,
                      mode,
                      zc::mv(steps),
                      invocation.resultType,
                      zc::mv(callArguments),
                      ZC_ASSERT_NONNULL(callSpan).clone(),
                      liveErase ? zc::Maybe<identity::DefId>(
                                      selected.get<checker::checked::DynMethodCallable>().interface)
                                : zc::Maybe<identity::DefId>()};
                }
              }
            }
          }  // end !returnsParameterReceiverCall guard
          if (shape.returnsParameterReceiverCall) {
            // Single-statement dyn-trait method call on a callable parameter:
            // `fn f(a: dyn I) -> T { return a.m(); }`. The receiver is a
            // parameter whose declared type is existential; the concrete type
            // is not statically known, so the call dispatches indirectly
            // through a vtable at runtime.
            const ast::NodeId receiverNode(
                tree.node(calleeNode).payload.words[ast::kMemberExpressionObjectWord]);
            auto receiverTypeIndex = factIndex(facts.nodeTypes(), receiverNode);
            auto memberIndex = factIndex(facts.members(), calleeNode);
            auto dispatchIndex = dispatchFactIndex(checkedModule.dispatchFacts().facts(),
                                                   ZC_ASSERT_NONNULL(callKey));
            if (!tree.contains(calleeNode) ||
                tree.node(calleeNode).kind != ast::SyntaxKind::MemberExpression ||
                static_cast<ast::MemberAccessKind>(
                    tree.node(calleeNode).payload.words[ast::kMemberExpressionAccessWord]) !=
                    ast::MemberAccessKind::Dot ||
                !tree.contains(receiverNode) ||
                tree.node(receiverNode).kind != ast::SyntaxKind::IdentExpr ||
                receiverTypeIndex == zc::none || memberIndex == zc::none ||
                calleeTypeIndex == zc::none || checkedCallIndex == zc::none ||
                callKey == zc::none || dispatchIndex == zc::none || callSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t receiverTypeSlot = 0;
            size_t memberSlot = 0;
            size_t calleeTypeSlot = 0;
            size_t checkedCallSlot = 0;
            size_t dispatchSlot = 0;
            ZC_IF_SOME(index, receiverTypeIndex) { receiverTypeSlot = index; }
            ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
            ZC_IF_SOME(index, calleeTypeIndex) { calleeTypeSlot = index; }
            ZC_IF_SOME(index, checkedCallIndex) { checkedCallSlot = index; }
            ZC_IF_SOME(index, dispatchIndex) { dispatchSlot = index; }
            const auto& member = facts.members().entries()[memberSlot].value;
            const auto& invocation = facts.calls().entries()[checkedCallSlot].value.invocation;
            const auto& selected = invocation.selected.variant();
            const auto& dispatch = checkedModule.dispatchFacts().facts()[dispatchSlot];
            const auto& target = dispatch.fact.target.variant();
            const auto& transform = dispatch.fact.resultTransform.variant();
            if (!selected.is<checker::checked::DynMethodCallable>() ||
                !target.is<checker::dispatch::DynMethodTarget>() ||
                !transform.is<checker::dispatch::IdentityResultTransform>() ||
                selected.get<checker::checked::DynMethodCallable>().method != member.member ||
                target.get<checker::dispatch::DynMethodTarget>().method != member.member ||
                member.node != calleeNode ||
                member.memberType != facts.nodeTypes().entries()[calleeTypeSlot].value ||
                member.adjustment != zc::none || invocation.calleeType != member.memberType ||
                invocation.successType != nodeType.value ||
                invocation.resultType != nodeType.value || invocation.receiver == zc::none ||
                invocation.receiverMode == zc::none || invocation.receiverAdjustment == zc::none ||
                invocation.arguments.size() != arguments.size ||
                invocation.substitutions != zc::none || invocation.witnesses != zc::none ||
                invocation.raises != zc::none || dispatch.fact.receiver == zc::none ||
                dispatch.fact.arguments.size() != arguments.size ||
                dispatch.fact.successType != nodeType.value ||
                dispatch.fact.resultType != nodeType.value ||
                dispatch.fact.substitutions != zc::none || dispatch.fact.witnesses != zc::none ||
                dispatch.fact.raises != zc::none ||
                !sameSpan(facts.calls().entries()[checkedCallSlot].value.sourceSpan,
                          ZC_ASSERT_NONNULL(callSpan)) ||
                !sameSpan(dispatch.fact.sourceSpan, ZC_ASSERT_NONNULL(callSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            ZC_IF_SOME(receiver, invocation.receiver) {
              ZC_IF_SOME(mode, invocation.receiverMode) {
                ZC_IF_SOME(adjustment, invocation.receiverAdjustment) {
                  const bool sharedCall = mode == checker::checked::ReceiverMode::Shared;
                  const auto expectedMutability = sharedCall ? type::semantic::Mutability::Const
                                                             : type::semantic::Mutability::Mutable;
                  const auto expectedMode = sharedCall ? checker::checked::ReceiverMode::Shared
                                                       : checker::checked::ReceiverMode::Mutable;
                  const auto expectedStep =
                      sharedCall ? checker::checked::ReceiverAdjustmentStep::BorrowShared
                                 : checker::checked::ReceiverAdjustmentStep::BorrowMutable;
                  if (mode != checker::checked::ReceiverMode::Shared &&
                      mode != checker::checked::ReceiverMode::Mutable) {
                    return rejectHirCapability<HirModuleCandidate>(
                        definition.definition, registries,
                        ir::IrFailureKind::UnsupportedSourceConstruct,
                        ZC_ASSERT_NONNULL(callSpan).clone());
                  }
                  auto receiverParameter =
                      checkedModule.semanticTypes().get(receiver.parameterType);
                  if (!receiverParameter.is<type::SemanticTypeLookup>() ||
                      !receiverParameter.get<type::SemanticTypeLookup>()
                           .data()
                           .is<type::semantic::ReferenceTypeData>() ||
                      receiverParameter.get<type::SemanticTypeLookup>()
                              .data()
                              .get<type::semantic::ReferenceTypeData>()
                              .mutability != expectedMutability) {
                    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                         ir::IrFailureKind::InvalidFact, module,
                                                         registries, ordinal + 2);
                  }
                  if (receiver.sourceNode != receiverNode ||
                      receiver.sourceType != facts.nodeTypes().entries()[receiverTypeSlot].value ||
                      receiver.adjustment != zc::none || mode != expectedMode ||
                      adjustment.source != receiver.sourceType ||
                      adjustment.destination != receiver.parameterType ||
                      adjustment.steps.size() != 1 || adjustment.steps[0] != expectedStep) {
                    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                         ir::IrFailureKind::InvalidFact, module,
                                                         registries, ordinal + 2);
                  }
                  zc::Vector<checker::checked::ReceiverAdjustmentStep> steps;
                  steps.add(expectedStep);
                  zc::Vector<HirDirectCallArgument> callArguments;
                  const auto argumentNodes = tree.list(arguments);
                  for (size_t index = 0; index < argumentNodes.size(); ++index) {
                    const auto argument = argumentNodes[index];
                    auto argumentTypeIndex = factIndex(facts.nodeTypes(), argument);
                    auto literalIndex = factIndex(facts.literals(), argument);
                    auto argumentSpan = bound.parsedModule().spanFor(tree.node(argument).range);
                    if (argumentTypeIndex == zc::none || literalIndex == zc::none ||
                        argumentSpan == zc::none) {
                      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                           ir::IrFailureKind::MissingRequiredFact,
                                                           module, registries, ordinal + 2);
                    }
                    size_t argumentTypeSlot = 0;
                    size_t literalSlot = 0;
                    ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
                    ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
                    const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
                    const auto& checkedArgument = invocation.arguments[index];
                    const auto& literal = facts.literals().entries()[literalSlot].value;
                    if (checkedArgument.sourceNode != argument ||
                        checkedArgument.sourceType != argumentType ||
                        checkedArgument.parameterType != argumentType ||
                        checkedArgument.adjustment != zc::none || literal.node != argument ||
                        literal.type != argumentType ||
                        !sameSpan(literal.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
                      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                           ir::IrFailureKind::InvalidFact, module,
                                                           registries, ordinal + 2);
                    }
                    callArguments.add(HirDirectCallArgument{
                        argumentType, literal.literal.clone(),
                        zc::Maybe<identity::CallableParameterKey>(), zc::none, zc::none, zc::none,
                        ZC_ASSERT_NONNULL(argumentSpan).clone()});
                  }
                  receiverCall = HirReceiverCallExpression{
                      HirNodeId(),
                      HirNodeId(),
                      member.member,
                      invocation.calleeType,
                      receiver.sourceType,
                      receiver.parameterType,
                      mode,
                      zc::mv(steps),
                      invocation.resultType,
                      zc::mv(callArguments),
                      ZC_ASSERT_NONNULL(callSpan).clone(),
                      zc::Maybe<identity::DefId>(
                          selected.get<checker::checked::DynMethodCallable>().interface)};
                  // Resolve the receiver parameter key so the lowering function
                  // can allocate a parameter reference node for the receiver.
                  auto receiverParameterHandle =
                      resolvedCallableParameter(bound.bindings(), receiverNode);
                  if (receiverParameterHandle == zc::none) {
                    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                         ir::IrFailureKind::MissingRequiredFact,
                                                         module, registries, ordinal + 2);
                  }
                  ZC_IF_SOME(handle, receiverParameterHandle) {
                    auto authority = registries.callableParameter(handle);
                    ZC_IF_SOME(entry, authority) {
                      for (const auto& parameter : parameters) {
                        if (parameter.key == entry.key() && parameter.type == receiver.sourceType) {
                          receiverCallParameterKey = entry.key().clone();
                        }
                      }
                    }
                  }
                  if (receiverCallParameterKey == zc::none) {
                    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                         ir::IrFailureKind::InvalidFact, module,
                                                         registries, ordinal + 2);
                  }
                  auto receiverSpan = bound.parsedModule().spanFor(tree.node(receiverNode).range);
                  if (receiverSpan == zc::none) {
                    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                         ir::IrFailureKind::MissingRequiredFact,
                                                         module, registries, ordinal + 2);
                  }
                  receiverCallParameterSpan = ZC_ASSERT_NONNULL(receiverSpan).clone();
                }
              }
            }
          }
          if (receiverCall == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          if (shape.hasDiscardedReceiverCallStatement &&
              ZC_ASSERT_NONNULL(receiverCall).receiverMode ==
                  checker::checked::ReceiverMode::Mutable) {
            // The discarded-call composition lowers a mutable statement call
            // followed only by a SHARED trailing borrow. A mutating trailing
            // call is well-formed source outside this slice; drain the owner
            // definition with the capability code here, in the builder, so the
            // shared-only verifier and MIR gates below never see it and fail it
            // as an invalid-fact invariant.
            return rejectHirCapability<HirModuleCandidate>(
                definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
                definition.source.clone());
          }
          if (shape.hasDiscardedReceiverCallStatement) {
            // The middle statement is a second, discarded owner-local receiver
            // call (`cell.set(42);`): a mutable borrow, one scalar-literal
            // argument, and a Unit result. Its node id lands directly in the
            // block statement list; it shares the owner local with the trailing
            // get call but has its own receiver source node and span.
            const ast::NodeId statementNode = shape.discardedCallStatement;
            const ast::NodeId statementCall(
                tree.node(statementNode).payload.words[ast::kExpressionStatementExpressionWord]);
            if (!tree.contains(statementCall) ||
                tree.node(statementCall).kind != ast::SyntaxKind::CallExpression) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto& statementSourceCall = tree.node(statementCall);
            auto statementCallSpan = bound.parsedModule().spanFor(statementSourceCall.range);
            const ast::NodeId statementCallee(
                statementSourceCall.payload.words[ast::kCallExpressionCalleeWord]);
            const ast::NodeList statementTypeArguments{
                statementSourceCall.payload.words[ast::kCallExpressionTypeArgsFirstWord],
                statementSourceCall.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
            const ast::NodeList statementArguments{
                statementSourceCall.payload.words[ast::kCallExpressionArgsFirstWord],
                statementSourceCall.payload.words[ast::kCallExpressionArgsSizeWord]};
            if (!tree.contains(statementCallee) ||
                tree.node(statementCallee).kind != ast::SyntaxKind::MemberExpression ||
                static_cast<ast::MemberAccessKind>(
                    tree.node(statementCallee).payload.words[ast::kMemberExpressionAccessWord]) !=
                    ast::MemberAccessKind::Dot ||
                !tree.contains(statementTypeArguments) || !statementTypeArguments.empty() ||
                !tree.contains(statementArguments) || statementCallSpan == zc::none ||
                local == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const ast::NodeId statementReceiverNode(
                tree.node(statementCallee).payload.words[ast::kMemberExpressionObjectWord]);
            const ast::NodeId trailingCallee(
                tree.node(callNode).payload.words[ast::kCallExpressionCalleeWord]);
            const ast::NodeId trailingReceiverNode(
                tree.node(trailingCallee).payload.words[ast::kMemberExpressionObjectWord]);
            auto statementReceiverTypeIndex = factIndex(facts.nodeTypes(), statementReceiverNode);
            auto statementCalleeTypeIndex = factIndex(facts.nodeTypes(), statementCallee);
            auto statementMemberIndex = factIndex(facts.members(), statementCallee);
            auto statementCallIndex = factIndex(facts.calls(), statementCall);
            auto statementCallKey = checkedNodeKey(tree, bound.parsedModule(), statementCall);
            auto statementReceiverBinding =
                resolvedOwnerLocal(bound.bindings(), statementReceiverNode);
            auto trailingReceiverBinding =
                resolvedOwnerLocal(bound.bindings(), trailingReceiverNode);
            auto statementDispatchIndex =
                statementCallKey == zc::none
                    ? zc::none
                    : dispatchFactIndex(checkedModule.dispatchFacts().facts(),
                                        ZC_ASSERT_NONNULL(statementCallKey));
            // A devirtualized-erase local carries the concrete type as its
            // initializer type, while the statement receiver IdentExpr reads
            // the erased `dyn Interface` type. The type-match guard below
            // verifies local-vs-receiver type identity for ordinary receiver
            // calls; skip it for devirtualized erases where the two types
            // differ by design.
            bool stmtDevirtualizedErase = false;
            ZC_IF_SOME(initializer, shape.localInitializer) {
              for (const auto& erased : devirtualizedEraseInitializers) {
                if (erased == initializer) {
                  stmtDevirtualizedErase = true;
                  break;
                }
              }
            }
            if (!tree.contains(statementReceiverNode) ||
                tree.node(statementReceiverNode).kind != ast::SyntaxKind::IdentExpr ||
                statementReceiverTypeIndex == zc::none || statementCalleeTypeIndex == zc::none ||
                statementMemberIndex == zc::none || statementCallIndex == zc::none ||
                statementCallKey == zc::none || statementDispatchIndex == zc::none ||
                statementReceiverBinding == zc::none || trailingReceiverBinding == zc::none ||
                ZC_ASSERT_NONNULL(statementReceiverBinding) !=
                    ZC_ASSERT_NONNULL(trailingReceiverBinding) ||
                !ownerLocalMatches(bound.definitions(), ZC_ASSERT_NONNULL(statementReceiverBinding),
                                   shape.localPattern, tree) ||
                (!stmtDevirtualizedErase &&
                 ZC_ASSERT_NONNULL(local).type !=
                     facts.nodeTypes()
                         .entries()[ZC_ASSERT_NONNULL(statementReceiverTypeIndex)]
                         .value)) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t statementReceiverTypeSlot = 0;
            size_t statementCalleeTypeSlot = 0;
            size_t statementMemberSlot = 0;
            size_t statementCallSlot = 0;
            size_t statementDispatchSlot = 0;
            ZC_IF_SOME(index, statementReceiverTypeIndex) { statementReceiverTypeSlot = index; }
            ZC_IF_SOME(index, statementCalleeTypeIndex) { statementCalleeTypeSlot = index; }
            ZC_IF_SOME(index, statementMemberIndex) { statementMemberSlot = index; }
            ZC_IF_SOME(index, statementCallIndex) { statementCallSlot = index; }
            ZC_IF_SOME(index, statementDispatchIndex) { statementDispatchSlot = index; }
            const auto& statementMember = facts.members().entries()[statementMemberSlot].value;
            const auto& statementInvocation =
                facts.calls().entries()[statementCallSlot].value.invocation;
            const auto& statementSelected = statementInvocation.selected.variant();
            const auto& statementDispatch =
                checkedModule.dispatchFacts().facts()[statementDispatchSlot];
            const auto& statementTarget = statementDispatch.fact.target.variant();
            const auto& statementTransform = statementDispatch.fact.resultTransform.variant();
            // Slice capability gate: a discarded statement-position receiver
            // call is lowered in one of two shapes. The first is a
            // one-literal-argument mutable call whose result is Unit. The
            // second is a devirtualized-erase shared, zero-argument,
            // value-returning call whose result is discarded into a dead
            // temporary. Every other well-formed discarded call (a non-Unit
            // result on a concrete receiver, a shared receiver with arguments,
            // a different arity, or a non-literal argument) is valid source
            // outside this slice, so it drains as the user-facing capability
            // code instead of an invalid-fact invariant. The checker already
            // drains arity, identifier-argument, and argument-type errors
            // earlier; this guards the result kind and receiver mode that a
            // discarded-call position otherwise lets through.
            bool statementArgumentAdmitted = statementArguments.size == 1;
            if (statementArgumentAdmitted) {
              const ast::NodeId onlyArgument = tree.list(statementArguments)[0];
              statementArgumentAdmitted =
                  tree.contains(onlyArgument) && isScalarLiteral(tree.node(onlyArgument).kind);
            }
            const bool statementModeMutable = statementInvocation.receiverMode != zc::none &&
                                              ZC_ASSERT_NONNULL(statementInvocation.receiverMode) ==
                                                  checker::checked::ReceiverMode::Mutable;
            const bool statementResultUnit =
                isUnitSemanticType(checkedModule.semanticTypes(),
                                   statementInvocation.successType) &&
                isUnitSemanticType(checkedModule.semanticTypes(), statementInvocation.resultType) &&
                isUnitSemanticType(checkedModule.semanticTypes(),
                                   statementDispatch.fact.successType) &&
                isUnitSemanticType(checkedModule.semanticTypes(),
                                   statementDispatch.fact.resultType);
            const bool statementSharedDevirt =
                stmtDevirtualizedErase && statementInvocation.receiverMode != zc::none &&
                ZC_ASSERT_NONNULL(statementInvocation.receiverMode) ==
                    checker::checked::ReceiverMode::Shared &&
                statementArguments.size == 0 && !statementResultUnit;
            if ((!statementArgumentAdmitted || !statementModeMutable || !statementResultUnit) &&
                !statementSharedDevirt) {
              return rejectHirCapability<HirModuleCandidate>(
                  definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
                  ZC_ASSERT_NONNULL(statementCallSpan).clone());
            }
            bool statementDispatchOwnerMatches = false;
            ZC_IF_SOME(owner, statementDispatch.owner) {
              statementDispatchOwnerMatches = owner == definition.definition;
            }
            // A statement-level receiver call resolves to either a concrete
            // method or a devirtualized impl method; both lower through the
            // same direct-call carrier.
            const auto stmtSelectedMethod = [&]() -> identity::DefId {
              if (statementSelected.is<checker::checked::ConcreteMethodCallable>()) {
                return statementSelected.get<checker::checked::ConcreteMethodCallable>().method;
              }
              if (statementSelected.is<checker::checked::DynMethodCallable>()) {
                return statementSelected.get<checker::checked::DynMethodCallable>().method;
              }
              return statementSelected.get<checker::checked::ImplMethodCallable>().method;
            };
            const auto stmtTargetMethod = [&]() -> identity::DefId {
              if (statementTarget.is<checker::dispatch::ConcreteMethodTarget>()) {
                return statementTarget.get<checker::dispatch::ConcreteMethodTarget>().method;
              }
              if (statementTarget.is<checker::dispatch::DynMethodTarget>()) {
                return statementTarget.get<checker::dispatch::DynMethodTarget>().method;
              }
              return statementTarget.get<checker::dispatch::ImplMethodTarget>().method;
            };
            const bool stmtLiveErase = statementSelected.is<checker::checked::DynMethodCallable>();
            const bool stmtErasedReceiver = stmtDevirtualizedErase || stmtLiveErase;
            if (!(statementSelected.is<checker::checked::ConcreteMethodCallable>() ||
                  statementSelected.is<checker::checked::ImplMethodCallable>() ||
                  statementSelected.is<checker::checked::DynMethodCallable>()) ||
                !(statementTarget.is<checker::dispatch::ConcreteMethodTarget>() ||
                  statementTarget.is<checker::dispatch::ImplMethodTarget>() ||
                  statementTarget.is<checker::dispatch::DynMethodTarget>()) ||
                !statementTransform.is<checker::dispatch::IdentityResultTransform>() ||
                stmtSelectedMethod() != statementMember.member ||
                stmtTargetMethod() != statementMember.member ||
                statementMember.node != statementCallee ||
                (!stmtErasedReceiver &&
                 statementMember.receiverType !=
                     facts.nodeTypes().entries()[statementReceiverTypeSlot].value) ||
                statementMember.memberType !=
                    facts.nodeTypes().entries()[statementCalleeTypeSlot].value ||
                statementMember.adjustment != zc::none ||
                statementInvocation.calleeType != statementMember.memberType ||
                (!statementSharedDevirt && (!isUnitSemanticType(checkedModule.semanticTypes(),
                                                                statementInvocation.successType) ||
                                            !isUnitSemanticType(checkedModule.semanticTypes(),
                                                                statementInvocation.resultType) ||
                                            statementInvocation.arguments.size() != 1)) ||
                statementInvocation.receiver == zc::none ||
                statementInvocation.receiverMode == zc::none ||
                statementInvocation.receiverAdjustment == zc::none ||
                statementInvocation.arguments.size() != statementArguments.size ||
                statementInvocation.substitutions != zc::none ||
                statementInvocation.witnesses != zc::none ||
                statementInvocation.raises != zc::none || !statementDispatchOwnerMatches ||
                statementDispatch.fact.receiver == zc::none ||
                statementDispatch.fact.arguments.size() != statementArguments.size ||
                (!statementSharedDevirt &&
                 (!isUnitSemanticType(checkedModule.semanticTypes(),
                                      statementDispatch.fact.successType) ||
                  !isUnitSemanticType(checkedModule.semanticTypes(),
                                      statementDispatch.fact.resultType))) ||
                statementDispatch.fact.substitutions != zc::none ||
                statementDispatch.fact.witnesses != zc::none ||
                statementDispatch.fact.raises != zc::none ||
                !sameSpan(facts.calls().entries()[statementCallSlot].value.sourceSpan,
                          ZC_ASSERT_NONNULL(statementCallSpan)) ||
                !sameSpan(statementDispatch.fact.sourceSpan,
                          ZC_ASSERT_NONNULL(statementCallSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto& statementCheckedReceiver = ZC_ASSERT_NONNULL(statementInvocation.receiver);
            const auto stmtExpectedMode = statementSharedDevirt
                                              ? checker::checked::ReceiverMode::Shared
                                              : checker::checked::ReceiverMode::Mutable;
            const auto stmtExpectedStep =
                statementSharedDevirt ? checker::checked::ReceiverAdjustmentStep::BorrowShared
                                      : checker::checked::ReceiverAdjustmentStep::BorrowMutable;
            if (statementCheckedReceiver.sourceNode != statementReceiverNode ||
                (!stmtDevirtualizedErase &&
                 statementCheckedReceiver.sourceType !=
                     facts.nodeTypes().entries()[statementReceiverTypeSlot].value) ||
                statementCheckedReceiver.adjustment != zc::none ||
                statementInvocation.receiverMode == zc::none ||
                ZC_ASSERT_NONNULL(statementInvocation.receiverMode) != stmtExpectedMode ||
                statementInvocation.receiverAdjustment == zc::none ||
                ZC_ASSERT_NONNULL(statementInvocation.receiverAdjustment).source !=
                    statementCheckedReceiver.sourceType ||
                ZC_ASSERT_NONNULL(statementInvocation.receiverAdjustment).destination !=
                    statementCheckedReceiver.parameterType ||
                ZC_ASSERT_NONNULL(statementInvocation.receiverAdjustment).steps.size() != 1 ||
                ZC_ASSERT_NONNULL(statementInvocation.receiverAdjustment).steps[0] !=
                    stmtExpectedStep) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            auto statementReceiverParameter =
                checkedModule.semanticTypes().get(statementCheckedReceiver.parameterType);
            const auto stmtExpectedMutability = statementSharedDevirt
                                                    ? type::semantic::Mutability::Const
                                                    : type::semantic::Mutability::Mutable;
            if (!statementReceiverParameter.is<type::SemanticTypeLookup>() ||
                !statementReceiverParameter.get<type::SemanticTypeLookup>()
                     .data()
                     .is<type::semantic::ReferenceTypeData>() ||
                statementReceiverParameter.get<type::SemanticTypeLookup>()
                        .data()
                        .get<type::semantic::ReferenceTypeData>()
                        .mutability != stmtExpectedMutability ||
                (!stmtDevirtualizedErase &&
                 statementReceiverParameter.get<type::SemanticTypeLookup>()
                         .data()
                         .get<type::semantic::ReferenceTypeData>()
                         .referent != statementCheckedReceiver.sourceType)) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            auto statementReceiverSpan =
                bound.parsedModule().spanFor(tree.node(statementReceiverNode).range);
            if (statementReceiverSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            statementReceiverReference = HirLocalReferenceExpression{
                HirNodeId(), HirLocalId(), ZC_ASSERT_NONNULL(local).type, HirValueCategory::Place,
                ZC_ASSERT_NONNULL(statementReceiverSpan).clone()};
            zc::Vector<checker::checked::ReceiverAdjustmentStep> statementSteps;
            statementSteps.add(stmtExpectedStep);
            zc::Vector<HirDirectCallArgument> statementCallArguments;
            for (size_t argumentIndex = 0; argumentIndex < statementArguments.size;
                 ++argumentIndex) {
              const auto argument = tree.list(statementArguments)[argumentIndex];
              auto argumentTypeIndex = factIndex(facts.nodeTypes(), argument);
              auto literalIndex = factIndex(facts.literals(), argument);
              auto argumentSpan = bound.parsedModule().spanFor(tree.node(argument).range);
              if (!tree.contains(argument) || !isScalarLiteral(tree.node(argument).kind) ||
                  argumentTypeIndex == zc::none || literalIndex == zc::none ||
                  argumentSpan == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              size_t argumentTypeSlot = 0;
              size_t literalSlot = 0;
              ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
              ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
              const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
              const auto& checkedArgument = statementInvocation.arguments[argumentIndex];
              const auto& literal = facts.literals().entries()[literalSlot].value;
              if (checkedArgument.sourceNode != argument ||
                  checkedArgument.sourceType != argumentType ||
                  checkedArgument.parameterType != argumentType ||
                  checkedArgument.adjustment != zc::none || literal.node != argument ||
                  literal.type != argumentType ||
                  !sameSpan(literal.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              statementCallArguments.add(HirDirectCallArgument{
                  argumentType, literal.literal.clone(),
                  zc::Maybe<identity::CallableParameterKey>(), zc::none, zc::none, zc::none,
                  ZC_ASSERT_NONNULL(argumentSpan).clone()});
            }
            statementReceiverCall = HirReceiverCallExpression{
                HirNodeId(),
                HirNodeId(),
                statementMember.member,
                statementInvocation.calleeType,
                statementCheckedReceiver.sourceType,
                statementCheckedReceiver.parameterType,
                stmtExpectedMode,
                zc::mv(statementSteps),
                statementInvocation.resultType,
                zc::mv(statementCallArguments),
                ZC_ASSERT_NONNULL(statementCallSpan).clone(),
                stmtLiveErase
                    ? zc::Maybe<identity::DefId>(
                          statementSelected.get<checker::checked::DynMethodCallable>().interface)
                    : zc::Maybe<identity::DefId>()};
          }
        } else {
          if (!tree.contains(calleeNode) ||
              tree.node(calleeNode).kind != ast::SyntaxKind::IdentExpr ||
              !tree.contains(typeArguments) || !tree.contains(arguments) ||
              !typeArguments.empty() || calleeTypeIndex == zc::none ||
              checkedCallIndex == zc::none || callKey == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          auto dispatchIndex =
              dispatchFactIndex(checkedModule.dispatchFacts().facts(), ZC_ASSERT_NONNULL(callKey));
          auto callee = resolvedDefinition(bound.bindings(), calleeNode);
          if (dispatchIndex == zc::none || callee == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t calleeTypeSlot = 0;
          size_t checkedCallSlot = 0;
          size_t dispatchSlot = 0;
          ZC_IF_SOME(index, calleeTypeIndex) { calleeTypeSlot = index; }
          ZC_IF_SOME(index, checkedCallIndex) { checkedCallSlot = index; }
          ZC_IF_SOME(index, dispatchIndex) { dispatchSlot = index; }
          const auto& checkedCall = facts.calls().entries()[checkedCallSlot].value;
          const auto& invocation = checkedCall.invocation;
          const auto& selected = invocation.selected.variant();
          const auto& dispatch = checkedModule.dispatchFacts().facts()[dispatchSlot];
          const auto& target = dispatch.fact.target.variant();
          const auto& transform = dispatch.fact.resultTransform.variant();
          bool dispatchOwnerMatches = false;
          ZC_IF_SOME(owner, dispatch.owner) {
            dispatchOwnerMatches = owner == definition.definition;
          }
          if (!selected.is<checker::checked::DirectCallable>() ||
              selected.get<checker::checked::DirectCallable>().callee !=
                  ZC_ASSERT_NONNULL(callee) ||
              invocation.calleeType != facts.nodeTypes().entries()[calleeTypeSlot].value ||
              (invocation.successType != nodeType.value &&
               (!shape.returnsErrorPropagateCall || invocation.successType != callable.success) &&
               (!shape.returnsErrorUnwrapCall || invocation.successType != callable.success)) ||
              (invocation.resultType != nodeType.value && !shape.returnsErrorUnwrapCall) ||
              invocation.receiver != zc::none || invocation.receiverMode != zc::none ||
              invocation.receiverAdjustment != zc::none ||
              invocation.arguments.size() != arguments.size ||
              invocation.substitutions != zc::none || invocation.witnesses != zc::none ||
              (invocation.raises != zc::none && !shape.returnsErrorPropagateCall &&
               !shape.returnsErrorUnwrapCall) ||
              callSpan == zc::none ||
              !sameSpan(checkedCall.sourceSpan, ZC_ASSERT_NONNULL(callSpan)) ||
              !dispatchOwnerMatches || !target.is<checker::dispatch::DirectTarget>() ||
              target.get<checker::dispatch::DirectTarget>().callee != ZC_ASSERT_NONNULL(callee) ||
              !transform.is<checker::dispatch::IdentityResultTransform>() ||
              dispatch.fact.receiver != zc::none ||
              dispatch.fact.arguments.size() != arguments.size ||
              (dispatch.fact.successType != nodeType.value && !shape.returnsErrorPropagateCall &&
               (!shape.returnsErrorUnwrapCall || dispatch.fact.successType != callable.success)) ||
              (dispatch.fact.resultType != nodeType.value && !shape.returnsErrorUnwrapCall) ||
              dispatch.fact.substitutions != zc::none || dispatch.fact.witnesses != zc::none ||
              (dispatch.fact.raises != zc::none && !shape.returnsErrorPropagateCall &&
               !shape.returnsErrorUnwrapCall) ||
              !sameSpan(dispatch.fact.sourceSpan, ZC_ASSERT_NONNULL(callSpan)) ||
              !typeExists(invocation.calleeType, checkedModule.semanticTypes())) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          zc::Vector<HirDirectCallArgument> callArguments;
          const auto argumentNodes = tree.list(arguments);
          for (size_t index = 0; index < argumentNodes.size(); ++index) {
            const auto argument = argumentNodes[index];
            auto argumentTypeIndex = factIndex(facts.nodeTypes(), argument);
            auto argumentKey = checkedNodeKey(tree, bound.parsedModule(), argument);
            auto argumentSpan = bound.parsedModule().spanFor(tree.node(argument).range);
            const bool isLiteralArgument =
                tree.contains(argument) && isScalarLiteral(tree.node(argument).kind);
            const bool isParameterArgument =
                tree.contains(argument) && tree.node(argument).kind == ast::SyntaxKind::IdentExpr;
            // A qualified enum variant access (`Color::Red`) is a MemberExpression
            // whose checker-produced Literal fact carries the variant discriminant.
            // It lowers through the same literal-carrier path as a scalar literal.
            const bool isEnumVariantArgument =
                tree.contains(argument) &&
                tree.node(argument).kind == ast::SyntaxKind::MemberExpression &&
                factIndex(facts.literals(), argument) != zc::none;
            if ((!isLiteralArgument && !isParameterArgument && !isEnumVariantArgument) ||
                argumentTypeIndex == zc::none || argumentKey == zc::none ||
                argumentSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t argumentTypeSlot = 0;
            ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
            const auto& checkedArgument = invocation.arguments[index];
            const auto& dispatchArgument = dispatch.fact.arguments[index];
            const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
            // A DynErase argument adjustment (concrete-to-dyn) is admitted by
            // the coercion validation above. The source type matches the
            // argument expression; the parameter type is the erased existential
            // type and the adjustment records the erasure, so both are exempt
            // from the identity checks that apply to non-erased arguments.
            const bool argumentErased =
                checkedArgument.adjustment != zc::none &&
                isSingleDynEraseAdjustment(ZC_ASSERT_NONNULL(checkedArgument.adjustment));
            const bool dispatchErased =
                dispatchArgument.adjustment != zc::none &&
                isSingleDynEraseAdjustment(ZC_ASSERT_NONNULL(dispatchArgument.adjustment));
            if (checkedArgument.sourceNode != argument ||
                checkedArgument.sourceType != argumentType ||
                (!argumentErased && checkedArgument.parameterType != argumentType) ||
                (!argumentErased && checkedArgument.adjustment != zc::none) ||
                !sameNodeKey(dispatchArgument.sourceNode, ZC_ASSERT_NONNULL(argumentKey)) ||
                dispatchArgument.sourceType != argumentType ||
                (!dispatchErased && dispatchArgument.parameterType != argumentType) ||
                (!dispatchErased && dispatchArgument.adjustment != zc::none)) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            if (isLiteralArgument || isEnumVariantArgument) {
              auto literalIndex = factIndex(facts.literals(), argument);
              if (literalIndex == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              size_t literalSlot = 0;
              ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
              const auto& literal = facts.literals().entries()[literalSlot].value;
              if (literal.node != argument || literal.type != argumentType ||
                  !sameSpan(literal.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              zc::Maybe<identity::CallableParameterKey> noParameter;
              callArguments.add(HirDirectCallArgument{
                  argumentType, literal.literal.clone(), zc::mv(noParameter), zc::none, zc::none,
                  zc::none, ZC_ASSERT_NONNULL(argumentSpan).clone()});
              continue;
            }
            // A by-value aggregate call passes the single aggregate-initialized
            // owner local as the sole argument. The local is the body's one and
            // only binding; its field set was i32-gated with the aggregate
            // initializer above. Every other owner-local argument keeps the
            // parameter-key rejection below.
            if (shape.returnsDirectAggregateCall && local != zc::none) {
              auto localBinding = resolvedOwnerLocal(bound.bindings(), argument);
              if (localBinding != zc::none &&
                  ownerLocalMatches(bound.definitions(), ZC_ASSERT_NONNULL(localBinding),
                                    shape.localPattern, tree) &&
                  argumentType == ZC_ASSERT_NONNULL(local).type) {
                zc::Maybe<checker::checked::CanonicalConstValue> noValue;
                zc::Maybe<identity::CallableParameterKey> noParameter;
                zc::Maybe<HirLocalId> argumentLocal = hirLocalId(1);
                callArguments.add(HirDirectCallArgument{
                    argumentType, zc::mv(noValue), zc::mv(noParameter), zc::mv(argumentLocal),
                    zc::none, zc::none, ZC_ASSERT_NONNULL(argumentSpan).clone()});
                continue;
              }
            }
            // An existential call passes the single call-initialized existential
            // owner local as the sole argument, copied by value through local 1.
            // The call already produced the fat-pointer carrier, so no erasure
            // is needed.
            if (shape.returnsDirectExistentialCall && local != zc::none) {
              auto localBinding = resolvedOwnerLocal(bound.bindings(), argument);
              if (localBinding != zc::none &&
                  ownerLocalMatches(bound.definitions(), ZC_ASSERT_NONNULL(localBinding),
                                    shape.localPattern, tree) &&
                  argumentType == ZC_ASSERT_NONNULL(local).type) {
                zc::Maybe<checker::checked::CanonicalConstValue> noValue;
                zc::Maybe<identity::CallableParameterKey> noParameter;
                zc::Maybe<HirLocalId> argumentLocal = hirLocalId(1);
                callArguments.add(HirDirectCallArgument{
                    argumentType, zc::mv(noValue), zc::mv(noParameter), zc::mv(argumentLocal),
                    zc::none, zc::none, ZC_ASSERT_NONNULL(argumentSpan).clone()});
                continue;
              }
            }
            // A scalar-local call passes the single literal-initialized i32 or
            // bool owner local as the sole argument, copied by value through
            // local 1.
            if (shape.returnsDirectScalarLocalCall && local != zc::none) {
              auto localBinding = resolvedOwnerLocal(bound.bindings(), argument);
              if (localBinding != zc::none &&
                  ownerLocalMatches(bound.definitions(), ZC_ASSERT_NONNULL(localBinding),
                                    shape.localPattern, tree) &&
                  (isI32SemanticType(checkedModule.semanticTypes(), argumentType) ||
                   isBoolSemanticType(checkedModule.semanticTypes(), argumentType)) &&
                  argumentType == ZC_ASSERT_NONNULL(local).type) {
                zc::Maybe<checker::checked::CanonicalConstValue> noValue;
                zc::Maybe<identity::CallableParameterKey> noParameter;
                zc::Maybe<HirLocalId> argumentLocal = hirLocalId(1);
                callArguments.add(HirDirectCallArgument{
                    argumentType, zc::mv(noValue), zc::mv(noParameter), zc::mv(argumentLocal),
                    zc::none, zc::none, ZC_ASSERT_NONNULL(argumentSpan).clone()});
                continue;
              }
            }
            // Any other owner-local carrier (an identifier- or call-initialized
            // local, a mixed carrier pair, a non-i32 scalar the checker left on
            // another rail) is well-formed source the direct-call lowering does
            // not emit; drain the owning definition with the capability code
            // instead of collapsing to an invalid-fact invariant.
            if (resolvedOwnerLocal(bound.bindings(), argument) != zc::none) {
              return rejectHirCapability<HirModuleCandidate>(
                  definition.definition, registries, ir::IrFailureKind::UnsupportedSourceConstruct,
                  ZC_ASSERT_NONNULL(argumentSpan).clone());
            }
            auto parameter = resolvedCallableParameter(bound.bindings(), argument);
            zc::Maybe<identity::CallableParameterKey> parameterKey;
            ZC_IF_SOME(handle, parameter) {
              auto authority = registries.callableParameter(handle);
              ZC_IF_SOME(entry, authority) {
                for (const auto& candidate : parameters) {
                  if (candidate.key == entry.key() && candidate.type == argumentType) {
                    parameterKey = entry.key().clone();
                  }
                }
              }
            }
            if (parameterKey == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            zc::Maybe<checker::checked::CanonicalConstValue> noValue;
            callArguments.add(
                HirDirectCallArgument{argumentType, zc::mv(noValue), zc::mv(parameterKey), zc::none,
                                      zc::none, zc::none, ZC_ASSERT_NONNULL(argumentSpan).clone()});
          }
          // For an error-propagate return (`return f()?!;`), the `?!` operator
          // unwraps the call's error union to the success type.  The HIR call
          // expression carries the unwrapped (success) type so that it matches
          // the enclosing function's result type.
          const identity::SemanticTypeId callResultType =
              shape.returnsErrorPropagateCall ? callable.success : invocation.resultType;
          call = HirDirectCallExpression{
              HirNodeId(),    ZC_ASSERT_NONNULL(callee), invocation.calleeType,
              callResultType, zc::mv(callArguments),     ZC_ASSERT_NONNULL(callSpan).clone()};
        }
      } else if (!shape.isForLoopAccumulator && !shape.isNestedForLoopAccumulator &&
                 literal == zc::none && aggregate == zc::none && parameterReference == zc::none &&
                 parameterIndex == zc::none && localReference == zc::none &&
                 initializerCall == zc::none && shape.localInitializer != zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      // Loop-body composite shape: the mut-local writes are the loop body. Build
      // the loop condition parameter reference and the loop span so the loop
      // statement wraps the writes in a reducible CFG. The declaration, writes,
      // and return are already assembled above exactly as for the flat mut-local
      // shape.
      zc::Maybe<PendingLoopBodyReturn> loopBodyReturn;
      zc::Maybe<PendingComparisonLoopBodyReturn> comparisonLoopBodyReturn;
      // Loop-body with a comparison condition: `while (<ident> <cmp> <lit>)`
      // or `do { <writes> } while (<ident> <cmp> <lit>);`. The comparison is
      // resolved like the for-loop condition: the left operand names the
      // accumulator local and the right operand is a scalar literal. The
      // while CFG is condition-before-body; the do-while CFG is
      // body-before-condition.
      if (shape.isLoopBody && tree.contains(shape.loopCondition) &&
          tree.node(shape.loopCondition).kind == ast::SyntaxKind::BinaryExpr) {
        const auto& condNode = tree.node(shape.loopCondition);
        const ast::NodeId condLhs(condNode.payload.words[ast::kBinaryExprLhsWord]);
        const ast::NodeId condRhs(condNode.payload.words[ast::kBinaryExprRhsWord]);
        if (!tree.contains(condLhs) || tree.node(condLhs).kind != ast::SyntaxKind::IdentExpr ||
            !tree.contains(condRhs) || !isScalarLiteral(tree.node(condRhs).kind)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto condResultTypeIndex = factIndex(facts.nodeTypes(), shape.loopCondition);
        auto condCallIndex = factIndex(facts.calls(), shape.loopCondition);
        auto condLhsTypeIndex = factIndex(facts.nodeTypes(), condLhs);
        auto condRhsTypeIndex = factIndex(facts.nodeTypes(), condRhs);
        auto condLhsBinding = resolvedOwnerLocal(bound.bindings(), condLhs);
        auto condRhsLiteralIndex = factIndex(facts.literals(), condRhs);
        auto condSpan = bound.parsedModule().spanFor(condNode.range);
        auto condLhsSpan = bound.parsedModule().spanFor(tree.node(condLhs).range);
        auto condRhsSpan = bound.parsedModule().spanFor(tree.node(condRhs).range);
        auto loopSpan = bound.parsedModule().spanFor(tree.node(shape.loopStatement).range);
        auto accumulatorBinding =
            ownerLocalBindingForPattern(bound.definitions(), shape.localPattern, tree);
        zc::Maybe<identity::SourceSpan> breakSpan;
        zc::Maybe<identity::SourceSpan> continueSpan;
        if (shape.loopBodyBreak) {
          breakSpan = bound.parsedModule().spanFor(tree.node(shape.loopBodyBreak).range);
          if (breakSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
        }
        if (shape.loopBodyContinue) {
          continueSpan = bound.parsedModule().spanFor(tree.node(shape.loopBodyContinue).range);
          if (continueSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
        }
        if (condResultTypeIndex == zc::none || condCallIndex == zc::none ||
            condLhsTypeIndex == zc::none || condRhsTypeIndex == zc::none ||
            condLhsBinding == zc::none || condRhsLiteralIndex == zc::none || condSpan == zc::none ||
            condLhsSpan == zc::none || condRhsSpan == zc::none || loopSpan == zc::none ||
            accumulatorBinding == zc::none ||
            ZC_ASSERT_NONNULL(condLhsBinding) != ZC_ASSERT_NONNULL(accumulatorBinding)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t condResultTypeSlot = 0;
        size_t condCallSlot = 0;
        size_t condLhsTypeSlot = 0;
        size_t condRhsTypeSlot = 0;
        size_t condRhsLiteralSlot = 0;
        ZC_IF_SOME(index, condResultTypeIndex) { condResultTypeSlot = index; }
        ZC_IF_SOME(index, condCallIndex) { condCallSlot = index; }
        ZC_IF_SOME(index, condLhsTypeIndex) { condLhsTypeSlot = index; }
        ZC_IF_SOME(index, condRhsTypeIndex) { condRhsTypeSlot = index; }
        ZC_IF_SOME(index, condRhsLiteralIndex) { condRhsLiteralSlot = index; }
        const auto condResultType = facts.nodeTypes().entries()[condResultTypeSlot].value;
        const auto condLhsType = facts.nodeTypes().entries()[condLhsTypeSlot].value;
        const auto condRhsType = facts.nodeTypes().entries()[condRhsTypeSlot].value;
        const auto& condRhsLiteralFact = facts.literals().entries()[condRhsLiteralSlot].value;
        const auto& condCallFact = facts.calls().entries()[condCallSlot].value;
        const auto& condSelected = condCallFact.invocation.selected.variant();
        if (!condSelected.is<checker::checked::PrimitiveCallable>()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto condOperation =
            condSelected.get<checker::checked::PrimitiveCallable>().operation;
        if (!isScalarComparisonOperation(condOperation) ||
            !isBoolSemanticType(checkedModule.semanticTypes(), condResultType) ||
            condLhsType != condRhsType || condRhsLiteralFact.type != condRhsType) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        comparisonLoopBodyReturn = PendingComparisonLoopBodyReturn{
            HirPrimitiveBinaryExpression{HirNodeId(), HirNodeId(), HirNodeId(), condLhsType,
                                         condResultType, HirValueCategory::Value, condOperation,
                                         ZC_ASSERT_NONNULL(condSpan).clone()},
            HirLocalReferenceExpression{HirNodeId(), hirLocalId(1), condLhsType,
                                        HirValueCategory::Place,
                                        ZC_ASSERT_NONNULL(condLhsSpan).clone()},
            HirScalarLiteralExpression{HirNodeId(), condRhsType, condRhsLiteralFact.literal.clone(),
                                       HirValueCategory::Value,
                                       ZC_ASSERT_NONNULL(condRhsSpan).clone()},
            ZC_ASSERT_NONNULL(loopSpan).clone(),
            zc::mv(breakSpan),
            zc::mv(continueSpan),
            shape.isDoWhileLoopBody,
            shape.loopStatement,
            shape.returnStatement};
      } else if (shape.isLoopBody && !shape.isDoWhileLoopBody) {
        auto conditionParameter = resolvedCallableParameter(bound.bindings(), shape.loopCondition);
        auto conditionTypeIndex = factIndex(facts.nodeTypes(), shape.loopCondition);
        auto conditionSpan = bound.parsedModule().spanFor(tree.node(shape.loopCondition).range);
        auto loopSpan = bound.parsedModule().spanFor(tree.node(shape.loopStatement).range);
        if (conditionParameter == zc::none || conditionTypeIndex == zc::none ||
            conditionSpan == zc::none || loopSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        identity::CallableParameterId conditionHandle;
        ZC_IF_SOME(value, conditionParameter) { conditionHandle = value; }
        auto conditionAuthority = registries.callableParameter(conditionHandle);
        if (conditionAuthority == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t conditionTypeSlot = 0;
        ZC_IF_SOME(index, conditionTypeIndex) { conditionTypeSlot = index; }
        const auto conditionType = facts.nodeTypes().entries()[conditionTypeSlot].value;
        zc::Maybe<identity::SourceSpan> breakSpan;
        zc::Maybe<identity::SourceSpan> continueSpan;
        if (shape.loopBodyBreak) {
          breakSpan = bound.parsedModule().spanFor(tree.node(shape.loopBodyBreak).range);
          if (breakSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
        }
        if (shape.loopBodyContinue) {
          continueSpan = bound.parsedModule().spanFor(tree.node(shape.loopBodyContinue).range);
          if (continueSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
        }
        ZC_IF_SOME(entry, conditionAuthority) {
          auto conditionRef = HirParameterReferenceExpression{
              HirNodeId(), entry.key().clone(), conditionType, HirValueCategory::Place,
              ZC_ASSERT_NONNULL(conditionSpan).clone()};
          loopBodyReturn =
              PendingLoopBodyReturn{zc::mv(conditionRef), ZC_ASSERT_NONNULL(loopSpan).clone(),
                                    zc::mv(breakSpan), zc::mv(continueSpan), shape.loopStatement};
        }
        if (loopBodyReturn == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
      }
      // For-loop shape: `for (let id = <lit>; <ident> <cmp> <lit>;
      // <ident> = <binary>) {}` followed by a scalar return. The for-loop
      // desugars to a leading local binding plus a loop whose condition is the
      // comparison and whose body is the update write. Resolve the init local,
      // the comparison condition, and the update write into a self-contained
      // pending carrier; the lowering function allocates the node ids.
      zc::Maybe<PendingForLoopReturn> forLoopReturn;
      if (shape.isForLoop) {
        // Init: let declaration with one identifier-pattern declarator and a
        // scalar literal initializer. The pattern resolves to the loop local.
        const auto& letNode = tree.node(shape.forLoopInit);
        const ast::NodeId declarations(letNode.payload.words[ast::kLetStmtDeclarationsWord]);
        const ast::NodeList declarators{
            tree.node(declarations).payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
            tree.node(declarations).payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
        if (!tree.contains(declarations) ||
            tree.node(declarations).kind != ast::SyntaxKind::VariableDeclaratorList ||
            !tree.contains(declarators) || declarators.size != 1) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto declarator = tree.list(declarators)[0];
        const ast::NodeId pattern(
            tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]);
        const ast::NodeId initializer(
            tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
        if (!tree.contains(pattern) ||
            tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern ||
            !tree.contains(initializer) || !isScalarLiteral(tree.node(initializer).kind)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto initBinding = ownerLocalBindingForPattern(bound.definitions(), pattern, tree);
        auto initTypeIndex = factIndex(facts.nodeTypes(), initializer);
        auto initLiteralIndex = factIndex(facts.literals(), initializer);
        auto patternSpan = bound.parsedModule().spanFor(tree.node(pattern).range);
        auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
        // Cond: binary comparison with an identifier left operand (the loop
        // local) and a scalar literal right operand.
        const auto& condNode = tree.node(shape.forLoopCond);
        const ast::NodeId condLhs(condNode.payload.words[ast::kBinaryExprLhsWord]);
        const ast::NodeId condRhs(condNode.payload.words[ast::kBinaryExprRhsWord]);
        if (!tree.contains(condLhs) || tree.node(condLhs).kind != ast::SyntaxKind::IdentExpr ||
            !tree.contains(condRhs) || !isScalarLiteral(tree.node(condRhs).kind)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto condResultTypeIndex = factIndex(facts.nodeTypes(), shape.forLoopCond);
        auto condCallIndex = factIndex(facts.calls(), shape.forLoopCond);
        auto condLhsTypeIndex = factIndex(facts.nodeTypes(), condLhs);
        auto condRhsTypeIndex = factIndex(facts.nodeTypes(), condRhs);
        auto condLhsBinding = resolvedOwnerLocal(bound.bindings(), condLhs);
        auto condRhsLiteralIndex = factIndex(facts.literals(), condRhs);
        auto condSpan = bound.parsedModule().spanFor(condNode.range);
        auto condLhsSpan = bound.parsedModule().spanFor(tree.node(condLhs).range);
        auto condRhsSpan = bound.parsedModule().spanFor(tree.node(condRhs).range);
        // Update: assignment of an arithmetic binary to the loop local. The
        // binary's left operand is the loop local and its right operand is a
        // scalar literal.
        const auto& updateNode = tree.node(shape.forLoopUpdate);
        const ast::NodeId updateTarget(updateNode.payload.words[ast::kAssignmentExprLhsWord]);
        const ast::NodeId updateValueNode(updateNode.payload.words[ast::kAssignmentExprRhsWord]);
        if (!tree.contains(updateTarget) ||
            tree.node(updateTarget).kind != ast::SyntaxKind::IdentExpr ||
            !tree.contains(updateValueNode) ||
            tree.node(updateValueNode).kind != ast::SyntaxKind::BinaryExpr) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& updateValueBin = tree.node(updateValueNode);
        const ast::NodeId updateLhs(updateValueBin.payload.words[ast::kBinaryExprLhsWord]);
        const ast::NodeId updateRhs(updateValueBin.payload.words[ast::kBinaryExprRhsWord]);
        if (!tree.contains(updateLhs) || tree.node(updateLhs).kind != ast::SyntaxKind::IdentExpr ||
            !tree.contains(updateRhs) || !isScalarLiteral(tree.node(updateRhs).kind)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto updateTargetBinding = resolvedOwnerLocal(bound.bindings(), updateTarget);
        auto updateValueTypeIndex = factIndex(facts.nodeTypes(), updateValueNode);
        auto updateCallIndex = factIndex(facts.calls(), updateValueNode);
        auto updateLhsTypeIndex = factIndex(facts.nodeTypes(), updateLhs);
        auto updateRhsTypeIndex = factIndex(facts.nodeTypes(), updateRhs);
        auto updateLhsBinding = resolvedOwnerLocal(bound.bindings(), updateLhs);
        auto updateRhsLiteralIndex = factIndex(facts.literals(), updateRhs);
        auto updateSpan = bound.parsedModule().spanFor(updateNode.range);
        auto updateValueSpan = bound.parsedModule().spanFor(updateValueBin.range);
        auto updateLhsSpan = bound.parsedModule().spanFor(tree.node(updateLhs).range);
        auto updateRhsSpan = bound.parsedModule().spanFor(tree.node(updateRhs).range);
        auto loopSpan = bound.parsedModule().spanFor(tree.node(shape.forLoopStatement).range);
        zc::Maybe<identity::SourceSpan> breakSpan;
        zc::Maybe<identity::SourceSpan> continueSpan;
        if (shape.forLoopBodyBreak) {
          breakSpan = bound.parsedModule().spanFor(tree.node(shape.forLoopBodyBreak).range);
          if (breakSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
        }
        if (shape.forLoopBodyContinue) {
          continueSpan = bound.parsedModule().spanFor(tree.node(shape.forLoopBodyContinue).range);
          if (continueSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
        }
        if (initBinding == zc::none || initTypeIndex == zc::none || initLiteralIndex == zc::none ||
            patternSpan == zc::none || initializerSpan == zc::none ||
            condResultTypeIndex == zc::none || condCallIndex == zc::none ||
            condLhsTypeIndex == zc::none || condRhsTypeIndex == zc::none ||
            condLhsBinding == zc::none || condRhsLiteralIndex == zc::none || condSpan == zc::none ||
            condLhsSpan == zc::none || condRhsSpan == zc::none || updateTargetBinding == zc::none ||
            updateValueTypeIndex == zc::none || updateCallIndex == zc::none ||
            updateLhsTypeIndex == zc::none || updateRhsTypeIndex == zc::none ||
            updateLhsBinding == zc::none || updateRhsLiteralIndex == zc::none ||
            updateSpan == zc::none || updateValueSpan == zc::none || updateLhsSpan == zc::none ||
            updateRhsSpan == zc::none || loopSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        // Every identifier operand must resolve to the same owner local (the
        // init loop local); a parameter or a different local has no lowering.
        if (ZC_ASSERT_NONNULL(initBinding) != ZC_ASSERT_NONNULL(condLhsBinding) ||
            ZC_ASSERT_NONNULL(initBinding) != ZC_ASSERT_NONNULL(updateTargetBinding) ||
            ZC_ASSERT_NONNULL(initBinding) != ZC_ASSERT_NONNULL(updateLhsBinding)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        size_t initTypeSlot = 0;
        size_t initLiteralSlot = 0;
        size_t condResultTypeSlot = 0;
        size_t condCallSlot = 0;
        size_t condLhsTypeSlot = 0;
        size_t condRhsTypeSlot = 0;
        size_t condRhsLiteralSlot = 0;
        size_t updateValueTypeSlot = 0;
        size_t updateCallSlot = 0;
        size_t updateLhsTypeSlot = 0;
        size_t updateRhsTypeSlot = 0;
        size_t updateRhsLiteralSlot = 0;
        ZC_IF_SOME(index, initTypeIndex) { initTypeSlot = index; }
        ZC_IF_SOME(index, initLiteralIndex) { initLiteralSlot = index; }
        ZC_IF_SOME(index, condResultTypeIndex) { condResultTypeSlot = index; }
        ZC_IF_SOME(index, condCallIndex) { condCallSlot = index; }
        ZC_IF_SOME(index, condLhsTypeIndex) { condLhsTypeSlot = index; }
        ZC_IF_SOME(index, condRhsTypeIndex) { condRhsTypeSlot = index; }
        ZC_IF_SOME(index, condRhsLiteralIndex) { condRhsLiteralSlot = index; }
        ZC_IF_SOME(index, updateValueTypeIndex) { updateValueTypeSlot = index; }
        ZC_IF_SOME(index, updateCallIndex) { updateCallSlot = index; }
        ZC_IF_SOME(index, updateLhsTypeIndex) { updateLhsTypeSlot = index; }
        ZC_IF_SOME(index, updateRhsTypeIndex) { updateRhsTypeSlot = index; }
        ZC_IF_SOME(index, updateRhsLiteralIndex) { updateRhsLiteralSlot = index; }
        const auto initType = facts.nodeTypes().entries()[initTypeSlot].value;
        const auto& initLiteralFact = facts.literals().entries()[initLiteralSlot].value;
        const auto condResultType = facts.nodeTypes().entries()[condResultTypeSlot].value;
        const auto condLhsType = facts.nodeTypes().entries()[condLhsTypeSlot].value;
        const auto condRhsType = facts.nodeTypes().entries()[condRhsTypeSlot].value;
        const auto& condRhsLiteralFact = facts.literals().entries()[condRhsLiteralSlot].value;
        const auto updateValueType = facts.nodeTypes().entries()[updateValueTypeSlot].value;
        const auto updateLhsType = facts.nodeTypes().entries()[updateLhsTypeSlot].value;
        const auto updateRhsType = facts.nodeTypes().entries()[updateRhsTypeSlot].value;
        const auto& updateRhsLiteralFact = facts.literals().entries()[updateRhsLiteralSlot].value;
        // The comparison call fact must select a primitive relational operator
        // producing bool; the arithmetic call fact must select a primitive
        // arithmetic operator producing the operand type.
        const auto& condCallFact = facts.calls().entries()[condCallSlot].value;
        const auto& condSelected = condCallFact.invocation.selected.variant();
        const auto& updateCallFact = facts.calls().entries()[updateCallSlot].value;
        const auto& updateSelected = updateCallFact.invocation.selected.variant();
        if (!condSelected.is<checker::checked::PrimitiveCallable>() ||
            !updateSelected.is<checker::checked::PrimitiveCallable>()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto condOperation =
            condSelected.get<checker::checked::PrimitiveCallable>().operation;
        const auto updateOperation =
            updateSelected.get<checker::checked::PrimitiveCallable>().operation;
        if (!isScalarComparisonOperation(condOperation) ||
            !isScalarArithmeticOperation(updateOperation) ||
            !isBoolSemanticType(checkedModule.semanticTypes(), condResultType) ||
            condLhsType != condRhsType || condLhsType != initType ||
            updateLhsType != updateRhsType || updateLhsType != initType ||
            updateValueType != updateLhsType || initLiteralFact.type != initType ||
            condRhsLiteralFact.type != condRhsType || updateRhsLiteralFact.type != updateRhsType) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        forLoopReturn = PendingForLoopReturn{
            HirPrimitiveBinaryExpression{HirNodeId(), HirNodeId(), HirNodeId(), condLhsType,
                                         condResultType, HirValueCategory::Value, condOperation,
                                         ZC_ASSERT_NONNULL(condSpan).clone()},
            HirLocalReferenceExpression{HirNodeId(), hirLocalId(1), condLhsType,
                                        HirValueCategory::Place,
                                        ZC_ASSERT_NONNULL(condLhsSpan).clone()},
            HirScalarLiteralExpression{HirNodeId(), condRhsType, condRhsLiteralFact.literal.clone(),
                                       HirValueCategory::Value,
                                       ZC_ASSERT_NONNULL(condRhsSpan).clone()},
            ZC_ASSERT_NONNULL(loopSpan).clone(),
            HirLocalBinding{HirNodeId(), hirLocalId(1), initType, zc::none,
                            ZC_ASSERT_NONNULL(patternSpan).clone(),
                            ZC_ASSERT_NONNULL(initializerSpan).clone()},
            HirScalarLiteralExpression{HirNodeId(), initType, initLiteralFact.literal.clone(),
                                       HirValueCategory::Value,
                                       ZC_ASSERT_NONNULL(initializerSpan).clone()},
            HirLocalWriteStatement{HirNodeId(), hirLocalId(1), zc::none, updateValueType,
                                   HirNodeId(), HirLocalWriteKind::Overwrite,
                                   ZC_ASSERT_NONNULL(updateSpan).clone(),
                                   ZC_ASSERT_NONNULL(updateValueSpan).clone()},
            HirPrimitiveBinaryExpression{HirNodeId(), HirNodeId(), HirNodeId(), updateLhsType,
                                         updateValueType, HirValueCategory::Value, updateOperation,
                                         ZC_ASSERT_NONNULL(updateValueSpan).clone()},
            HirLocalReferenceExpression{HirNodeId(), hirLocalId(1), updateLhsType,
                                        HirValueCategory::Place,
                                        ZC_ASSERT_NONNULL(updateLhsSpan).clone()},
            HirScalarLiteralExpression{
                HirNodeId(), updateRhsType, updateRhsLiteralFact.literal.clone(),
                HirValueCategory::Value, ZC_ASSERT_NONNULL(updateRhsSpan).clone()},
            zc::mv(breakSpan),
            zc::mv(continueSpan),
            shape.forLoopStatement,
            shape.returnStatement};
      }
      // For-loop accumulator shape: N leading scalar `mut` accumulator locals,
      // a `for (let id = <lit>; <ident> <cmp> <lit>; <ident> = <binary>) {
      // <accumulator> = <accumulator> <bin> <ident|lit>; ... }` loop, and a
      // trailing `return <accumulator-local>;`. Accumulator k is
      // hirLocalId(1+k); the loop init local is hirLocalId(1+N). Resolve the
      // for-loop init/cond/update, the N accumulator body writes, and the
      // return reference into a self-contained pending carrier; the lowering
      // function allocates the node ids.
      zc::Maybe<PendingForLoopAccumulatorReturn> forLoopAccumulatorReturn;
      if (shape.isForLoopAccumulator) {
        const size_t accumulatorCount = shape.forLoopAccumulatorPatterns.size();
        // Init: let declaration with one identifier-pattern declarator and a
        // scalar literal initializer. The pattern resolves to the loop local
        // (hirLocalId(1+N)).
        const auto& letNode = tree.node(shape.forLoopInit);
        const ast::NodeId declarations(letNode.payload.words[ast::kLetStmtDeclarationsWord]);
        const ast::NodeList declarators{
            tree.node(declarations).payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
            tree.node(declarations).payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
        if (!tree.contains(declarations) ||
            tree.node(declarations).kind != ast::SyntaxKind::VariableDeclaratorList ||
            !tree.contains(declarators) || declarators.size != 1) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto declarator = tree.list(declarators)[0];
        const ast::NodeId pattern(
            tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]);
        const ast::NodeId initializer(
            tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
        if (!tree.contains(pattern) ||
            tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern ||
            !tree.contains(initializer) || !isScalarLiteral(tree.node(initializer).kind)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto initBinding = ownerLocalBindingForPattern(bound.definitions(), pattern, tree);
        auto initTypeIndex = factIndex(facts.nodeTypes(), initializer);
        auto initLiteralIndex = factIndex(facts.literals(), initializer);
        auto patternSpan = bound.parsedModule().spanFor(tree.node(pattern).range);
        auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
        // Cond: binary comparison with an identifier left operand (the loop
        // local) and a scalar literal right operand.
        const auto& condNode = tree.node(shape.forLoopCond);
        const ast::NodeId condLhs(condNode.payload.words[ast::kBinaryExprLhsWord]);
        const ast::NodeId condRhs(condNode.payload.words[ast::kBinaryExprRhsWord]);
        if (!tree.contains(condLhs) || tree.node(condLhs).kind != ast::SyntaxKind::IdentExpr ||
            !tree.contains(condRhs) || !isScalarLiteral(tree.node(condRhs).kind)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto condResultTypeIndex = factIndex(facts.nodeTypes(), shape.forLoopCond);
        auto condCallIndex = factIndex(facts.calls(), shape.forLoopCond);
        auto condLhsTypeIndex = factIndex(facts.nodeTypes(), condLhs);
        auto condRhsTypeIndex = factIndex(facts.nodeTypes(), condRhs);
        auto condLhsBinding = resolvedOwnerLocal(bound.bindings(), condLhs);
        auto condRhsLiteralIndex = factIndex(facts.literals(), condRhs);
        auto condSpan = bound.parsedModule().spanFor(condNode.range);
        auto condLhsSpan = bound.parsedModule().spanFor(tree.node(condLhs).range);
        auto condRhsSpan = bound.parsedModule().spanFor(tree.node(condRhs).range);
        // Update: assignment of an arithmetic binary to the loop local. The
        // binary's left operand is the loop local and its right operand is a
        // scalar literal.
        const auto& updateNode = tree.node(shape.forLoopUpdate);
        const ast::NodeId updateTarget(updateNode.payload.words[ast::kAssignmentExprLhsWord]);
        const ast::NodeId updateValueNode(updateNode.payload.words[ast::kAssignmentExprRhsWord]);
        if (!tree.contains(updateTarget) ||
            tree.node(updateTarget).kind != ast::SyntaxKind::IdentExpr ||
            !tree.contains(updateValueNode) ||
            tree.node(updateValueNode).kind != ast::SyntaxKind::BinaryExpr) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& updateValueBin = tree.node(updateValueNode);
        const ast::NodeId updateLhs(updateValueBin.payload.words[ast::kBinaryExprLhsWord]);
        const ast::NodeId updateRhs(updateValueBin.payload.words[ast::kBinaryExprRhsWord]);
        if (!tree.contains(updateLhs) || tree.node(updateLhs).kind != ast::SyntaxKind::IdentExpr ||
            !tree.contains(updateRhs) || !isScalarLiteral(tree.node(updateRhs).kind)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto updateTargetBinding = resolvedOwnerLocal(bound.bindings(), updateTarget);
        auto updateValueTypeIndex = factIndex(facts.nodeTypes(), updateValueNode);
        auto updateCallIndex = factIndex(facts.calls(), updateValueNode);
        auto updateLhsTypeIndex = factIndex(facts.nodeTypes(), updateLhs);
        auto updateRhsTypeIndex = factIndex(facts.nodeTypes(), updateRhs);
        auto updateLhsBinding = resolvedOwnerLocal(bound.bindings(), updateLhs);
        auto updateRhsLiteralIndex = factIndex(facts.literals(), updateRhs);
        auto updateSpan = bound.parsedModule().spanFor(updateNode.range);
        auto updateValueSpan = bound.parsedModule().spanFor(updateValueBin.range);
        auto updateLhsSpan = bound.parsedModule().spanFor(tree.node(updateLhs).range);
        auto updateRhsSpan = bound.parsedModule().spanFor(tree.node(updateRhs).range);
        auto loopSpan = bound.parsedModule().spanFor(tree.node(shape.forLoopStatement).range);
        zc::Maybe<identity::SourceSpan> breakSpan;
        zc::Maybe<identity::SourceSpan> continueSpan;
        if (shape.forLoopBodyBreak) {
          breakSpan = bound.parsedModule().spanFor(tree.node(shape.forLoopBodyBreak).range);
          if (breakSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
        }
        if (shape.forLoopBodyContinue) {
          continueSpan = bound.parsedModule().spanFor(tree.node(shape.forLoopBodyContinue).range);
          if (continueSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
        }
        // Resolve the if-guarded break condition (`if (i == <lit>) { break; }`):
        // a binary comparison whose left operand is the loop init local and
        // whose right operand is a scalar literal. The guard block evaluates
        // this comparison before the accumulator writes and exits the loop on
        // true.
        zc::Maybe<size_t> breakCondResultTypeIndex;
        zc::Maybe<size_t> breakCondCallIndex;
        zc::Maybe<size_t> breakCondLhsTypeIndex;
        zc::Maybe<size_t> breakCondRhsTypeIndex;
        zc::Maybe<binder::OwnerLocalBindingId> breakCondLhsBinding;
        zc::Maybe<size_t> breakCondRhsLiteralIndex;
        zc::Maybe<identity::SourceSpan> breakCondSpan;
        zc::Maybe<identity::SourceSpan> breakCondLhsSpan;
        zc::Maybe<identity::SourceSpan> breakCondRhsSpan;
        if (shape.forLoopBodyBreakCondition) {
          const auto& breakCondNode = tree.node(shape.forLoopBodyBreakCondition);
          if (breakCondNode.kind != ast::SyntaxKind::BinaryExpr) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          const ast::NodeId breakCondLhs(breakCondNode.payload.words[ast::kBinaryExprLhsWord]);
          const ast::NodeId breakCondRhs(breakCondNode.payload.words[ast::kBinaryExprRhsWord]);
          if (!tree.contains(breakCondLhs) ||
              tree.node(breakCondLhs).kind != ast::SyntaxKind::IdentExpr ||
              !tree.contains(breakCondRhs) || !isScalarLiteral(tree.node(breakCondRhs).kind)) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          breakCondResultTypeIndex = factIndex(facts.nodeTypes(), shape.forLoopBodyBreakCondition);
          breakCondCallIndex = factIndex(facts.calls(), shape.forLoopBodyBreakCondition);
          breakCondLhsTypeIndex = factIndex(facts.nodeTypes(), breakCondLhs);
          breakCondRhsTypeIndex = factIndex(facts.nodeTypes(), breakCondRhs);
          breakCondLhsBinding = resolvedOwnerLocal(bound.bindings(), breakCondLhs);
          breakCondRhsLiteralIndex = factIndex(facts.literals(), breakCondRhs);
          breakCondSpan = bound.parsedModule().spanFor(breakCondNode.range);
          breakCondLhsSpan = bound.parsedModule().spanFor(tree.node(breakCondLhs).range);
          breakCondRhsSpan = bound.parsedModule().spanFor(tree.node(breakCondRhs).range);
        }
        // Resolve each accumulator: its pattern/initializer, body write, and
        // facts. The body write right operand is either the loop init local
        // (IdentExpr) or a scalar literal.
        zc::Vector<PendingForLoopAccumulator> accumulators;
        bool accumulatorOk = true;
        for (size_t accIndex = 0; accIndex < accumulatorCount; ++accIndex) {
          const ast::NodeId accPattern = shape.forLoopAccumulatorPatterns[accIndex];
          const ast::NodeId accInitializer = shape.forLoopAccumulatorInitializers[accIndex];
          auto accBinding = ownerLocalBindingForPattern(bound.definitions(), accPattern, tree);
          auto accTypeIndex = factIndex(facts.nodeTypes(), accInitializer);
          auto accLiteralIndex = factIndex(facts.literals(), accInitializer);
          auto accPatternSpan = bound.parsedModule().spanFor(tree.node(accPattern).range);
          auto accInitializerSpan = bound.parsedModule().spanFor(tree.node(accInitializer).range);
          // Body write: `<accumulator> = <accumulator> <bin> <ident|lit>;`.
          const ast::NodeId bodyWriteStmt = shape.forLoopBodyWrites[accIndex];
          auto bodyWriteItem = statementItem(tree, bodyWriteStmt);
          if (bodyWriteItem == zc::none) {
            accumulatorOk = false;
            break;
          }
          ast::NodeId bodyWriteStmtNode;
          ZC_IF_SOME(item, bodyWriteItem) { bodyWriteStmtNode = item; }
          const ast::NodeId bodyWriteExpr(
              tree.node(bodyWriteStmtNode).payload.words[ast::kExpressionStatementExpressionWord]);
          const ast::NodeId bodyWriteTarget(
              tree.node(bodyWriteExpr).payload.words[ast::kAssignmentExprLhsWord]);
          const ast::NodeId bodyWriteValueNode(
              tree.node(bodyWriteExpr).payload.words[ast::kAssignmentExprRhsWord]);
          if (!tree.contains(bodyWriteExpr) ||
              tree.node(bodyWriteExpr).kind != ast::SyntaxKind::AssignmentExpr ||
              !tree.contains(bodyWriteTarget) ||
              tree.node(bodyWriteTarget).kind != ast::SyntaxKind::IdentExpr ||
              !tree.contains(bodyWriteValueNode) ||
              tree.node(bodyWriteValueNode).kind != ast::SyntaxKind::BinaryExpr) {
            accumulatorOk = false;
            break;
          }
          const auto& bodyWriteBin = tree.node(bodyWriteValueNode);
          const ast::NodeId bodyWriteLhs(bodyWriteBin.payload.words[ast::kBinaryExprLhsWord]);
          const ast::NodeId bodyWriteRhs(bodyWriteBin.payload.words[ast::kBinaryExprRhsWord]);
          const bool rhsIsLiteral = isScalarLiteral(tree.node(bodyWriteRhs).kind);
          if (!tree.contains(bodyWriteLhs) ||
              tree.node(bodyWriteLhs).kind != ast::SyntaxKind::IdentExpr ||
              !tree.contains(bodyWriteRhs) ||
              (tree.node(bodyWriteRhs).kind != ast::SyntaxKind::IdentExpr && !rhsIsLiteral)) {
            accumulatorOk = false;
            break;
          }
          auto bodyWriteTargetBinding = resolvedOwnerLocal(bound.bindings(), bodyWriteTarget);
          auto bodyWriteResultTypeIndex = factIndex(facts.nodeTypes(), bodyWriteValueNode);
          auto bodyWriteCallIndex = factIndex(facts.calls(), bodyWriteValueNode);
          auto bodyWriteLhsTypeIndex = factIndex(facts.nodeTypes(), bodyWriteLhs);
          auto bodyWriteRhsTypeIndex = factIndex(facts.nodeTypes(), bodyWriteRhs);
          auto bodyWriteLhsBinding = resolvedOwnerLocal(bound.bindings(), bodyWriteLhs);
          zc::Maybe<binder::OwnerLocalBindingId> bodyWriteRhsBinding;
          zc::Maybe<size_t> bodyWriteRhsLiteralIndex;
          if (!rhsIsLiteral) {
            bodyWriteRhsBinding = resolvedOwnerLocal(bound.bindings(), bodyWriteRhs);
          } else {
            bodyWriteRhsLiteralIndex = factIndex(facts.literals(), bodyWriteRhs);
          }
          auto bodyWriteSpan = bound.parsedModule().spanFor(tree.node(bodyWriteExpr).range);
          auto bodyWriteValueSpan = bound.parsedModule().spanFor(bodyWriteBin.range);
          auto bodyWriteLhsSpan = bound.parsedModule().spanFor(tree.node(bodyWriteLhs).range);
          auto bodyWriteRhsSpan = bound.parsedModule().spanFor(tree.node(bodyWriteRhs).range);
          if (accBinding == zc::none || accTypeIndex == zc::none || accLiteralIndex == zc::none ||
              accPatternSpan == zc::none || accInitializerSpan == zc::none ||
              bodyWriteTargetBinding == zc::none || bodyWriteResultTypeIndex == zc::none ||
              bodyWriteCallIndex == zc::none || bodyWriteLhsTypeIndex == zc::none ||
              bodyWriteRhsTypeIndex == zc::none || bodyWriteLhsBinding == zc::none ||
              bodyWriteSpan == zc::none || bodyWriteValueSpan == zc::none ||
              bodyWriteLhsSpan == zc::none || bodyWriteRhsSpan == zc::none ||
              (!rhsIsLiteral && bodyWriteRhsBinding == zc::none) ||
              (rhsIsLiteral && bodyWriteRhsLiteralIndex == zc::none)) {
            accumulatorOk = false;
            break;
          }
          // The accumulator and the loop init local are distinct owner
          // locals. The body write target and left operand resolve to the
          // accumulator; the body write right operand resolves to the loop
          // init local (IdentExpr) or is a scalar literal.
          if (ZC_ASSERT_NONNULL(accBinding) == ZC_ASSERT_NONNULL(initBinding) ||
              ZC_ASSERT_NONNULL(accBinding) != ZC_ASSERT_NONNULL(bodyWriteTargetBinding) ||
              ZC_ASSERT_NONNULL(accBinding) != ZC_ASSERT_NONNULL(bodyWriteLhsBinding) ||
              (!rhsIsLiteral &&
               ZC_ASSERT_NONNULL(initBinding) != ZC_ASSERT_NONNULL(bodyWriteRhsBinding))) {
            accumulatorOk = false;
            break;
          }
          size_t accTypeSlot = 0;
          size_t accLiteralSlot = 0;
          size_t bodyWriteResultTypeSlot = 0;
          size_t bodyWriteCallSlot = 0;
          size_t bodyWriteLhsTypeSlot = 0;
          size_t bodyWriteRhsTypeSlot = 0;
          ZC_IF_SOME(index, accTypeIndex) { accTypeSlot = index; }
          ZC_IF_SOME(index, accLiteralIndex) { accLiteralSlot = index; }
          ZC_IF_SOME(index, bodyWriteResultTypeIndex) { bodyWriteResultTypeSlot = index; }
          ZC_IF_SOME(index, bodyWriteCallIndex) { bodyWriteCallSlot = index; }
          ZC_IF_SOME(index, bodyWriteLhsTypeIndex) { bodyWriteLhsTypeSlot = index; }
          ZC_IF_SOME(index, bodyWriteRhsTypeIndex) { bodyWriteRhsTypeSlot = index; }
          const auto accType = facts.nodeTypes().entries()[accTypeSlot].value;
          const auto& accLiteralFact = facts.literals().entries()[accLiteralSlot].value;
          const auto bodyWriteResultType =
              facts.nodeTypes().entries()[bodyWriteResultTypeSlot].value;
          const auto bodyWriteLhsType = facts.nodeTypes().entries()[bodyWriteLhsTypeSlot].value;
          const auto bodyWriteRhsType = facts.nodeTypes().entries()[bodyWriteRhsTypeSlot].value;
          const auto& bodyWriteCallFact = facts.calls().entries()[bodyWriteCallSlot].value;
          const auto& bodyWriteSelected = bodyWriteCallFact.invocation.selected.variant();
          if (!bodyWriteSelected.is<checker::checked::PrimitiveCallable>()) {
            accumulatorOk = false;
            break;
          }
          const auto bodyWriteOperation =
              bodyWriteSelected.get<checker::checked::PrimitiveCallable>().operation;
          if (!isScalarArithmeticOperation(bodyWriteOperation) || bodyWriteLhsType != accType ||
              bodyWriteRhsType != accType || bodyWriteResultType != accType ||
              accLiteralFact.type != accType) {
            accumulatorOk = false;
            break;
          }
          if (rhsIsLiteral) {
            size_t bodyWriteRhsLiteralSlot = 0;
            ZC_IF_SOME(index, bodyWriteRhsLiteralIndex) { bodyWriteRhsLiteralSlot = index; }
            const auto& bodyWriteRhsLiteralFact =
                facts.literals().entries()[bodyWriteRhsLiteralSlot].value;
            if (bodyWriteRhsLiteralFact.type != bodyWriteRhsType) {
              accumulatorOk = false;
              break;
            }
            accumulators.add(PendingForLoopAccumulator{
                HirLocalBinding{HirNodeId(), hirLocalId(static_cast<uint32_t>(accIndex + 1)),
                                accType, zc::none, ZC_ASSERT_NONNULL(accPatternSpan).clone(),
                                ZC_ASSERT_NONNULL(accInitializerSpan).clone()},
                HirScalarLiteralExpression{HirNodeId(), accType, accLiteralFact.literal.clone(),
                                           HirValueCategory::Value,
                                           ZC_ASSERT_NONNULL(accInitializerSpan).clone()},
                HirLocalWriteStatement{HirNodeId(), hirLocalId(static_cast<uint32_t>(accIndex + 1)),
                                       zc::none, bodyWriteResultType, HirNodeId(),
                                       HirLocalWriteKind::Overwrite,
                                       ZC_ASSERT_NONNULL(bodyWriteSpan).clone(),
                                       ZC_ASSERT_NONNULL(bodyWriteValueSpan).clone()},
                HirPrimitiveBinaryExpression{HirNodeId(), HirNodeId(), HirNodeId(),
                                             bodyWriteLhsType, bodyWriteResultType,
                                             HirValueCategory::Value, bodyWriteOperation,
                                             ZC_ASSERT_NONNULL(bodyWriteValueSpan).clone()},
                HirLocalReferenceExpression{
                    HirNodeId(), hirLocalId(static_cast<uint32_t>(accIndex + 1)), bodyWriteLhsType,
                    HirValueCategory::Place, ZC_ASSERT_NONNULL(bodyWriteLhsSpan).clone()},
                true, zc::none,
                zc::some(HirScalarLiteralExpression{
                    HirNodeId(), bodyWriteRhsType, bodyWriteRhsLiteralFact.literal.clone(),
                    HirValueCategory::Value, ZC_ASSERT_NONNULL(bodyWriteRhsSpan).clone()}),
                accPattern, bodyWriteExpr});
          } else {
            accumulators.add(PendingForLoopAccumulator{
                HirLocalBinding{HirNodeId(), hirLocalId(static_cast<uint32_t>(accIndex + 1)),
                                accType, zc::none, ZC_ASSERT_NONNULL(accPatternSpan).clone(),
                                ZC_ASSERT_NONNULL(accInitializerSpan).clone()},
                HirScalarLiteralExpression{HirNodeId(), accType, accLiteralFact.literal.clone(),
                                           HirValueCategory::Value,
                                           ZC_ASSERT_NONNULL(accInitializerSpan).clone()},
                HirLocalWriteStatement{HirNodeId(), hirLocalId(static_cast<uint32_t>(accIndex + 1)),
                                       zc::none, bodyWriteResultType, HirNodeId(),
                                       HirLocalWriteKind::Overwrite,
                                       ZC_ASSERT_NONNULL(bodyWriteSpan).clone(),
                                       ZC_ASSERT_NONNULL(bodyWriteValueSpan).clone()},
                HirPrimitiveBinaryExpression{HirNodeId(), HirNodeId(), HirNodeId(),
                                             bodyWriteLhsType, bodyWriteResultType,
                                             HirValueCategory::Value, bodyWriteOperation,
                                             ZC_ASSERT_NONNULL(bodyWriteValueSpan).clone()},
                HirLocalReferenceExpression{
                    HirNodeId(), hirLocalId(static_cast<uint32_t>(accIndex + 1)), bodyWriteLhsType,
                    HirValueCategory::Place, ZC_ASSERT_NONNULL(bodyWriteLhsSpan).clone()},
                false,
                zc::some(HirLocalReferenceExpression{
                    HirNodeId(), hirLocalId(static_cast<uint32_t>(accumulatorCount + 1)),
                    bodyWriteRhsType, HirValueCategory::Place,
                    ZC_ASSERT_NONNULL(bodyWriteRhsSpan).clone()}),
                zc::none, accPattern, bodyWriteExpr});
          }
        }
        if (!accumulatorOk || accumulators.size() != accumulatorCount) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        // Return reference: the trailing `return <accumulator-local>;` names
        // the first accumulator local.
        auto returnBinding = resolvedOwnerLocal(bound.bindings(), shape.value);
        auto returnTypeIndex = factIndex(facts.nodeTypes(), shape.value);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(shape.value).range);
        if (returnBinding == zc::none || returnTypeIndex == zc::none || returnSpan == zc::none ||
            ZC_ASSERT_NONNULL(returnBinding) !=
                ZC_ASSERT_NONNULL(ownerLocalBindingForPattern(
                    bound.definitions(), shape.forLoopAccumulatorPatterns[0], tree))) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        // Validate the loop init/cond/update facts.
        if (initBinding == zc::none || initTypeIndex == zc::none || initLiteralIndex == zc::none ||
            patternSpan == zc::none || initializerSpan == zc::none ||
            condResultTypeIndex == zc::none || condCallIndex == zc::none ||
            condLhsTypeIndex == zc::none || condRhsTypeIndex == zc::none ||
            condLhsBinding == zc::none || condRhsLiteralIndex == zc::none || condSpan == zc::none ||
            condLhsSpan == zc::none || condRhsSpan == zc::none || updateTargetBinding == zc::none ||
            updateValueTypeIndex == zc::none || updateCallIndex == zc::none ||
            updateLhsTypeIndex == zc::none || updateRhsTypeIndex == zc::none ||
            updateLhsBinding == zc::none || updateRhsLiteralIndex == zc::none ||
            updateSpan == zc::none || updateValueSpan == zc::none || updateLhsSpan == zc::none ||
            updateRhsSpan == zc::none || loopSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        if (ZC_ASSERT_NONNULL(initBinding) != ZC_ASSERT_NONNULL(condLhsBinding) ||
            ZC_ASSERT_NONNULL(initBinding) != ZC_ASSERT_NONNULL(updateTargetBinding) ||
            ZC_ASSERT_NONNULL(initBinding) != ZC_ASSERT_NONNULL(updateLhsBinding)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        // Resolve the init type early so the break condition validation can
        // compare against it.
        size_t initTypeSlot = 0;
        ZC_IF_SOME(index, initTypeIndex) { initTypeSlot = index; }
        const auto initType = facts.nodeTypes().entries()[initTypeSlot].value;
        // Resolve the break condition comparison facts when a guarded break is
        // present. The left operand must resolve to the loop init local and the
        // operation must be a scalar comparison.
        checker::PrimitiveOperation breakCondOperation = checker::PrimitiveOperation::Eq;
        identity::SemanticTypeId breakCondResultType{};
        identity::SemanticTypeId breakCondLhsType{};
        identity::SemanticTypeId breakCondRhsType{};
        zc::Maybe<checker::checked::CanonicalConstValue> breakCondRhsLiteral;
        bool breakCondResolved = false;
        if (shape.forLoopBodyBreakCondition) {
          if (breakCondResultTypeIndex == zc::none || breakCondCallIndex == zc::none ||
              breakCondLhsTypeIndex == zc::none || breakCondRhsTypeIndex == zc::none ||
              breakCondLhsBinding == zc::none || breakCondRhsLiteralIndex == zc::none ||
              breakCondSpan == zc::none || breakCondLhsSpan == zc::none ||
              breakCondRhsSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          if (ZC_ASSERT_NONNULL(initBinding) != ZC_ASSERT_NONNULL(breakCondLhsBinding)) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          size_t breakCondResultTypeSlot = 0;
          size_t breakCondCallSlot = 0;
          size_t breakCondLhsTypeSlot = 0;
          size_t breakCondRhsTypeSlot = 0;
          size_t breakCondRhsLiteralSlot = 0;
          ZC_IF_SOME(index, breakCondResultTypeIndex) { breakCondResultTypeSlot = index; }
          ZC_IF_SOME(index, breakCondCallIndex) { breakCondCallSlot = index; }
          ZC_IF_SOME(index, breakCondLhsTypeIndex) { breakCondLhsTypeSlot = index; }
          ZC_IF_SOME(index, breakCondRhsTypeIndex) { breakCondRhsTypeSlot = index; }
          ZC_IF_SOME(index, breakCondRhsLiteralIndex) { breakCondRhsLiteralSlot = index; }
          breakCondResultType = facts.nodeTypes().entries()[breakCondResultTypeSlot].value;
          breakCondLhsType = facts.nodeTypes().entries()[breakCondLhsTypeSlot].value;
          breakCondRhsType = facts.nodeTypes().entries()[breakCondRhsTypeSlot].value;
          const auto& breakCondRhsLiteralFact =
              facts.literals().entries()[breakCondRhsLiteralSlot].value;
          breakCondRhsLiteral = breakCondRhsLiteralFact.literal.clone();
          const auto& breakCondCallFact = facts.calls().entries()[breakCondCallSlot].value;
          const auto& breakCondSelected = breakCondCallFact.invocation.selected.variant();
          if (!breakCondSelected.is<checker::checked::PrimitiveCallable>()) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          breakCondOperation =
              breakCondSelected.get<checker::checked::PrimitiveCallable>().operation;
          if (!isScalarComparisonOperation(breakCondOperation) ||
              !isBoolSemanticType(checkedModule.semanticTypes(), breakCondResultType) ||
              breakCondLhsType != breakCondRhsType || breakCondLhsType != initType ||
              breakCondRhsLiteralFact.type != breakCondRhsType) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          breakCondResolved = true;
        }
        size_t initLiteralSlot = 0;
        size_t condResultTypeSlot = 0;
        size_t condCallSlot = 0;
        size_t condLhsTypeSlot = 0;
        size_t condRhsTypeSlot = 0;
        size_t condRhsLiteralSlot = 0;
        size_t updateValueTypeSlot = 0;
        size_t updateCallSlot = 0;
        size_t updateLhsTypeSlot = 0;
        size_t updateRhsTypeSlot = 0;
        size_t updateRhsLiteralSlot = 0;
        size_t returnTypeSlot = 0;
        ZC_IF_SOME(index, initLiteralIndex) { initLiteralSlot = index; }
        ZC_IF_SOME(index, condResultTypeIndex) { condResultTypeSlot = index; }
        ZC_IF_SOME(index, condCallIndex) { condCallSlot = index; }
        ZC_IF_SOME(index, condLhsTypeIndex) { condLhsTypeSlot = index; }
        ZC_IF_SOME(index, condRhsTypeIndex) { condRhsTypeSlot = index; }
        ZC_IF_SOME(index, condRhsLiteralIndex) { condRhsLiteralSlot = index; }
        ZC_IF_SOME(index, updateValueTypeIndex) { updateValueTypeSlot = index; }
        ZC_IF_SOME(index, updateCallIndex) { updateCallSlot = index; }
        ZC_IF_SOME(index, updateLhsTypeIndex) { updateLhsTypeSlot = index; }
        ZC_IF_SOME(index, updateRhsTypeIndex) { updateRhsTypeSlot = index; }
        ZC_IF_SOME(index, updateRhsLiteralIndex) { updateRhsLiteralSlot = index; }
        ZC_IF_SOME(index, returnTypeIndex) { returnTypeSlot = index; }
        const auto& initLiteralFact = facts.literals().entries()[initLiteralSlot].value;
        const auto condResultType = facts.nodeTypes().entries()[condResultTypeSlot].value;
        const auto condLhsType = facts.nodeTypes().entries()[condLhsTypeSlot].value;
        const auto condRhsType = facts.nodeTypes().entries()[condRhsTypeSlot].value;
        const auto& condRhsLiteralFact = facts.literals().entries()[condRhsLiteralSlot].value;
        const auto updateValueType = facts.nodeTypes().entries()[updateValueTypeSlot].value;
        const auto updateLhsType = facts.nodeTypes().entries()[updateLhsTypeSlot].value;
        const auto updateRhsType = facts.nodeTypes().entries()[updateRhsTypeSlot].value;
        const auto& updateRhsLiteralFact = facts.literals().entries()[updateRhsLiteralSlot].value;
        const auto returnType = facts.nodeTypes().entries()[returnTypeSlot].value;
        const auto& condCallFact = facts.calls().entries()[condCallSlot].value;
        const auto& condSelected = condCallFact.invocation.selected.variant();
        const auto& updateCallFact = facts.calls().entries()[updateCallSlot].value;
        const auto& updateSelected = updateCallFact.invocation.selected.variant();
        if (!condSelected.is<checker::checked::PrimitiveCallable>() ||
            !updateSelected.is<checker::checked::PrimitiveCallable>()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto condOperation =
            condSelected.get<checker::checked::PrimitiveCallable>().operation;
        const auto updateOperation =
            updateSelected.get<checker::checked::PrimitiveCallable>().operation;
        if (!isScalarComparisonOperation(condOperation) ||
            !isScalarArithmeticOperation(updateOperation) ||
            !isBoolSemanticType(checkedModule.semanticTypes(), condResultType) ||
            condLhsType != condRhsType || condLhsType != initType ||
            updateLhsType != updateRhsType || updateLhsType != initType ||
            updateValueType != updateLhsType || initLiteralFact.type != initType ||
            condRhsLiteralFact.type != condRhsType || updateRhsLiteralFact.type != updateRhsType ||
            accumulators[0].local.type != initType || returnType != accumulators[0].local.type ||
            returnType != callable.success) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        // Build the if-guarded break condition comparison when present. The
        // left operand is a place reference to the loop init local; the right
        // operand is the scalar literal. The lowering function materializes the
        // nodes and wires them to the loop statement's breakCondition field.
        zc::Maybe<HirPrimitiveBinaryExpression> breakCondition;
        zc::Maybe<HirLocalReferenceExpression> breakConditionLeft;
        zc::Maybe<HirScalarLiteralExpression> breakConditionRight;
        if (breakCondResolved) {
          breakCondition = zc::some(HirPrimitiveBinaryExpression{
              HirNodeId(), HirNodeId(), HirNodeId(), breakCondLhsType, breakCondResultType,
              HirValueCategory::Value, breakCondOperation,
              ZC_ASSERT_NONNULL(breakCondSpan).clone()});
          breakConditionLeft = zc::some(HirLocalReferenceExpression{
              HirNodeId(), hirLocalId(static_cast<uint32_t>(accumulatorCount + 1)),
              breakCondLhsType, HirValueCategory::Place,
              ZC_ASSERT_NONNULL(breakCondLhsSpan).clone()});
          breakConditionRight = zc::some(HirScalarLiteralExpression{
              HirNodeId(), breakCondRhsType, zc::mv(ZC_ASSERT_NONNULL(breakCondRhsLiteral)),
              HirValueCategory::Value, ZC_ASSERT_NONNULL(breakCondRhsSpan).clone()});
        }
        forLoopAccumulatorReturn = PendingForLoopAccumulatorReturn{
            HirPrimitiveBinaryExpression{HirNodeId(), HirNodeId(), HirNodeId(), condLhsType,
                                         condResultType, HirValueCategory::Value, condOperation,
                                         ZC_ASSERT_NONNULL(condSpan).clone()},
            HirLocalReferenceExpression{
                HirNodeId(), hirLocalId(static_cast<uint32_t>(accumulatorCount + 1)), condLhsType,
                HirValueCategory::Place, ZC_ASSERT_NONNULL(condLhsSpan).clone()},
            HirScalarLiteralExpression{HirNodeId(), condRhsType, condRhsLiteralFact.literal.clone(),
                                       HirValueCategory::Value,
                                       ZC_ASSERT_NONNULL(condRhsSpan).clone()},
            ZC_ASSERT_NONNULL(loopSpan).clone(),
            HirLocalBinding{HirNodeId(), hirLocalId(static_cast<uint32_t>(accumulatorCount + 1)),
                            initType, zc::none, ZC_ASSERT_NONNULL(patternSpan).clone(),
                            ZC_ASSERT_NONNULL(initializerSpan).clone()},
            HirScalarLiteralExpression{HirNodeId(), initType, initLiteralFact.literal.clone(),
                                       HirValueCategory::Value,
                                       ZC_ASSERT_NONNULL(initializerSpan).clone()},
            HirLocalWriteStatement{
                HirNodeId(), hirLocalId(static_cast<uint32_t>(accumulatorCount + 1)), zc::none,
                updateValueType, HirNodeId(), HirLocalWriteKind::Overwrite,
                ZC_ASSERT_NONNULL(updateSpan).clone(), ZC_ASSERT_NONNULL(updateValueSpan).clone()},
            HirPrimitiveBinaryExpression{HirNodeId(), HirNodeId(), HirNodeId(), updateLhsType,
                                         updateValueType, HirValueCategory::Value, updateOperation,
                                         ZC_ASSERT_NONNULL(updateValueSpan).clone()},
            HirLocalReferenceExpression{
                HirNodeId(), hirLocalId(static_cast<uint32_t>(accumulatorCount + 1)), updateLhsType,
                HirValueCategory::Place, ZC_ASSERT_NONNULL(updateLhsSpan).clone()},
            HirScalarLiteralExpression{
                HirNodeId(), updateRhsType, updateRhsLiteralFact.literal.clone(),
                HirValueCategory::Value, ZC_ASSERT_NONNULL(updateRhsSpan).clone()},
            zc::mv(accumulators),
            HirLocalReferenceExpression{HirNodeId(), hirLocalId(1), returnType,
                                        HirValueCategory::Place,
                                        ZC_ASSERT_NONNULL(returnSpan).clone()},
            zc::mv(breakSpan),
            zc::mv(continueSpan),
            zc::mv(breakCondition),
            zc::mv(breakConditionLeft),
            zc::mv(breakConditionRight),
            shape.forLoopStatement,
            shape.returnStatement};
      }
      // For-in loop accumulator shape: N leading scalar `mut` accumulator
      // locals, a `for (let x in [<lit>, ...]) { <accumulator> =
      // <accumulator> <bin> x; ... }` loop, and a trailing
      // `return <accumulator-local>;`. The loop is compile-time unrolled: each
      // array element literal is substituted for the binding in the body write
      // template. Resolve the accumulator locals, the body write template, the
      // element literals, and the return reference into a pending carrier.
      zc::Maybe<PendingForInLoopAccumulatorReturn> forInLoopAccumulatorReturn;
      if (shape.isForInLoopAccumulator) {
        const size_t accumulatorCount = shape.forInAccumulatorPatterns.size();
        // Resolve the N accumulator locals: pattern binding, initializer type
        // and literal facts, and source spans.
        zc::Vector<PendingForInLoopAccumulatorReturn::Accumulator> accumulators;
        bool accumulatorOk = true;
        for (size_t accIndex = 0; accIndex < accumulatorCount; ++accIndex) {
          const ast::NodeId accPattern = shape.forInAccumulatorPatterns[accIndex];
          const ast::NodeId accInitializer = shape.forInAccumulatorInitializers[accIndex];
          auto accBinding = ownerLocalBindingForPattern(bound.definitions(), accPattern, tree);
          auto accTypeIndex = factIndex(facts.nodeTypes(), accInitializer);
          auto accLiteralIndex = factIndex(facts.literals(), accInitializer);
          auto accPatternSpan = bound.parsedModule().spanFor(tree.node(accPattern).range);
          auto accInitializerSpan = bound.parsedModule().spanFor(tree.node(accInitializer).range);
          if (accBinding == zc::none || accTypeIndex == zc::none || accLiteralIndex == zc::none ||
              accPatternSpan == zc::none || accInitializerSpan == zc::none) {
            accumulatorOk = false;
            break;
          }
          size_t accTypeSlot = 0;
          size_t accLiteralSlot = 0;
          ZC_IF_SOME(index, accTypeIndex) { accTypeSlot = index; }
          ZC_IF_SOME(index, accLiteralIndex) { accLiteralSlot = index; }
          const auto accType = facts.nodeTypes().entries()[accTypeSlot].value;
          const auto& accLiteralFact = facts.literals().entries()[accLiteralSlot].value;
          const auto accLocalId = hirLocalId(static_cast<uint32_t>(accIndex + 1));
          accumulators.add(PendingForInLoopAccumulatorReturn::Accumulator{
              HirLocalBinding{HirNodeId(), accLocalId, accType, zc::none,
                              ZC_ASSERT_NONNULL(accPatternSpan).clone(),
                              ZC_ASSERT_NONNULL(accInitializerSpan).clone()},
              HirScalarLiteralExpression{HirNodeId(), accType, accLiteralFact.literal.clone(),
                                         HirValueCategory::Value,
                                         ZC_ASSERT_NONNULL(accInitializerSpan).clone()},
              accPattern});
        }
        // Resolve the body write template from the first accumulator's body
        // write. All accumulators share the same binary operation; the builder
        // validates this when it resolves each write's call fact.
        checker::PrimitiveOperation operation{};
        identity::SemanticTypeId operandType{};
        identity::SemanticTypeId resultType{};
        zc::Maybe<identity::SourceSpan> writeSpan;
        zc::Maybe<identity::SourceSpan> valueSpan;
        zc::Maybe<identity::SourceSpan> leftSpan;
        zc::Vector<ast::NodeId> bodyWriteNodes;
        if (accumulatorOk) {
          for (size_t accIndex = 0; accIndex < accumulatorCount; ++accIndex) {
            const ast::NodeId bodyWriteStmt = shape.forInBodyWrites[accIndex];
            auto bodyWriteItem = statementItem(tree, bodyWriteStmt);
            if (bodyWriteItem == zc::none) {
              accumulatorOk = false;
              break;
            }
            ast::NodeId bodyWriteStmtNode;
            ZC_IF_SOME(item, bodyWriteItem) { bodyWriteStmtNode = item; }
            const ast::NodeId bodyWriteExpr(
                tree.node(bodyWriteStmtNode)
                    .payload.words[ast::kExpressionStatementExpressionWord]);
            const ast::NodeId bodyWriteTarget(
                tree.node(bodyWriteExpr).payload.words[ast::kAssignmentExprLhsWord]);
            const ast::NodeId bodyWriteValueNode(
                tree.node(bodyWriteExpr).payload.words[ast::kAssignmentExprRhsWord]);
            if (!tree.contains(bodyWriteExpr) ||
                tree.node(bodyWriteExpr).kind != ast::SyntaxKind::AssignmentExpr ||
                !tree.contains(bodyWriteTarget) ||
                tree.node(bodyWriteTarget).kind != ast::SyntaxKind::IdentExpr ||
                !tree.contains(bodyWriteValueNode) ||
                tree.node(bodyWriteValueNode).kind != ast::SyntaxKind::BinaryExpr) {
              accumulatorOk = false;
              break;
            }
            const auto& bodyWriteBin = tree.node(bodyWriteValueNode);
            const ast::NodeId bodyWriteLhs(bodyWriteBin.payload.words[ast::kBinaryExprLhsWord]);
            const ast::NodeId bodyWriteRhs(bodyWriteBin.payload.words[ast::kBinaryExprRhsWord]);
            if (!tree.contains(bodyWriteLhs) ||
                tree.node(bodyWriteLhs).kind != ast::SyntaxKind::IdentExpr ||
                !tree.contains(bodyWriteRhs) ||
                tree.node(bodyWriteRhs).kind != ast::SyntaxKind::IdentExpr) {
              accumulatorOk = false;
              break;
            }
            auto bodyWriteTargetBinding = resolvedOwnerLocal(bound.bindings(), bodyWriteTarget);
            auto bodyWriteLhsBinding = resolvedOwnerLocal(bound.bindings(), bodyWriteLhs);
            auto bodyWriteResultTypeIndex = factIndex(facts.nodeTypes(), bodyWriteValueNode);
            auto bodyWriteCallIndex = factIndex(facts.calls(), bodyWriteValueNode);
            auto bodyWriteLhsTypeIndex = factIndex(facts.nodeTypes(), bodyWriteLhs);
            auto bodyWriteRhsTypeIndex = factIndex(facts.nodeTypes(), bodyWriteRhs);
            auto bodyWriteSpan = bound.parsedModule().spanFor(tree.node(bodyWriteExpr).range);
            auto bodyWriteValueSpan = bound.parsedModule().spanFor(bodyWriteBin.range);
            auto bodyWriteLhsSpan = bound.parsedModule().spanFor(tree.node(bodyWriteLhs).range);
            if (bodyWriteTargetBinding == zc::none || bodyWriteLhsBinding == zc::none ||
                bodyWriteResultTypeIndex == zc::none || bodyWriteCallIndex == zc::none ||
                bodyWriteLhsTypeIndex == zc::none || bodyWriteRhsTypeIndex == zc::none ||
                bodyWriteSpan == zc::none || bodyWriteValueSpan == zc::none ||
                bodyWriteLhsSpan == zc::none) {
              accumulatorOk = false;
              break;
            }
            // The target and left operand must resolve to this accumulator.
            if (ZC_ASSERT_NONNULL(bodyWriteTargetBinding) !=
                    ownerLocalBindingForPattern(bound.definitions(),
                                                shape.forInAccumulatorPatterns[accIndex], tree) ||
                ZC_ASSERT_NONNULL(bodyWriteLhsBinding) !=
                    ownerLocalBindingForPattern(bound.definitions(),
                                                shape.forInAccumulatorPatterns[accIndex], tree)) {
              accumulatorOk = false;
              break;
            }
            size_t bodyWriteResultTypeSlot = 0;
            size_t bodyWriteCallSlot = 0;
            size_t bodyWriteLhsTypeSlot = 0;
            size_t bodyWriteRhsTypeSlot = 0;
            ZC_IF_SOME(index, bodyWriteResultTypeIndex) { bodyWriteResultTypeSlot = index; }
            ZC_IF_SOME(index, bodyWriteCallIndex) { bodyWriteCallSlot = index; }
            ZC_IF_SOME(index, bodyWriteLhsTypeIndex) { bodyWriteLhsTypeSlot = index; }
            ZC_IF_SOME(index, bodyWriteRhsTypeIndex) { bodyWriteRhsTypeSlot = index; }
            const auto bodyWriteResultType =
                facts.nodeTypes().entries()[bodyWriteResultTypeSlot].value;
            const auto bodyWriteLhsType = facts.nodeTypes().entries()[bodyWriteLhsTypeSlot].value;
            const auto bodyWriteRhsType = facts.nodeTypes().entries()[bodyWriteRhsTypeSlot].value;
            const auto& bodyWriteCallFact = facts.calls().entries()[bodyWriteCallSlot].value;
            const auto& bodyWriteSelected = bodyWriteCallFact.invocation.selected.variant();
            if (!bodyWriteSelected.is<checker::checked::PrimitiveCallable>()) {
              accumulatorOk = false;
              break;
            }
            const auto bodyWriteOperation =
                bodyWriteSelected.get<checker::checked::PrimitiveCallable>().operation;
            if (accIndex == 0) {
              operation = bodyWriteOperation;
              operandType = bodyWriteLhsType;
              resultType = bodyWriteResultType;
              writeSpan = zc::some(ZC_ASSERT_NONNULL(bodyWriteSpan).clone());
              valueSpan = zc::some(ZC_ASSERT_NONNULL(bodyWriteValueSpan).clone());
              leftSpan = zc::some(ZC_ASSERT_NONNULL(bodyWriteLhsSpan).clone());
            } else if (bodyWriteOperation != operation || bodyWriteLhsType != operandType ||
                       bodyWriteRhsType != operandType || bodyWriteResultType != resultType) {
              accumulatorOk = false;
              break;
            }
            bodyWriteNodes.add(bodyWriteExpr);
          }
        }
        // Resolve the array element literals.
        zc::Vector<HirScalarLiteralExpression> elements;
        if (accumulatorOk) {
          const auto& arrayLit = tree.node(shape.forInCollection);
          const ast::NodeList elems{arrayLit.payload.words[ast::kArrayLiteralElemsFirstWord],
                                    arrayLit.payload.words[ast::kArrayLiteralElemsSizeWord]};
          if (!tree.contains(elems) || elems.size == 0) {
            accumulatorOk = false;
          } else {
            for (const auto& elem : tree.list(elems)) {
              auto elemTypeIndex = factIndex(facts.nodeTypes(), elem);
              auto elemLiteralIndex = factIndex(facts.literals(), elem);
              auto elemSpan = bound.parsedModule().spanFor(tree.node(elem).range);
              if (elemTypeIndex == zc::none || elemLiteralIndex == zc::none ||
                  elemSpan == zc::none) {
                accumulatorOk = false;
                break;
              }
              size_t elemTypeSlot = 0;
              size_t elemLiteralSlot = 0;
              ZC_IF_SOME(index, elemTypeIndex) { elemTypeSlot = index; }
              ZC_IF_SOME(index, elemLiteralIndex) { elemLiteralSlot = index; }
              const auto elemType = facts.nodeTypes().entries()[elemTypeSlot].value;
              const auto& elemLiteralFact = facts.literals().entries()[elemLiteralSlot].value;
              if (elemType != operandType) {
                accumulatorOk = false;
                break;
              }
              elements.add(HirScalarLiteralExpression{
                  HirNodeId(), elemType, elemLiteralFact.literal.clone(), HirValueCategory::Value,
                  ZC_ASSERT_NONNULL(elemSpan).clone()});
            }
          }
        }
        // Resolve the return reference: the trailing `return
        // <accumulator-local>;` names the first accumulator local.
        zc::Maybe<HirLocalReferenceExpression> returnReference;
        if (accumulatorOk) {
          auto returnBinding = resolvedOwnerLocal(bound.bindings(), shape.value);
          auto returnTypeIndex = factIndex(facts.nodeTypes(), shape.value);
          auto returnSpan = bound.parsedModule().spanFor(tree.node(shape.value).range);
          if (returnBinding == zc::none || returnTypeIndex == zc::none || returnSpan == zc::none ||
              ZC_ASSERT_NONNULL(returnBinding) !=
                  ownerLocalBindingForPattern(bound.definitions(),
                                              shape.forInAccumulatorPatterns[0], tree)) {
            accumulatorOk = false;
          } else {
            size_t returnTypeSlot = 0;
            ZC_IF_SOME(index, returnTypeIndex) { returnTypeSlot = index; }
            const auto returnType = facts.nodeTypes().entries()[returnTypeSlot].value;
            returnReference = zc::some(HirLocalReferenceExpression{
                HirNodeId(), hirLocalId(1), returnType, HirValueCategory::Place,
                ZC_ASSERT_NONNULL(returnSpan).clone()});
          }
        }
        if (accumulatorOk && writeSpan != zc::none && valueSpan != zc::none &&
            leftSpan != zc::none && returnReference != zc::none) {
          forInLoopAccumulatorReturn =
              PendingForInLoopAccumulatorReturn{zc::mv(accumulators),
                                                operation,
                                                operandType,
                                                resultType,
                                                zc::mv(ZC_ASSERT_NONNULL(writeSpan)),
                                                zc::mv(ZC_ASSERT_NONNULL(valueSpan)),
                                                zc::mv(ZC_ASSERT_NONNULL(leftSpan)),
                                                zc::mv(elements),
                                                zc::mv(ZC_ASSERT_NONNULL(returnReference)),
                                                shape.forInLoopStatement,
                                                shape.returnStatement,
                                                zc::mv(bodyWriteNodes)};
        }
      }
      // Nested for-loop accumulator shape: N leading scalar `mut` accumulator
      // locals, an outer `for` loop whose sole body statement is an inner
      // `for` loop that writes each accumulator, and a trailing
      // `return <accumulator-local>;`. Resolve the outer and inner loop
      // init/cond/update, the N accumulator body writes, and the return
      // reference into a self-contained pending carrier.
      zc::Maybe<PendingNestedForLoopAccumulatorReturn> nestedForLoopAccumulatorReturn;
      if (shape.isNestedForLoopAccumulator) {
        const size_t accumulatorCount = shape.forLoopAccumulatorPatterns.size();
        // Resolve a for-loop init/cond/update from AST nodes. Returns true on
        // success and fills the output carriers.
        auto resolveLoopParts =
            [&](ast::NodeId initNode, ast::NodeId condNode, ast::NodeId updateNode,
                ast::NodeId loopStmtNode, uint32_t initLocalOrdinal,
                zc::Maybe<HirLocalBinding>& outLocal,
                zc::Maybe<HirScalarLiteralExpression>& outInitLiteral,
                zc::Maybe<HirPrimitiveBinaryExpression>& outCondition,
                zc::Maybe<HirLocalReferenceExpression>& outConditionLeft,
                zc::Maybe<HirScalarLiteralExpression>& outConditionRight,
                zc::Maybe<identity::SourceSpan>& outLoopSpan,
                zc::Maybe<HirLocalWriteStatement>& outWrite,
                zc::Maybe<HirPrimitiveBinaryExpression>& outWriteValue,
                zc::Maybe<HirLocalReferenceExpression>& outWriteValueLeft,
                zc::Maybe<HirScalarLiteralExpression>& outWriteValueRight) -> bool {
          const auto& letNode = tree.node(initNode);
          const ast::NodeId declarations(letNode.payload.words[ast::kLetStmtDeclarationsWord]);
          const ast::NodeList declarators{
              tree.node(declarations).payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
              tree.node(declarations).payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
          if (!tree.contains(declarations) ||
              tree.node(declarations).kind != ast::SyntaxKind::VariableDeclaratorList ||
              !tree.contains(declarators) || declarators.size != 1) {
            return false;
          }
          const auto declarator = tree.list(declarators)[0];
          const ast::NodeId pattern(
              tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]);
          const ast::NodeId initializer(
              tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
          if (!tree.contains(pattern) ||
              tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern ||
              !tree.contains(initializer) || !isScalarLiteral(tree.node(initializer).kind)) {
            return false;
          }
          auto initBinding = ownerLocalBindingForPattern(bound.definitions(), pattern, tree);
          auto initTypeIndex = factIndex(facts.nodeTypes(), initializer);
          auto initLiteralIndex = factIndex(facts.literals(), initializer);
          auto patternSpan = bound.parsedModule().spanFor(tree.node(pattern).range);
          auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
          if (initBinding == zc::none || initTypeIndex == zc::none ||
              initLiteralIndex == zc::none || patternSpan == zc::none ||
              initializerSpan == zc::none) {
            return false;
          }
          size_t initTypeSlot = 0;
          size_t initLiteralSlot = 0;
          ZC_IF_SOME(index, initTypeIndex) { initTypeSlot = index; }
          ZC_IF_SOME(index, initLiteralIndex) { initLiteralSlot = index; }
          const auto initType = facts.nodeTypes().entries()[initTypeSlot].value;
          const auto& initLiteralFact = facts.literals().entries()[initLiteralSlot].value;
          // Cond: binary comparison with identifier left and scalar literal right.
          const auto& condAstNode = tree.node(condNode);
          const ast::NodeId condLhs(condAstNode.payload.words[ast::kBinaryExprLhsWord]);
          const ast::NodeId condRhs(condAstNode.payload.words[ast::kBinaryExprRhsWord]);
          if (!tree.contains(condLhs) || tree.node(condLhs).kind != ast::SyntaxKind::IdentExpr ||
              !tree.contains(condRhs) || !isScalarLiteral(tree.node(condRhs).kind)) {
            return false;
          }
          auto condResultTypeIndex = factIndex(facts.nodeTypes(), condNode);
          auto condCallIndex = factIndex(facts.calls(), condNode);
          auto condLhsTypeIndex = factIndex(facts.nodeTypes(), condLhs);
          auto condRhsTypeIndex = factIndex(facts.nodeTypes(), condRhs);
          auto condLhsBinding = resolvedOwnerLocal(bound.bindings(), condLhs);
          auto condRhsLiteralIndex = factIndex(facts.literals(), condRhs);
          auto condSpan = bound.parsedModule().spanFor(condAstNode.range);
          auto condLhsSpan = bound.parsedModule().spanFor(tree.node(condLhs).range);
          auto condRhsSpan = bound.parsedModule().spanFor(tree.node(condRhs).range);
          if (condResultTypeIndex == zc::none || condCallIndex == zc::none ||
              condLhsTypeIndex == zc::none || condRhsTypeIndex == zc::none ||
              condLhsBinding == zc::none || condRhsLiteralIndex == zc::none ||
              condSpan == zc::none || condLhsSpan == zc::none || condRhsSpan == zc::none) {
            return false;
          }
          size_t condResultTypeSlot = 0;
          size_t condCallSlot = 0;
          size_t condLhsTypeSlot = 0;
          size_t condRhsTypeSlot = 0;
          size_t condRhsLiteralSlot = 0;
          ZC_IF_SOME(index, condResultTypeIndex) { condResultTypeSlot = index; }
          ZC_IF_SOME(index, condCallIndex) { condCallSlot = index; }
          ZC_IF_SOME(index, condLhsTypeIndex) { condLhsTypeSlot = index; }
          ZC_IF_SOME(index, condRhsTypeIndex) { condRhsTypeSlot = index; }
          ZC_IF_SOME(index, condRhsLiteralIndex) { condRhsLiteralSlot = index; }
          const auto condResultType = facts.nodeTypes().entries()[condResultTypeSlot].value;
          const auto condLhsType = facts.nodeTypes().entries()[condLhsTypeSlot].value;
          const auto condRhsType = facts.nodeTypes().entries()[condRhsTypeSlot].value;
          const auto& condCallFact = facts.calls().entries()[condCallSlot].value;
          const auto& condSelected = condCallFact.invocation.selected.variant();
          if (!condSelected.is<checker::checked::PrimitiveCallable>()) { return false; }
          const auto condOperation =
              condSelected.get<checker::checked::PrimitiveCallable>().operation;
          if (!isScalarComparisonOperation(condOperation) || condLhsType != initType ||
              condRhsType != initType) {
            return false;
          }
          const auto& condRhsLiteralFact = facts.literals().entries()[condRhsLiteralSlot].value;
          if (condRhsLiteralFact.type != condRhsType) { return false; }
          // Update: assignment of arithmetic binary to the loop local.
          const auto& updateAstNode = tree.node(updateNode);
          const ast::NodeId updateTarget(updateAstNode.payload.words[ast::kAssignmentExprLhsWord]);
          const ast::NodeId updateValueNode(
              updateAstNode.payload.words[ast::kAssignmentExprRhsWord]);
          if (!tree.contains(updateTarget) ||
              tree.node(updateTarget).kind != ast::SyntaxKind::IdentExpr ||
              !tree.contains(updateValueNode) ||
              tree.node(updateValueNode).kind != ast::SyntaxKind::BinaryExpr) {
            return false;
          }
          const auto& updateValueBin = tree.node(updateValueNode);
          const ast::NodeId updateLhs(updateValueBin.payload.words[ast::kBinaryExprLhsWord]);
          const ast::NodeId updateRhs(updateValueBin.payload.words[ast::kBinaryExprRhsWord]);
          if (!tree.contains(updateLhs) ||
              tree.node(updateLhs).kind != ast::SyntaxKind::IdentExpr ||
              !tree.contains(updateRhs) || !isScalarLiteral(tree.node(updateRhs).kind)) {
            return false;
          }
          auto updateTargetBinding = resolvedOwnerLocal(bound.bindings(), updateTarget);
          auto updateValueTypeIndex = factIndex(facts.nodeTypes(), updateValueNode);
          auto updateCallIndex = factIndex(facts.calls(), updateValueNode);
          auto updateLhsTypeIndex = factIndex(facts.nodeTypes(), updateLhs);
          auto updateRhsTypeIndex = factIndex(facts.nodeTypes(), updateRhs);
          auto updateLhsBinding = resolvedOwnerLocal(bound.bindings(), updateLhs);
          auto updateRhsLiteralIndex = factIndex(facts.literals(), updateRhs);
          auto updateSpan = bound.parsedModule().spanFor(updateAstNode.range);
          auto updateValueSpan = bound.parsedModule().spanFor(updateValueBin.range);
          auto updateLhsSpan = bound.parsedModule().spanFor(tree.node(updateLhs).range);
          auto updateRhsSpan = bound.parsedModule().spanFor(tree.node(updateRhs).range);
          if (updateTargetBinding == zc::none || updateValueTypeIndex == zc::none ||
              updateCallIndex == zc::none || updateLhsTypeIndex == zc::none ||
              updateRhsTypeIndex == zc::none || updateLhsBinding == zc::none ||
              updateRhsLiteralIndex == zc::none || updateSpan == zc::none ||
              updateValueSpan == zc::none || updateLhsSpan == zc::none ||
              updateRhsSpan == zc::none) {
            return false;
          }
          size_t updateValueTypeSlot = 0;
          size_t updateCallSlot = 0;
          size_t updateLhsTypeSlot = 0;
          size_t updateRhsTypeSlot = 0;
          size_t updateRhsLiteralSlot = 0;
          ZC_IF_SOME(index, updateValueTypeIndex) { updateValueTypeSlot = index; }
          ZC_IF_SOME(index, updateCallIndex) { updateCallSlot = index; }
          ZC_IF_SOME(index, updateLhsTypeIndex) { updateLhsTypeSlot = index; }
          ZC_IF_SOME(index, updateRhsTypeIndex) { updateRhsTypeSlot = index; }
          ZC_IF_SOME(index, updateRhsLiteralIndex) { updateRhsLiteralSlot = index; }
          const auto updateValueType = facts.nodeTypes().entries()[updateValueTypeSlot].value;
          const auto updateLhsType = facts.nodeTypes().entries()[updateLhsTypeSlot].value;
          const auto updateRhsType = facts.nodeTypes().entries()[updateRhsTypeSlot].value;
          const auto& updateCallFact = facts.calls().entries()[updateCallSlot].value;
          const auto& updateSelected = updateCallFact.invocation.selected.variant();
          if (!updateSelected.is<checker::checked::PrimitiveCallable>()) { return false; }
          const auto updateOperation =
              updateSelected.get<checker::checked::PrimitiveCallable>().operation;
          if (!isScalarArithmeticOperation(updateOperation) || updateLhsType != initType ||
              updateRhsType != initType || updateValueType != initType) {
            return false;
          }
          const auto& updateRhsLiteralFact = facts.literals().entries()[updateRhsLiteralSlot].value;
          if (updateRhsLiteralFact.type != updateRhsType) { return false; }
          auto loopSpan = bound.parsedModule().spanFor(tree.node(loopStmtNode).range);
          if (loopSpan == zc::none) { return false; }
          outLocal = zc::some(HirLocalBinding{HirNodeId(), hirLocalId(initLocalOrdinal), initType,
                                              zc::none, ZC_ASSERT_NONNULL(patternSpan).clone(),
                                              ZC_ASSERT_NONNULL(initializerSpan).clone()});
          outInitLiteral = zc::some(HirScalarLiteralExpression{
              HirNodeId(), initType, initLiteralFact.literal.clone(), HirValueCategory::Value,
              ZC_ASSERT_NONNULL(initializerSpan).clone()});
          outCondition = zc::some(HirPrimitiveBinaryExpression{
              HirNodeId(), HirNodeId(), HirNodeId(), condLhsType, condResultType,
              HirValueCategory::Value, condOperation, ZC_ASSERT_NONNULL(condSpan).clone()});
          outConditionLeft = zc::some(HirLocalReferenceExpression{
              HirNodeId(), hirLocalId(initLocalOrdinal), condLhsType, HirValueCategory::Place,
              ZC_ASSERT_NONNULL(condLhsSpan).clone()});
          outConditionRight = zc::some(HirScalarLiteralExpression{
              HirNodeId(), condRhsType, condRhsLiteralFact.literal.clone(), HirValueCategory::Value,
              ZC_ASSERT_NONNULL(condRhsSpan).clone()});
          outLoopSpan = zc::some(ZC_ASSERT_NONNULL(loopSpan).clone());
          outWrite = zc::some(HirLocalWriteStatement{
              HirNodeId(), hirLocalId(initLocalOrdinal), zc::none, updateValueType, HirNodeId(),
              HirLocalWriteKind::Overwrite, ZC_ASSERT_NONNULL(updateSpan).clone(),
              ZC_ASSERT_NONNULL(updateValueSpan).clone()});
          outWriteValue = zc::some(HirPrimitiveBinaryExpression{
              HirNodeId(), HirNodeId(), HirNodeId(), updateLhsType, updateValueType,
              HirValueCategory::Value, updateOperation,
              ZC_ASSERT_NONNULL(updateValueSpan).clone()});
          outWriteValueLeft = zc::some(HirLocalReferenceExpression{
              HirNodeId(), hirLocalId(initLocalOrdinal), updateLhsType, HirValueCategory::Place,
              ZC_ASSERT_NONNULL(updateLhsSpan).clone()});
          outWriteValueRight = zc::some(HirScalarLiteralExpression{
              HirNodeId(), updateRhsType, updateRhsLiteralFact.literal.clone(),
              HirValueCategory::Value, ZC_ASSERT_NONNULL(updateRhsSpan).clone()});
          return true;
        };
        // Resolve the outer loop (init local ordinal N+1) and inner loop
        // (init local ordinal N+2).
        zc::Maybe<HirLocalBinding> outerLocal;
        zc::Maybe<HirScalarLiteralExpression> outerInitLiteral;
        zc::Maybe<HirPrimitiveBinaryExpression> outerCondition;
        zc::Maybe<HirLocalReferenceExpression> outerConditionLeft;
        zc::Maybe<HirScalarLiteralExpression> outerConditionRight;
        zc::Maybe<identity::SourceSpan> outerLoopSpan;
        zc::Maybe<HirLocalWriteStatement> outerWrite;
        zc::Maybe<HirPrimitiveBinaryExpression> outerWriteValue;
        zc::Maybe<HirLocalReferenceExpression> outerWriteValueLeft;
        zc::Maybe<HirScalarLiteralExpression> outerWriteValueRight;
        zc::Maybe<HirLocalBinding> innerLocal;
        zc::Maybe<HirScalarLiteralExpression> innerInitLiteral;
        zc::Maybe<HirPrimitiveBinaryExpression> innerCondition;
        zc::Maybe<HirLocalReferenceExpression> innerConditionLeft;
        zc::Maybe<HirScalarLiteralExpression> innerConditionRight;
        zc::Maybe<identity::SourceSpan> innerLoopSpan;
        zc::Maybe<HirLocalWriteStatement> innerWrite;
        zc::Maybe<HirPrimitiveBinaryExpression> innerWriteValue;
        zc::Maybe<HirLocalReferenceExpression> innerWriteValueLeft;
        zc::Maybe<HirScalarLiteralExpression> innerWriteValueRight;
        const uint32_t outerInitOrdinal = static_cast<uint32_t>(accumulatorCount + 1);
        const uint32_t innerInitOrdinal = static_cast<uint32_t>(accumulatorCount + 2);
        bool outerOk = resolveLoopParts(shape.forLoopInit, shape.forLoopCond, shape.forLoopUpdate,
                                        shape.forLoopStatement, outerInitOrdinal, outerLocal,
                                        outerInitLiteral, outerCondition, outerConditionLeft,
                                        outerConditionRight, outerLoopSpan, outerWrite,
                                        outerWriteValue, outerWriteValueLeft, outerWriteValueRight);
        bool innerOk = false;
        if (outerOk) {
          innerOk =
              resolveLoopParts(shape.nestedForLoopInnerInit, shape.nestedForLoopInnerCond,
                               shape.nestedForLoopInnerUpdate, shape.nestedForLoopInnerStatement,
                               innerInitOrdinal, innerLocal, innerInitLiteral, innerCondition,
                               innerConditionLeft, innerConditionRight, innerLoopSpan, innerWrite,
                               innerWriteValue, innerWriteValueLeft, innerWriteValueRight);
        }
        // Resolve the N accumulator locals and their body writes (same pattern
        // as the for-loop accumulator).
        zc::Vector<PendingForLoopAccumulator> nestedAccumulators;
        bool accumulatorsOk = innerOk;
        if (innerOk) {
          for (size_t accIndex = 0; accIndex < accumulatorCount; ++accIndex) {
            const ast::NodeId accPattern = shape.forLoopAccumulatorPatterns[accIndex];
            const ast::NodeId accInitializer = shape.forLoopAccumulatorInitializers[accIndex];
            auto accBinding = ownerLocalBindingForPattern(bound.definitions(), accPattern, tree);
            auto accTypeIndex = factIndex(facts.nodeTypes(), accInitializer);
            auto accLiteralIndex = factIndex(facts.literals(), accInitializer);
            auto accPatternSpan = bound.parsedModule().spanFor(tree.node(accPattern).range);
            auto accInitializerSpan = bound.parsedModule().spanFor(tree.node(accInitializer).range);
            const ast::NodeId bodyWriteStmt = shape.forLoopBodyWrites[accIndex];
            auto bodyWriteItem = statementItem(tree, bodyWriteStmt);
            if (bodyWriteItem == zc::none) {
              accumulatorsOk = false;
              break;
            }
            ast::NodeId bodyWriteStmtNode;
            ZC_IF_SOME(item, bodyWriteItem) { bodyWriteStmtNode = item; }
            const ast::NodeId bodyWriteExpr(
                tree.node(bodyWriteStmtNode)
                    .payload.words[ast::kExpressionStatementExpressionWord]);
            const ast::NodeId bodyWriteTarget(
                tree.node(bodyWriteExpr).payload.words[ast::kAssignmentExprLhsWord]);
            const ast::NodeId bodyWriteValueNode(
                tree.node(bodyWriteExpr).payload.words[ast::kAssignmentExprRhsWord]);
            if (!tree.contains(bodyWriteExpr) ||
                tree.node(bodyWriteExpr).kind != ast::SyntaxKind::AssignmentExpr ||
                !tree.contains(bodyWriteTarget) ||
                tree.node(bodyWriteTarget).kind != ast::SyntaxKind::IdentExpr ||
                !tree.contains(bodyWriteValueNode) ||
                tree.node(bodyWriteValueNode).kind != ast::SyntaxKind::BinaryExpr) {
              accumulatorsOk = false;
              break;
            }
            const auto& bodyWriteBin = tree.node(bodyWriteValueNode);
            const ast::NodeId bodyWriteLhs(bodyWriteBin.payload.words[ast::kBinaryExprLhsWord]);
            const ast::NodeId bodyWriteRhs(bodyWriteBin.payload.words[ast::kBinaryExprRhsWord]);
            const bool rhsIsLiteral = isScalarLiteral(tree.node(bodyWriteRhs).kind);
            if (!tree.contains(bodyWriteLhs) ||
                tree.node(bodyWriteLhs).kind != ast::SyntaxKind::IdentExpr ||
                !tree.contains(bodyWriteRhs) ||
                (tree.node(bodyWriteRhs).kind != ast::SyntaxKind::IdentExpr && !rhsIsLiteral)) {
              accumulatorsOk = false;
              break;
            }
            auto bodyWriteTargetBinding = resolvedOwnerLocal(bound.bindings(), bodyWriteTarget);
            auto bodyWriteResultTypeIndex = factIndex(facts.nodeTypes(), bodyWriteValueNode);
            auto bodyWriteCallIndex = factIndex(facts.calls(), bodyWriteValueNode);
            auto bodyWriteLhsTypeIndex = factIndex(facts.nodeTypes(), bodyWriteLhs);
            auto bodyWriteRhsTypeIndex = factIndex(facts.nodeTypes(), bodyWriteRhs);
            auto bodyWriteLhsBinding = resolvedOwnerLocal(bound.bindings(), bodyWriteLhs);
            zc::Maybe<binder::OwnerLocalBindingId> bodyWriteRhsBinding;
            zc::Maybe<size_t> bodyWriteRhsLiteralIndex;
            if (!rhsIsLiteral) {
              bodyWriteRhsBinding = resolvedOwnerLocal(bound.bindings(), bodyWriteRhs);
            } else {
              bodyWriteRhsLiteralIndex = factIndex(facts.literals(), bodyWriteRhs);
            }
            auto bodyWriteSpan = bound.parsedModule().spanFor(tree.node(bodyWriteExpr).range);
            auto bodyWriteValueSpan = bound.parsedModule().spanFor(bodyWriteBin.range);
            auto bodyWriteLhsSpan = bound.parsedModule().spanFor(tree.node(bodyWriteLhs).range);
            auto bodyWriteRhsSpan = bound.parsedModule().spanFor(tree.node(bodyWriteRhs).range);
            if (accBinding == zc::none || accTypeIndex == zc::none || accLiteralIndex == zc::none ||
                accPatternSpan == zc::none || accInitializerSpan == zc::none ||
                bodyWriteTargetBinding == zc::none || bodyWriteResultTypeIndex == zc::none ||
                bodyWriteCallIndex == zc::none || bodyWriteLhsTypeIndex == zc::none ||
                bodyWriteRhsTypeIndex == zc::none || bodyWriteLhsBinding == zc::none ||
                bodyWriteSpan == zc::none || bodyWriteValueSpan == zc::none ||
                bodyWriteLhsSpan == zc::none || bodyWriteRhsSpan == zc::none ||
                (!rhsIsLiteral && bodyWriteRhsBinding == zc::none) ||
                (rhsIsLiteral && bodyWriteRhsLiteralIndex == zc::none)) {
              accumulatorsOk = false;
              break;
            }
            size_t accTypeSlot = 0;
            size_t accLiteralSlot = 0;
            size_t bodyWriteResultTypeSlot = 0;
            size_t bodyWriteCallSlot = 0;
            size_t bodyWriteLhsTypeSlot = 0;
            size_t bodyWriteRhsTypeSlot = 0;
            ZC_IF_SOME(index, accTypeIndex) { accTypeSlot = index; }
            ZC_IF_SOME(index, accLiteralIndex) { accLiteralSlot = index; }
            ZC_IF_SOME(index, bodyWriteResultTypeIndex) { bodyWriteResultTypeSlot = index; }
            ZC_IF_SOME(index, bodyWriteCallIndex) { bodyWriteCallSlot = index; }
            ZC_IF_SOME(index, bodyWriteLhsTypeIndex) { bodyWriteLhsTypeSlot = index; }
            ZC_IF_SOME(index, bodyWriteRhsTypeIndex) { bodyWriteRhsTypeSlot = index; }
            const auto accType = facts.nodeTypes().entries()[accTypeSlot].value;
            const auto& accLiteralFact = facts.literals().entries()[accLiteralSlot].value;
            const auto bodyWriteResultType =
                facts.nodeTypes().entries()[bodyWriteResultTypeSlot].value;
            const auto bodyWriteLhsType = facts.nodeTypes().entries()[bodyWriteLhsTypeSlot].value;
            const auto bodyWriteRhsType = facts.nodeTypes().entries()[bodyWriteRhsTypeSlot].value;
            const auto& bodyWriteCallFact = facts.calls().entries()[bodyWriteCallSlot].value;
            const auto& bodyWriteSelected = bodyWriteCallFact.invocation.selected.variant();
            if (!bodyWriteSelected.is<checker::checked::PrimitiveCallable>()) {
              accumulatorsOk = false;
              break;
            }
            const auto bodyWriteOperation =
                bodyWriteSelected.get<checker::checked::PrimitiveCallable>().operation;
            if (!isScalarArithmeticOperation(bodyWriteOperation) || bodyWriteLhsType != accType ||
                bodyWriteRhsType != accType || bodyWriteResultType != accType ||
                accLiteralFact.type != accType) {
              accumulatorsOk = false;
              break;
            }
            if (rhsIsLiteral) {
              size_t bodyWriteRhsLiteralSlot = 0;
              ZC_IF_SOME(index, bodyWriteRhsLiteralIndex) { bodyWriteRhsLiteralSlot = index; }
              const auto& bodyWriteRhsLiteralFact =
                  facts.literals().entries()[bodyWriteRhsLiteralSlot].value;
              if (bodyWriteRhsLiteralFact.type != bodyWriteRhsType) {
                accumulatorsOk = false;
                break;
              }
            }
            // The body write right operand must reference the inner loop init
            // local (not the outer loop init local).
            if (!rhsIsLiteral) {
              auto innerInitBinding = ownerLocalBindingForPattern(
                  bound.definitions(),
                  [&]() -> ast::NodeId {
                    const auto& innerLetNode = tree.node(shape.nestedForLoopInnerInit);
                    const ast::NodeId innerDeclarations(
                        innerLetNode.payload.words[ast::kLetStmtDeclarationsWord]);
                    const ast::NodeList innerDeclarators{
                        tree.node(innerDeclarations)
                            .payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
                        tree.node(innerDeclarations)
                            .payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
                    return tree.list(innerDeclarators)[0];
                  }(),
                  tree);
              if (innerInitBinding == zc::none ||
                  ZC_ASSERT_NONNULL(bodyWriteRhsBinding) != ZC_ASSERT_NONNULL(innerInitBinding)) {
                accumulatorsOk = false;
                break;
              }
            }
            HirLocalReferenceExpression bodyWriteLeft{
                HirNodeId(), hirLocalId(static_cast<uint32_t>(accIndex + 1)), bodyWriteLhsType,
                HirValueCategory::Place, ZC_ASSERT_NONNULL(bodyWriteLhsSpan).clone()};
            zc::Maybe<HirLocalReferenceExpression> bodyWriteRight;
            zc::Maybe<HirScalarLiteralExpression> bodyWriteRightLiteral;
            if (rhsIsLiteral) {
              size_t bodyWriteRhsLiteralSlot = 0;
              ZC_IF_SOME(index, bodyWriteRhsLiteralIndex) { bodyWriteRhsLiteralSlot = index; }
              const auto& bodyWriteRhsLiteralFact =
                  facts.literals().entries()[bodyWriteRhsLiteralSlot].value;
              bodyWriteRightLiteral = zc::some(HirScalarLiteralExpression{
                  HirNodeId(), bodyWriteRhsType, bodyWriteRhsLiteralFact.literal.clone(),
                  HirValueCategory::Value, ZC_ASSERT_NONNULL(bodyWriteRhsSpan).clone()});
            } else {
              bodyWriteRight = zc::some(HirLocalReferenceExpression{
                  HirNodeId(), hirLocalId(innerInitOrdinal), bodyWriteRhsType,
                  HirValueCategory::Place, ZC_ASSERT_NONNULL(bodyWriteRhsSpan).clone()});
            }
            nestedAccumulators.add(PendingForLoopAccumulator{
                HirLocalBinding{HirNodeId(), hirLocalId(static_cast<uint32_t>(accIndex + 1)),
                                accType, zc::none, ZC_ASSERT_NONNULL(accPatternSpan).clone(),
                                ZC_ASSERT_NONNULL(accInitializerSpan).clone()},
                HirScalarLiteralExpression{HirNodeId(), accType, accLiteralFact.literal.clone(),
                                           HirValueCategory::Value,
                                           ZC_ASSERT_NONNULL(accInitializerSpan).clone()},
                HirLocalWriteStatement{HirNodeId(), hirLocalId(static_cast<uint32_t>(accIndex + 1)),
                                       zc::none, bodyWriteResultType, HirNodeId(),
                                       HirLocalWriteKind::Overwrite,
                                       ZC_ASSERT_NONNULL(bodyWriteSpan).clone(),
                                       ZC_ASSERT_NONNULL(bodyWriteValueSpan).clone()},
                HirPrimitiveBinaryExpression{HirNodeId(), HirNodeId(), HirNodeId(),
                                             bodyWriteLhsType, bodyWriteResultType,
                                             HirValueCategory::Value, bodyWriteOperation,
                                             ZC_ASSERT_NONNULL(bodyWriteValueSpan).clone()},
                zc::mv(bodyWriteLeft), rhsIsLiteral, zc::mv(bodyWriteRight),
                zc::mv(bodyWriteRightLiteral), accPattern, bodyWriteExpr});
          }
        }
        if (accumulatorsOk) {
          auto returnSpan = bound.parsedModule().spanFor(tree.node(shape.returnStatement).range);
          auto returnTypeIndex = factIndex(facts.nodeTypes(), shape.value);
          if (returnSpan == zc::none || returnTypeIndex == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t returnTypeSlot = 0;
          ZC_IF_SOME(index, returnTypeIndex) { returnTypeSlot = index; }
          const auto returnType = facts.nodeTypes().entries()[returnTypeSlot].value;
          // Resolve the inner loop's trailing break/continue spans and
          // determine whether a labeled break/continue targets the outer
          // loop. The binder has already validated the label; in the
          // two-level nested accumulator slice, a labeled break/continue
          // in the inner body can only target the outer loop.
          zc::Maybe<identity::SourceSpan> innerBreakSpan;
          zc::Maybe<identity::SourceSpan> innerContinueSpan;
          bool innerBreakTargetsEnclosing = false;
          bool innerContinueTargetsEnclosing = false;
          if (shape.nestedForLoopInnerBreak) {
            innerBreakSpan =
                bound.parsedModule().spanFor(tree.node(shape.nestedForLoopInnerBreak).range);
            const auto labelWord =
                tree.node(shape.nestedForLoopInnerBreak).payload.words[ast::kBreakStmtLabelWord];
            if (labelWord != 0) { innerBreakTargetsEnclosing = true; }
          }
          if (shape.nestedForLoopInnerContinue) {
            innerContinueSpan =
                bound.parsedModule().spanFor(tree.node(shape.nestedForLoopInnerContinue).range);
            const auto labelWord = tree.node(shape.nestedForLoopInnerContinue)
                                       .payload.words[ast::kContinueStatementLabelWord];
            if (labelWord != 0) { innerContinueTargetsEnclosing = true; }
          }
          nestedForLoopAccumulatorReturn = PendingNestedForLoopAccumulatorReturn{
              zc::mv(ZC_ASSERT_NONNULL(outerCondition)),
              zc::mv(ZC_ASSERT_NONNULL(outerConditionLeft)),
              zc::mv(ZC_ASSERT_NONNULL(outerConditionRight)),
              zc::mv(ZC_ASSERT_NONNULL(outerLoopSpan)),
              zc::mv(ZC_ASSERT_NONNULL(outerLocal)),
              zc::mv(ZC_ASSERT_NONNULL(outerInitLiteral)),
              zc::mv(ZC_ASSERT_NONNULL(outerWrite)),
              zc::mv(ZC_ASSERT_NONNULL(outerWriteValue)),
              zc::mv(ZC_ASSERT_NONNULL(outerWriteValueLeft)),
              zc::mv(ZC_ASSERT_NONNULL(outerWriteValueRight)),
              zc::mv(ZC_ASSERT_NONNULL(innerCondition)),
              zc::mv(ZC_ASSERT_NONNULL(innerConditionLeft)),
              zc::mv(ZC_ASSERT_NONNULL(innerConditionRight)),
              zc::mv(ZC_ASSERT_NONNULL(innerLoopSpan)),
              zc::mv(ZC_ASSERT_NONNULL(innerLocal)),
              zc::mv(ZC_ASSERT_NONNULL(innerInitLiteral)),
              zc::mv(ZC_ASSERT_NONNULL(innerWrite)),
              zc::mv(ZC_ASSERT_NONNULL(innerWriteValue)),
              zc::mv(ZC_ASSERT_NONNULL(innerWriteValueLeft)),
              zc::mv(ZC_ASSERT_NONNULL(innerWriteValueRight)),
              zc::mv(nestedAccumulators),
              HirLocalReferenceExpression{HirNodeId(), hirLocalId(1), returnType,
                                          HirValueCategory::Place,
                                          ZC_ASSERT_NONNULL(returnSpan).clone()},
              shape.forLoopStatement,
              shape.nestedForLoopInnerStatement,
              shape.returnStatement,
              zc::mv(innerBreakSpan),
              zc::mv(innerContinueSpan),
              innerBreakTargetsEnclosing,
              innerContinueTargetsEnclosing};
        }
      }
      // The function's semantic result type is the success type.  The `?!`
      // operator unwraps the call's error union to the success type, so the
      // enclosing function's result type matches the success type, not the
      // call's error-union result type.
      const identity::SemanticTypeId functionResultType = callable.success;
      // Classify the body shape into a tagged-union variant. The lambda
      // mirrors the dispatch guard order below; each branch moves only the
      // fields its variant carries. Fields always zc::none on this path
      // (conditionalReturn, loopReturn, sequentialLocalReturn, etc.) are
      // skipped — their variants are produced by specialized construction
      // sites earlier in the builder. Negative field-clear conjunctions are
      // compiler-generated via allNone/allEmpty, never hand-written.
      auto selectShape = [&]() -> PendingFunctionShape {
        // Shared carrier-clear predicates, mirroring the guard chain.
        const bool callFieldsClear =
            allEmpty(localWrites, localWriteValues) &&
            allNone(localFieldProjection, parameterIndex, parameterReborrow, localBorrow,
                    statementReceiverCall, statementReceiverReference, comparisonReturn,
                    loopBodyReturn, comparisonLoopBodyReturn, unsafeBlockSpan);
        const bool borrowCarriersClear =
            allNone(call, receiverCall, aggregate, localReference, localFieldProjection,
                    parameterIndex, comparisonReturn, loopBodyReturn, comparisonLoopBodyReturn) &&
            allEmpty(localWrites, localWriteValues);
        // By-value struct parameter field return.
        if (byValueParameterField && parameterFieldProjection != zc::none &&
            allNone(methodReceiver, parameterFieldWrite, statementReceiverCall,
                    statementReceiverReference, literal, call, receiverCall, local, aggregate,
                    localReference, localFieldProjection, parameterReference, parameterIndex,
                    parameterReborrow, localBorrow, comparisonReturn, loopBodyReturn,
                    comparisonLoopBodyReturn, receiverFieldArithmetic, receiverSelfCall,
                    unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues) && !voidBody) {
          return ByValueParameterFieldReturn{zc::mv(ZC_ASSERT_NONNULL(parameterFieldProjection))};
        }
        // Receiver field write return.
        if (parameterFieldProjection != zc::none && methodReceiver != zc::none &&
            parameterFieldWrite != zc::none && parameterFieldWriteLiteral != zc::none &&
            allNone(literal, call, receiverCall, local, aggregate, localReference,
                    localFieldProjection, parameterReference, statementReceiverCall,
                    statementReceiverReference, parameterIndex, parameterReborrow, localBorrow,
                    comparisonReturn, loopBodyReturn, comparisonLoopBodyReturn, unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues) && !voidBody) {
          return ReceiverFieldWriteReturn{zc::mv(ZC_ASSERT_NONNULL(parameterFieldProjection)),
                                          zc::mv(ZC_ASSERT_NONNULL(parameterFieldWrite)),
                                          zc::mv(ZC_ASSERT_NONNULL(parameterFieldWriteLiteral))};
        }
        // Receiver field return.
        if (parameterFieldProjection != zc::none && methodReceiver != zc::none &&
            parameterFieldWrite == zc::none &&
            allNone(statementReceiverCall, statementReceiverReference, literal, call, receiverCall,
                    local, aggregate, localReference, localFieldProjection, parameterReference,
                    parameterIndex, parameterReborrow, localBorrow, comparisonReturn,
                    loopBodyReturn, comparisonLoopBodyReturn, unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues) && !voidBody) {
          return ReceiverFieldReturn{zc::mv(ZC_ASSERT_NONNULL(parameterFieldProjection))};
        }
        // Empty-body void function.
        if (voidBody &&
            allNone(parameterFieldWrite, methodReceiver, parameterFieldProjection,
                    parameterFieldWriteLiteral, literal, call, receiverCall, receiverSelfCall,
                    local, aggregate, localReference, localFieldProjection, parameterReference,
                    statementReceiverCall, statementReceiverReference, parameterIndex,
                    parameterReborrow, localBorrow, comparisonReturn, loopBodyReturn,
                    comparisonLoopBodyReturn, receiverFieldArithmetic, unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues)) {
          return EmptyVoidBody{};
        }
        // Scalar leaf family: bare literal/parameter return, single
        // initialized local return, or array index/length fold return.
        const bool hasScalarLeaf = (literal != zc::none) != (parameterReference != zc::none);
        const bool onlyScalarReturn =
            allNone(local, call, receiverCall, aggregate, localReference, localFieldProjection,
                    parameterIndex, parameterReborrow, localBorrow, comparisonReturn,
                    loopBodyReturn, comparisonLoopBodyReturn, forLoopReturn, unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues);
        const bool singleInitializedLocal =
            local != zc::none && ZC_ASSERT_NONNULL(local).initializer != zc::none &&
            localReference != zc::none &&
            allNone(call, receiverCall, aggregate, localFieldProjection, parameterIndex,
                    parameterReborrow, localBorrow, comparisonReturn, loopBodyReturn,
                    comparisonLoopBodyReturn, unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues);
        const bool arrayIndexFoldLocal =
            local != zc::none && ZC_ASSERT_NONNULL(local).initializer != zc::none &&
            (returnsArrayIndexFold || returnsArrayLengthFold) &&
            allNone(call, receiverCall, aggregate, localFieldProjection, parameterIndex,
                    parameterReborrow, localBorrow, comparisonReturn, loopBodyReturn,
                    comparisonLoopBodyReturn, unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues);
        if (hasScalarLeaf && onlyScalarReturn) {
          if (literal != zc::none) {
            return ScalarLiteralReturn{zc::mv(ZC_ASSERT_NONNULL(literal))};
          }
          return ParameterReferenceReturn{zc::mv(ZC_ASSERT_NONNULL(parameterReference))};
        }
        if (hasScalarLeaf && singleInitializedLocal) {
          return LocalReturn{zc::mv(ZC_ASSERT_NONNULL(local)), zc::mv(literal),
                             zc::mv(parameterReference), zc::mv(ZC_ASSERT_NONNULL(localReference))};
        }
        if (hasScalarLeaf && arrayIndexFoldLocal) {
          if (returnsArrayIndexFold) {
            return ArrayIndexFoldReturn{zc::mv(ZC_ASSERT_NONNULL(literal))};
          }
          return ArrayLengthFoldReturn{zc::mv(ZC_ASSERT_NONNULL(literal))};
        }
        // Dyn field-access return.
        if (dynFieldAccessReturn != zc::none) {
          return DynFieldAccessReturn{zc::mv(ZC_ASSERT_NONNULL(dynFieldAccessReturn))};
        }
        // Comparison return (bare binary, no local).
        if (comparisonReturn != zc::none &&
            allNone(literal, call, receiverCall, aggregate, local, localReference,
                    localFieldProjection, parameterReference, parameterIndex, parameterReborrow,
                    localBorrow, loopBodyReturn, comparisonLoopBodyReturn, unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues)) {
          return ComparisonReturn{zc::mv(ZC_ASSERT_NONNULL(comparisonReturn))};
        }
        // Primitive unary return (comparison + local + literal).
        if (comparisonReturn != zc::none && local != zc::none && localReference == zc::none &&
            literal != zc::none &&
            allNone(call, receiverCall, aggregate, localFieldProjection, parameterReference,
                    parameterIndex, parameterReborrow, localBorrow, loopBodyReturn,
                    comparisonLoopBodyReturn, unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues)) {
          return PrimitiveUnaryReturn{zc::mv(ZC_ASSERT_NONNULL(comparisonReturn)),
                                      zc::mv(ZC_ASSERT_NONNULL(local)), zc::mv(literal)};
        }
        // Receiver field arithmetic return.
        if (receiverFieldArithmetic != zc::none &&
            allNone(literal, call, receiverCall, receiverSelfCall, local, aggregate, localReference,
                    localFieldProjection, parameterFieldProjection, parameterReference,
                    parameterIndex, parameterReborrow, localBorrow, comparisonReturn,
                    loopBodyReturn, comparisonLoopBodyReturn, parameterFieldWrite,
                    parameterFieldWriteLiteral, unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues)) {
          return ReceiverFieldArithmeticReturn{zc::mv(ZC_ASSERT_NONNULL(receiverFieldArithmetic))};
        }
        // Enum construction return.
        if (aggregate != zc::none && aggregateIsEnumConstruction && local == zc::none &&
            allNone(call, receiverCall, literal, parameterReference, localReference,
                    localFieldProjection, parameterFieldProjection, parameterIndex,
                    parameterReborrow, localBorrow, comparisonReturn, loopBodyReturn,
                    comparisonLoopBodyReturn, forLoopReturn, forLoopAccumulatorReturn,
                    nestedForLoopAccumulatorReturn, forInLoopAccumulatorReturn, parameterFieldWrite,
                    parameterFieldWriteLiteral, receiverSelfCall, receiverFieldArithmetic,
                    statementReceiverCall, statementReceiverReference, unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues) && !voidBody && !byValueParameterField) {
          return EnumConstructionReturn{zc::mv(ZC_ASSERT_NONNULL(aggregate))};
        }
        // Direct aggregate construction return (non-existential, non-enum).
        if (aggregate != zc::none && !aggregateIsEnumConstruction && returnErasure == zc::none &&
            local == zc::none &&
            allNone(call, receiverCall, literal, parameterReference, localReference,
                    localFieldProjection, parameterFieldProjection, parameterIndex,
                    parameterReborrow, localBorrow, comparisonReturn, loopBodyReturn,
                    comparisonLoopBodyReturn, forLoopReturn, forLoopAccumulatorReturn,
                    nestedForLoopAccumulatorReturn, forInLoopAccumulatorReturn, parameterFieldWrite,
                    parameterFieldWriteLiteral, receiverSelfCall, receiverFieldArithmetic,
                    statementReceiverCall, statementReceiverReference, unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues) && !voidBody && !byValueParameterField) {
          return AggregateReturn{zc::mv(ZC_ASSERT_NONNULL(aggregate))};
        }
        // Existential aggregate construction return.
        if (aggregate != zc::none && returnErasure != zc::none && !aggregateIsEnumConstruction &&
            local == zc::none &&
            allNone(call, receiverCall, literal, parameterReference, localReference,
                    localFieldProjection, parameterFieldProjection, parameterIndex,
                    parameterReborrow, localBorrow, comparisonReturn, loopBodyReturn,
                    comparisonLoopBodyReturn, forLoopReturn, forLoopAccumulatorReturn,
                    nestedForLoopAccumulatorReturn, forInLoopAccumulatorReturn, parameterFieldWrite,
                    parameterFieldWriteLiteral, receiverSelfCall, receiverFieldArithmetic,
                    statementReceiverCall, statementReceiverReference, unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues) && !voidBody && !byValueParameterField) {
          return ExistentialReturn{zc::mv(ZC_ASSERT_NONNULL(aggregate)),
                                   zc::mv(ZC_ASSERT_NONNULL(returnErasure))};
        }
        // Aggregate field projection return.
        if (local != zc::none && ZC_ASSERT_NONNULL(local).initializer != zc::none &&
            aggregate != zc::none && localFieldProjection != zc::none &&
            localReference != zc::none &&
            allNone(call, receiverCall, literal, parameterReference, parameterIndex,
                    parameterReborrow, localBorrow, comparisonReturn, loopBodyReturn,
                    comparisonLoopBodyReturn, unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues)) {
          return AggregateFieldProjectionReturn{zc::mv(ZC_ASSERT_NONNULL(aggregate)),
                                                zc::mv(ZC_ASSERT_NONNULL(local)),
                                                zc::mv(ZC_ASSERT_NONNULL(localFieldProjection)),
                                                zc::mv(ZC_ASSERT_NONNULL(localReference))};
        }
        // Local write return.
        if (local != zc::none && ZC_ASSERT_NONNULL(local).initializer != zc::none &&
            localWrites.size() != 0 && localWrites.size() == localWriteValues.size() &&
            localReference != zc::none &&
            allNone(call, receiverCall, aggregate, localFieldProjection, parameterIndex,
                    parameterReborrow, localBorrow, comparisonReturn, loopBodyReturn,
                    comparisonLoopBodyReturn, unsafeBlockSpan)) {
          bool writesArePlain = true;
          for (const auto& write : localWrites) {
            if (write.field != zc::none) writesArePlain = false;
          }
          // The local-write lowering carrier only builds literal-initialized
          // storage; a parameter-initialized local with writes is rejected as
          // ZOM4098 by the GenericShape catch-all below.
          const bool initializedByLiteral = literal != zc::none && parameterReference == zc::none;
          if (writesArePlain && initializedByLiteral) {
            return LocalWriteReturn{zc::mv(ZC_ASSERT_NONNULL(local)),
                                    zc::mv(ZC_ASSERT_NONNULL(literal)), zc::mv(localWrites),
                                    zc::mv(localWriteValues),
                                    zc::mv(ZC_ASSERT_NONNULL(localReference))};
          }
        }
        // Local field projection return (aggregate field-write family).
        if (local != zc::none && localWrites.size() != 0 &&
            localWrites.size() == localWriteValues.size() && localFieldProjection != zc::none &&
            localReference == zc::none &&
            allNone(call, receiverCall, parameterIndex, parameterReborrow, localBorrow,
                    comparisonReturn, loopBodyReturn, comparisonLoopBodyReturn, unsafeBlockSpan)) {
          const bool initializedByAggregate = ZC_ASSERT_NONNULL(local).initializer != zc::none &&
                                              aggregate != zc::none && literal == zc::none &&
                                              parameterReference == zc::none;
          const bool uninitializedLocal = ZC_ASSERT_NONNULL(local).initializer == zc::none &&
                                          aggregate == zc::none && literal == zc::none &&
                                          parameterReference == zc::none;
          bool writesAreLiteralFields = initializedByAggregate || uninitializedLocal;
          if (writesAreLiteralFields) {
            for (size_t index = 0; index < localWrites.size(); ++index) {
              const auto& write = localWrites[index];
              const auto& writeValue = localWriteValues[index];
              if (write.field == zc::none || writeValue.literal == zc::none ||
                  writeValue.parameter != zc::none || writeValue.binary != zc::none) {
                writesAreLiteralFields = false;
                break;
              }
            }
          }
          if (writesAreLiteralFields) {
            return LocalFieldProjectionReturn{zc::mv(ZC_ASSERT_NONNULL(local)), zc::mv(aggregate),
                                              zc::mv(localWrites), zc::mv(localWriteValues),
                                              zc::mv(ZC_ASSERT_NONNULL(localFieldProjection))};
          }
        }
        // Direct call family.
        if (call != zc::none && literal != zc::none && local != zc::none &&
            ZC_ASSERT_NONNULL(local).initializer != zc::none && localReference == zc::none &&
            aggregate == zc::none && allNone(receiverCall, receiverSelfCall, parameterReference) &&
            callFieldsClear) {
          return DirectScalarLocalCallReturn{zc::mv(ZC_ASSERT_NONNULL(call)),
                                             zc::mv(ZC_ASSERT_NONNULL(literal)),
                                             zc::mv(ZC_ASSERT_NONNULL(local))};
        }
        if (call != zc::none && aggregate != zc::none && local != zc::none &&
            ZC_ASSERT_NONNULL(local).initializer != zc::none && localReference == zc::none &&
            receiverCall == zc::none && receiverSelfCall == zc::none && literal == zc::none &&
            parameterReference == zc::none && callFieldsClear) {
          return DirectAggregateCallReturn{zc::mv(ZC_ASSERT_NONNULL(call)),
                                           zc::mv(ZC_ASSERT_NONNULL(aggregate)),
                                           zc::mv(ZC_ASSERT_NONNULL(local))};
        }
        if (call != zc::none && initializerCall != zc::none && local != zc::none &&
            ZC_ASSERT_NONNULL(local).initializer != zc::none && localReference == zc::none &&
            aggregate == zc::none &&
            allNone(receiverCall, receiverSelfCall, literal, parameterReference) &&
            callFieldsClear) {
          return DirectExistentialCallReturn{zc::mv(ZC_ASSERT_NONNULL(call)),
                                             zc::mv(ZC_ASSERT_NONNULL(initializerCall)),
                                             zc::mv(ZC_ASSERT_NONNULL(local))};
        }
        if (call != zc::none && local == zc::none &&
            allNone(receiverCall, aggregate, localReference, literal, parameterReference) &&
            callFieldsClear) {
          if (shape.returnsErrorPropagateCall) {
            return ErrorPropagateCallReturn{zc::mv(ZC_ASSERT_NONNULL(call))};
          }
          if (shape.returnsErrorUnwrapCall) {
            return ErrorUnwrapCallReturn{zc::mv(ZC_ASSERT_NONNULL(call)), zc::none, zc::none};
          }
          return DirectCallReturn{zc::mv(ZC_ASSERT_NONNULL(call))};
        }
        if (call != zc::none && local != zc::none &&
            ZC_ASSERT_NONNULL(local).initializer != zc::none && localReference != zc::none &&
            allNone(receiverCall, aggregate, literal, parameterReference) && callFieldsClear) {
          return DirectCallInitializerReturn{zc::mv(ZC_ASSERT_NONNULL(call)),
                                             zc::mv(ZC_ASSERT_NONNULL(local)),
                                             zc::mv(ZC_ASSERT_NONNULL(localReference))};
        }
        // Discarded receiver call + trailing receiver call return.
        if (statementReceiverCall != zc::none && statementReceiverReference != zc::none &&
            receiverCall != zc::none && local != zc::none &&
            ZC_ASSERT_NONNULL(local).initializer != zc::none && aggregate != zc::none &&
            localReference != zc::none &&
            allNone(call, literal, parameterReference, parameterFieldWrite) &&
            allEmpty(localWrites, localWriteValues) &&
            allNone(localFieldProjection, parameterIndex, parameterReborrow, localBorrow,
                    comparisonReturn, loopBodyReturn, comparisonLoopBodyReturn, unsafeBlockSpan) &&
            !voidBody) {
          return DiscardedReceiverCall{zc::mv(ZC_ASSERT_NONNULL(statementReceiverCall)),
                                       zc::mv(ZC_ASSERT_NONNULL(statementReceiverReference)),
                                       zc::mv(ZC_ASSERT_NONNULL(receiverCall)),
                                       zc::mv(ZC_ASSERT_NONNULL(local)),
                                       zc::mv(ZC_ASSERT_NONNULL(aggregate)),
                                       zc::mv(ZC_ASSERT_NONNULL(localReference))};
        }
        // Receiver call return.
        if (receiverCall != zc::none && local != zc::none &&
            ZC_ASSERT_NONNULL(local).initializer != zc::none && localReference != zc::none &&
            (aggregate != zc::none) != (literal != zc::none) &&
            allNone(call, parameterReference, statementReceiverCall, statementReceiverReference) &&
            callFieldsClear) {
          return ReceiverCallReturn{zc::mv(ZC_ASSERT_NONNULL(receiverCall)),
                                    zc::mv(ZC_ASSERT_NONNULL(local)), zc::mv(aggregate),
                                    zc::mv(literal), zc::mv(ZC_ASSERT_NONNULL(localReference))};
        }
        // Receiver self call return.
        if (receiverSelfCall != zc::none && methodReceiver != zc::none &&
            allNone(local, localReference, aggregate, call, receiverCall, literal,
                    parameterReference, parameterFieldProjection, parameterFieldWrite,
                    parameterFieldWriteLiteral) &&
            callFieldsClear) {
          return ReceiverSelfCallReturn{zc::mv(ZC_ASSERT_NONNULL(receiverSelfCall))};
        }
        // Parameter receiver call return.
        if (receiverCall != zc::none && receiverCallParameterKey != zc::none &&
            receiverCallParameterSpan != zc::none &&
            ZC_ASSERT_NONNULL(receiverCall).interface != zc::none &&
            allNone(local, localReference, aggregate, literal, call, methodReceiver,
                    receiverSelfCall, parameterReference) &&
            callFieldsClear) {
          return ParameterReceiverCallReturn{zc::mv(ZC_ASSERT_NONNULL(receiverCall)),
                                             zc::mv(ZC_ASSERT_NONNULL(receiverCallParameterKey)),
                                             zc::mv(ZC_ASSERT_NONNULL(receiverCallParameterSpan))};
        }
        // Loop body return.
        if (loopBodyReturn != zc::none && local != zc::none &&
            ZC_ASSERT_NONNULL(local).initializer != zc::none && localWrites.size() != 0 &&
            localWrites.size() == localWriteValues.size() && localReference != zc::none &&
            allNone(call, receiverCall, aggregate, localFieldProjection, parameterIndex,
                    parameterReborrow, localBorrow, comparisonReturn, comparisonLoopBodyReturn,
                    unsafeBlockSpan)) {
          bool loopWritesArePlain = true;
          for (const auto& write : localWrites) {
            if (write.field != zc::none) loopWritesArePlain = false;
          }
          const bool loopInitializedByLeaf =
              (literal != zc::none) != (parameterReference != zc::none);
          if (loopWritesArePlain && loopInitializedByLeaf) {
            return LoopBodyReturn{zc::mv(ZC_ASSERT_NONNULL(loopBodyReturn)),
                                  zc::mv(ZC_ASSERT_NONNULL(local)),
                                  zc::mv(literal),
                                  zc::mv(parameterReference),
                                  zc::mv(localWrites),
                                  zc::mv(localWriteValues),
                                  zc::mv(ZC_ASSERT_NONNULL(localReference))};
          }
        }
        // Comparison loop body return.
        if (comparisonLoopBodyReturn != zc::none && local != zc::none &&
            ZC_ASSERT_NONNULL(local).initializer != zc::none && localWrites.size() != 0 &&
            localWrites.size() == localWriteValues.size() && localReference != zc::none &&
            allNone(call, receiverCall, aggregate, localFieldProjection, parameterIndex,
                    parameterReborrow, localBorrow, comparisonReturn, loopBodyReturn,
                    unsafeBlockSpan)) {
          bool loopWritesArePlain = true;
          for (const auto& write : localWrites) {
            if (write.field != zc::none) loopWritesArePlain = false;
          }
          const bool loopInitializedByLeaf =
              (literal != zc::none) != (parameterReference != zc::none);
          if (loopWritesArePlain && loopInitializedByLeaf) {
            return ComparisonLoopBodyReturn{zc::mv(ZC_ASSERT_NONNULL(comparisonLoopBodyReturn)),
                                            zc::mv(ZC_ASSERT_NONNULL(local)),
                                            zc::mv(literal),
                                            zc::mv(parameterReference),
                                            zc::mv(localWrites),
                                            zc::mv(localWriteValues),
                                            zc::mv(ZC_ASSERT_NONNULL(localReference))};
          }
        }
        // For-loop return.
        if (forLoopReturn != zc::none &&
            allNone(local, call, receiverCall, aggregate, localReference, localFieldProjection,
                    parameterFieldProjection, parameterReference, parameterIndex, parameterReborrow,
                    localBorrow, comparisonReturn, loopBodyReturn, comparisonLoopBodyReturn,
                    unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues)) {
          return ForLoopReturn{zc::mv(ZC_ASSERT_NONNULL(forLoopReturn)),
                               zc::mv(ZC_ASSERT_NONNULL(literal))};
        }
        // For-loop accumulator return.
        if (forLoopAccumulatorReturn != zc::none &&
            allNone(local, call, receiverCall, aggregate, localReference, localFieldProjection,
                    parameterFieldProjection, parameterReference, parameterIndex, parameterReborrow,
                    localBorrow, comparisonReturn, loopBodyReturn, comparisonLoopBodyReturn,
                    forLoopReturn, forInLoopAccumulatorReturn, unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues)) {
          return ForLoopAccumulatorReturn{zc::mv(ZC_ASSERT_NONNULL(forLoopAccumulatorReturn))};
        }
        // Nested for-loop accumulator return.
        if (nestedForLoopAccumulatorReturn != zc::none &&
            allNone(local, call, receiverCall, aggregate, localReference, localFieldProjection,
                    parameterFieldProjection, parameterReference, parameterIndex, parameterReborrow,
                    localBorrow, comparisonReturn, loopBodyReturn, comparisonLoopBodyReturn,
                    forLoopReturn, forLoopAccumulatorReturn, forInLoopAccumulatorReturn,
                    unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues)) {
          return NestedForLoopAccumulatorReturn{
              zc::mv(ZC_ASSERT_NONNULL(nestedForLoopAccumulatorReturn))};
        }
        // For-in loop accumulator return.
        if (forInLoopAccumulatorReturn != zc::none &&
            allNone(local, call, receiverCall, aggregate, localReference, localFieldProjection,
                    parameterFieldProjection, parameterReference, parameterIndex, parameterReborrow,
                    localBorrow, comparisonReturn, loopBodyReturn, comparisonLoopBodyReturn,
                    forLoopReturn, forLoopAccumulatorReturn, nestedForLoopAccumulatorReturn,
                    unsafeBlockSpan) &&
            allEmpty(localWrites, localWriteValues)) {
          return ForInLoopAccumulatorReturn{zc::mv(ZC_ASSERT_NONNULL(forInLoopAccumulatorReturn))};
        }
        // Bare parameter reborrow return.
        if (parameterReborrow != zc::none &&
            allNone(local, literal, parameterReference, localBorrow) && borrowCarriersClear) {
          return ParameterReborrowReturn{zc::mv(ZC_ASSERT_NONNULL(parameterReborrow))};
        }
        // Unsafe-block-wrapped scalar literal return.
        if (literal != zc::none && unsafeBlockSpan != zc::none &&
            allNone(local, parameterReference, parameterReborrow, localBorrow) &&
            borrowCarriersClear) {
          return UnsafeScalarReturn{zc::mv(ZC_ASSERT_NONNULL(literal))};
        }
        // Raw-pointer write-read return.
        if (rawDereferenceWrite != zc::none && rawDereferenceSource != zc::none &&
            unsafeBlockSpan != zc::none && writeUnsafeBlockSpan != zc::none &&
            rawDereferenceWriteLiteral != zc::none &&
            allNone(literal, local, parameterReference, parameterReborrow, localBorrow) &&
            borrowCarriersClear) {
          return RawPointerWriteReturn{zc::mv(ZC_ASSERT_NONNULL(rawDereferenceWrite)),
                                       zc::mv(ZC_ASSERT_NONNULL(writeUnsafeBlockSpan)),
                                       zc::mv(ZC_ASSERT_NONNULL(rawDereferenceWriteLiteral)),
                                       zc::mv(ZC_ASSERT_NONNULL(rawDereferenceSource))};
        }
        // Raw-pointer dereference return.
        if (rawDereferenceSource != zc::none && unsafeBlockSpan != zc::none &&
            allNone(literal, local, parameterReference, parameterReborrow, localBorrow) &&
            borrowCarriersClear) {
          return RawDereferenceReturn{zc::mv(ZC_ASSERT_NONNULL(rawDereferenceSource))};
        }
        // Local-alias reborrow return.
        if (local != zc::none && ZC_ASSERT_NONNULL(local).initializer != zc::none &&
            parameterReference != zc::none && parameterReborrow != zc::none &&
            literal == zc::none && localBorrow == zc::none && borrowCarriersClear) {
          return LocalAliasReborrowReturn{zc::mv(ZC_ASSERT_NONNULL(local)),
                                          zc::mv(ZC_ASSERT_NONNULL(parameterReference)),
                                          zc::mv(ZC_ASSERT_NONNULL(parameterReborrow))};
        }
        // Local borrow return.
        if (local != zc::none && ZC_ASSERT_NONNULL(local).initializer != zc::none &&
            localBorrow != zc::none && parameterReborrow == zc::none &&
            allNone(call, receiverCall, aggregate, localReference, localFieldProjection,
                    parameterIndex, comparisonReturn, loopBodyReturn, comparisonLoopBodyReturn) &&
            allEmpty(localWrites, localWriteValues) &&
            ((literal != zc::none) != (parameterReference != zc::none))) {
          return LocalBorrowReturn{zc::mv(ZC_ASSERT_NONNULL(local)), zc::mv(literal),
                                   zc::mv(parameterReference),
                                   zc::mv(ZC_ASSERT_NONNULL(localBorrow))};
        }
        // Catch-all: generic materializer shape.
        return GenericShape{zc::mv(literal),
                            zc::mv(local),
                            zc::mv(aggregate),
                            zc::mv(localWrites),
                            zc::mv(localWriteValues),
                            zc::mv(localReference),
                            zc::mv(localFieldProjection),
                            zc::mv(parameterReference),
                            zc::mv(parameterIndex),
                            zc::mv(parameterReborrow),
                            zc::mv(localBorrow),
                            zc::mv(comparisonReturn),
                            shape.returnsErrorUnwrapCall};
      };
      // selectShape() reads methodReceiver, so it must run before the
      // PendingFunctionDeclaration constructor moves it. C++ argument
      // evaluation order is unspecified, so calling selectShape() inside the
      // constructor argument list would race with zc::mv(methodReceiver).
      auto selectedShape = selectShape();
      pendingFunctions.add(PendingFunctionDeclaration{
          definition.definition, definition.node, functionResultType, zc::mv(parameters),
          zc::mv(methodReceiver), zc::mv(visibilityValue), linkageValue, definition.source.clone(),
          bodySpanValue.clone(), returnSpanValue.clone(), valueSpanValue.clone(),
          zc::mv(orderingKey), shape.body, shape.returnStatement, shape.value,
          zc::mv(unsafeBlockSpan), zc::mv(selectedShape)});
      continue;
    }
    if (definition.record.kind() != identity::DefinitionKind::Static &&
        definition.record.kind() != identity::DefinitionKind::Constant) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::MissingRequiredFact, module,
                                           registries, ordinal + 2);
    }
    auto patternSite = patternBindingSite(definitionInventory, definition);
    if (patternSite == zc::none) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    const auto& bindingSite = ZC_ASSERT_NONNULL(patternSite);
    const auto& tree = bound.tree();
    if (bindingSite.patternPath.size() != 0 || !tree.contains(bindingSite.introducer) ||
        tree.node(bindingSite.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    const auto& declarator = tree.node(bindingSite.introducer);
    const ast::NodeId patternNode(declarator.payload.words[ast::kVariableDeclaratorPatternWord]);
    const ast::NodeId initializer(declarator.payload.words[ast::kVariableDeclaratorInitWord]);
    if (!tree.contains(patternNode) ||
        tree.node(patternNode).kind != ast::SyntaxKind::IdentifierPattern ||
        !tree.contains(initializer) || !isScalarLiteral(tree.node(initializer).kind)) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::MissingRequiredFact, module,
                                           registries, ordinal + 2);
    }

    auto definitionTypeIndex = factIndex(facts.definitionTypes(), definition.definition);
    auto patternIndex = factIndex(facts.patterns(), patternNode);
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

    if (!typeExists(definitionType.value, checkedModule.semanticTypes())) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    zc::Array<uint8_t> orderingKey;
    orderingKey = definition.key.encode();
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
        literal.literal.clone(), zc::mv(constant), zc::mv(orderingKey), bindingSite.introducer,
        patternNode, initializer});
  }

  sortPendingDeclarations(pending);
  sortPendingFunctions(pendingFunctions);
  zc::Vector<HirValueDeclaration> declarations;
  zc::Vector<HirFunctionDeclaration> functions;
  zc::Vector<HirBlockStatement> blocks;
  zc::Vector<HirReturnStatement> returns;
  zc::Vector<HirBindingPattern> patterns;
  zc::Vector<HirScalarLiteralExpression> expressions;
  zc::Vector<HirNominalAggregateExpression> aggregates;
  zc::Vector<HirErrorUnionExpression> errorUnions;
  zc::Vector<HirLocalBinding> locals;
  zc::Vector<HirLocalWriteStatement> localWrites;
  zc::Vector<HirLocalReferenceExpression> localReferences;
  zc::Vector<HirLocalFieldProjectionExpression> localFieldProjections;
  zc::Vector<HirParameterFieldProjectionExpression> parameterFieldProjections;
  zc::Vector<HirParameterFieldWriteStatement> parameterFieldWrites;
  zc::Vector<HirRawPointerWriteStatement> rawPointerWrites;
  zc::Vector<HirParameterReferenceExpression> parameterReferences;
  zc::Vector<HirParameterIndexExpression> parameterIndexes;
  zc::Vector<HirParameterReborrowExpression> parameterReborrows;
  zc::Vector<HirLocalBorrowExpression> localBorrows;
  zc::Vector<HirDirectCallExpression> calls;
  zc::Vector<HirReceiverCallExpression> receiverCalls;
  zc::Vector<HirUnsafeBlockExpression> unsafeBlocks;
  zc::Vector<HirPrimitiveBinaryExpression> primitiveBinaryOperations;
  zc::Vector<HirConditionalExpression> conditionals;
  zc::Vector<HirLoopStatement> loops;
  uint32_t next = 1;
  // Side-table mapping HirNodeId ordinal to the source AST node. Index 0 is a
  // dummy so the one-based HirNodeId ordinal is the direct vector index.
  zc::Vector<ast::NodeId> sourceNodes;
  sourceNodes.add(ast::NodeId());
  for (auto& value : pending) {
    const auto declarationId = hirId(next++);
    const auto patternId = hirId(next++);
    const auto initializerId = hirId(next++);
    // Value declarations allocate three node ids outside HirFnCtx; push the
    // source AST nodes so the side-table stays aligned with the shared ordinal
    // counter and the per-node correspondence validator can anchor each node.
    sourceNodes.add(value.declarationNode);
    sourceNodes.add(value.patternNode);
    sourceNodes.add(value.initializerNode);
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
  for (auto& value : pendingFunctions) {
    // Specialized shape dispatch — each variant maps to exactly one lowering
    // helper. The variant type is the discriminant; no field-presence checks.
    auto makeFnCtx = [&]() {
      return HirFnCtx(next, functions, blocks, returns, expressions, parameterReferences, locals,
                      localWrites, localReferences, primitiveBinaryOperations, aggregates,
                      localFieldProjections, parameterFieldProjections, parameterFieldWrites,
                      rawPointerWrites, unsafeBlocks, parameterReborrows, localBorrows,
                      parameterIndexes, calls, receiverCalls, conditionals, loops, sourceNodes);
    };
    if (value.shape.is<ByValueParameterFieldReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerReceiverFieldReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<ReceiverFieldWriteReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerReceiverFieldWriteReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<ReceiverFieldReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerReceiverFieldReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<VoidLetBody>()) {
      auto fnCtx = makeFnCtx();
      lowerVoidLetBodyFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<EmptyVoidBody>() || value.shape.is<DestructorFieldWriteBody>() ||
        value.shape.is<DestructorMethodCallBody>()) {
      auto fnCtx = makeFnCtx();
      lowerEmptyBodyVoidFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<VoidMutatingMethod>()) {
      auto fnCtx = makeFnCtx();
      lowerVoidReceiverFieldWriteFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<ScalarLiteralReturn>() || value.shape.is<ParameterReferenceReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerScalarReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<LocalReturn>() || value.shape.is<ArrayIndexFoldReturn>() ||
        value.shape.is<ArrayLengthFoldReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerLocalReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<SequentialLocalReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerSequentialLocalReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<DynFieldAccessReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerDynFieldAccessReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<ComparisonReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerComparisonReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<PrimitiveUnaryReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerPrimitiveUnaryReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<ReceiverFieldArithmeticReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerReceiverFieldArithmeticFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<EnumConstructionReturn>() || value.shape.is<AggregateReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerEnumConstructionReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<ExistentialReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerExistentialReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<AggregateFieldProjectionReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerAggregateFieldProjectionFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<LocalWriteReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerLocalWriteFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<LocalFieldProjectionReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerLocalFieldWriteFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<DirectScalarLocalCallReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerDirectScalarLocalCallFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<DirectAggregateCallReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerDirectAggregateCallFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<DirectExistentialCallReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerDirectExistentialCallFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<DirectCallReturn>() || value.shape.is<ErrorPropagateCallReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerDirectCallReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<ErrorUnwrapCallReturn>()) {
      // Compute the error-unwrap tag and bool types on the variant before
      // lowering, mirroring the old guard-side computation.
      auto& unwrap = value.shape.get<ErrorUnwrapCallReturn>();
      auto tagType = internPrimitiveType(semanticTypes, type::semantic::PrimitiveKind::I32);
      if (tagType == zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             9001);
      }
      auto boolType = internPrimitiveType(semanticTypes, type::semantic::PrimitiveKind::Bool);
      if (boolType == zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             9002);
      }
      unwrap.errorUnwrapTagType = zc::mv(tagType);
      unwrap.errorUnwrapBoolType = zc::mv(boolType);
      auto fnCtx = makeFnCtx();
      lowerDirectCallReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<DirectCallInitializerReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerDirectCallInitializerFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<DiscardedReceiverCall>()) {
      auto fnCtx = makeFnCtx();
      lowerDiscardedReceiverCallFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<ReceiverCallReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerReceiverCallFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<ReceiverSelfCallReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerReceiverSelfCallFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<ParameterReceiverCallReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerParameterReceiverCallFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<ConditionalReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerConditionalReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<ChainedConditionalReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerChainedConditionalReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<LeadingLocalConditionalReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerLeadingLocalConditionalReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<LoopReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerLoopReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<LoopBodyReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerLoopBodyReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<ComparisonLoopBodyReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerComparisonLoopBodyReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<ForLoopReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerForLoopReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<ForLoopAccumulatorReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerForLoopAccumulatorReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<NestedForLoopAccumulatorReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerNestedForLoopAccumulatorReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<ForInLoopAccumulatorReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerForInLoopAccumulatorReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<ParameterReborrowReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerParameterReborrowReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<UnsafeScalarReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerUnsafeScalarReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<RawPointerWriteReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerRawPointerWriteReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<RawDereferenceReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerRawDereferenceReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<LocalAliasReborrowReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerLocalAliasReborrowReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    if (value.shape.is<LocalBorrowReturn>()) {
      auto fnCtx = makeFnCtx();
      lowerLocalBorrowReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    // A parameter-initialized mut local with writes and a local-reference
    // return is admitted by ownership surface admission and the checker, but
    // the local-write lowering carrier only builds literal-initialized
    // storage. Reject the definition as the expression-statement capability
    // code ZOM4098 (the same code compound assignment and postfix update use)
    // rather than falling through to the generic materializer and failing
    // downstream as an internal compiler error.
    if (value.shape.is<GenericShape>()) {
      const auto& generic = value.shape.get<GenericShape>();
      if (generic.local != zc::none && ZC_ASSERT_NONNULL(generic.local).initializer != zc::none &&
          generic.parameterReference != zc::none && generic.localWrites.size() != 0 &&
          generic.localReference != zc::none) {
        return rejectHirCapability<HirModuleCandidate>(
            value.definition, registries, ir::IrFailureKind::UnsupportedExpressionStatement,
            generic.localWrites[0].sourceSpan.clone());
      }
    }
    // Generic catch-all: shapes no specialized guard claims land here.
    {
      auto fnCtx = makeFnCtx();
      lowerGenericFunction(zc::mv(value), fnCtx);
    }
  }
  auto impl = zc::heap<HirModuleCandidate::Impl>(
      zc::mv(checkedModule), zc::mv(declarations), zc::mv(functions), zc::mv(blocks),
      zc::mv(returns), zc::mv(patterns), zc::mv(expressions), zc::mv(aggregates),
      zc::mv(errorUnions), zc::mv(locals), zc::mv(localWrites), zc::mv(localReferences),
      zc::mv(localFieldProjections), zc::mv(parameterFieldProjections),
      zc::mv(parameterFieldWrites), zc::mv(rawPointerWrites), zc::mv(parameterReferences),
      zc::mv(parameterIndexes), zc::mv(parameterReborrows), zc::mv(localBorrows), zc::mv(calls),
      zc::mv(receiverCalls), zc::mv(unsafeBlocks), zc::mv(primitiveBinaryOperations),
      zc::mv(conditionals), zc::mv(loops), zc::mv(sourceNodes));
  return ir::IrOperationResult<HirModuleCandidate>::verified(HirModuleCandidate(zc::mv(impl)));
}

}  // namespace zomlang::compiler::hir
