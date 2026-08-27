#include "compiler/driver/query/binding/owner-body-query.h"

#include "zc/core/debug.h"
#include "compiler/ast/generated/node-accessors.h"
#include "compiler/ast/generated/node-schema.h"
#include "compiler/binder/stable/stable-binding-codec.h"
#include "compiler/binder/stable/stable-binding-diagnostic-fact.h"
#include "compiler/driver/query/binding/named-identity-inventory-query.h"
#include "compiler/driver/query/binding/named-item-query.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

constexpr zc::StringPtr kFailureDomain = "zom.owner-body-query-failure"_zc;
constexpr uint64_t kMaximumDetachedNodes = 1024 * 1024;
constexpr uint64_t kMaximumScalarBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumIdentifierList = 65536;

enum class OwnerBodyFailureKind : uint8_t {
  InactiveOwner = 0x01,
  ForeignOwner = 0x02,
  DefinitionWithoutBody = 0x03,
  UpstreamSourceRejected = 0x04,
  BoundaryMismatch = 0x05,
  MalformedDetachedSyntax = 0x06,
  MissingProvenance = 0x07,
  DuplicateProvenance = 0x08,
  NonSelectedSource = 0x09,
  CrossBoundaryPath = 0x0a
};

enum class ExecutableRootAdmission : uint8_t { Executable, NoBody, Malformed };

struct DetachedFieldProjection final {
  bool present;
  uint32_t childOrdinal;
};

struct ExecutableRootSelection final {
  ExecutableRootAdmission admission;
  uint32_t childOrdinal;
};

zc::Array<uint8_t> encodeFailure(OwnerBodyFailureKind kind,
                                 zc::ArrayPtr<const uint8_t> payload = {}) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kFailureDomain.asBytes());
  encoder.encodeUint8(static_cast<uint8_t>(kind));
  encoder.encodeByteString(payload);
  return encoder.finish();
}

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

zc::Maybe<identity::ModuleKey> decodeModule(const StableModuleQueryKey& key) {
  identity::CanonicalDecoder decoder(key.canonicalModuleBytes());
  auto module = identity::ModuleKey::decodeCanonical(decoder);
  if (module == zc::none || !decoder.finished()) { return zc::none; }
  return zc::mv(ZC_ASSERT_NONNULL(module));
}

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] < right[index]) return -1;
    if (left[index] > right[index]) return 1;
  }
  if (left.size() < right.size()) return -1;
  if (left.size() > right.size()) return 1;
  return 0;
}

void sortOwners(zc::Vector<binder::StableBodyOwnerKey>& owners) {
  for (size_t index = 1; index < owners.size(); ++index) {
    auto current = zc::mv(owners[index]);
    auto currentBytes = current.encode();
    size_t insertion = index;
    while (insertion != 0) {
      auto previousBytes = owners[insertion - 1].encode();
      if (compareBytes(currentBytes.asPtr(), previousBytes.asPtr()) >= 0) { break; }
      owners[insertion] = zc::mv(owners[insertion - 1]);
      --insertion;
    }
    owners[insertion] = zc::mv(current);
  }
}

zc::Maybe<DetachedFieldProjection> providerField(const binder::DetachedModuleBodyNode& node,
                                                 zc::StringPtr fieldName) {
  if (node.kind() != binder::DetachedModuleBodyNodeKind::Syntax) { return zc::none; }
  auto schema = ast::lookupNodeSchema(ZC_ASSERT_NONNULL(node.syntaxKind()));
  if (schema == nullptr) { return zc::none; }
  for (uint32_t index = 0; index < schema->fieldCount; ++index) {
    if (zc::StringPtr(schema->fields[index].name) != fieldName) { continue; }
    auto field = node.childField(index);
    if (field == zc::none) { return zc::none; }
    return DetachedFieldProjection{ZC_ASSERT_NONNULL(field).present,
                                   ZC_ASSERT_NONNULL(field).firstChildOrdinal};
  }
  return zc::none;
}

zc::Maybe<size_t> immediateChildIndex(zc::ArrayPtr<const binder::DetachedModuleBodyNode> nodes,
                                      uint32_t ordinal) {
  if (nodes.size() == 0 || ordinal >= nodes[0].childCount()) { return zc::none; }
  size_t current = 1;
  for (uint32_t child = 0; child < ordinal; ++child) {
    if (current >= nodes.size()) { return zc::none; }
    uint64_t pending = 1;
    while (pending != 0) {
      if (current >= nodes.size()) { return zc::none; }
      --pending;
      pending += nodes[current].childCount();
      if (pending > kMaximumDetachedNodes) { return zc::none; }
      ++current;
    }
  }
  return current < nodes.size() ? zc::Maybe<size_t>(current) : zc::none;
}

bool validExecutableChild(const binder::ModuleBodySyntax& syntax,
                          const DetachedFieldProjection& field, bool requiresBlock) {
  if (!field.present) { return false; }
  auto index = immediateChildIndex(syntax.nodes(), field.childOrdinal);
  if (index == zc::none) { return false; }
  const auto& child = syntax.nodes()[ZC_ASSERT_NONNULL(index)];
  if (child.kind() != binder::DetachedModuleBodyNodeKind::Syntax) { return false; }
  const auto kind = ZC_ASSERT_NONNULL(child.syntaxKind());
  return requiresBlock ? kind == ast::SyntaxKind::BlockStmt
                       : ast::isLiteralExprKind(kind) || ast::isExprKind(kind) ||
                             kind == ast::SyntaxKind::UnsafeBlockExpr;
}

ExecutableRootSelection providerExecutableRootSelection(const binder::ModuleBodySyntax& syntax) {
  if (syntax.rootCount() != 1 || syntax.nodes().size() == 0 ||
      syntax.nodes()[0].kind() != binder::DetachedModuleBodyNodeKind::Syntax) {
    return ExecutableRootSelection{ExecutableRootAdmission::Malformed, 0};
  }
  const auto rootKind = ZC_ASSERT_NONNULL(syntax.nodes()[0].syntaxKind());
  zc::StringPtr fieldName;
  bool requiresBlock = false;
  bool optional = true;
  switch (rootKind) {
    case ast::SyntaxKind::FunctionDecl:
      fieldName = "body"_zc;
      requiresBlock = true;
      optional = false;
      break;
    case ast::SyntaxKind::MethodDecl:
      fieldName = "body"_zc;
      requiresBlock = true;
      break;
    case ast::SyntaxKind::ConstructorDecl:
    case ast::SyntaxKind::DestructorDecl:
      fieldName = "body"_zc;
      requiresBlock = true;
      optional = false;
      break;
    case ast::SyntaxKind::FieldDecl:
    case ast::SyntaxKind::ClassConstDecl:
      fieldName = "init"_zc;
      break;
    default:
      return ExecutableRootSelection{ExecutableRootAdmission::NoBody, 0};
  }
  auto field = providerField(syntax.nodes()[0], fieldName);
  if (field == zc::none) { return ExecutableRootSelection{ExecutableRootAdmission::Malformed, 0}; }
  if (!ZC_ASSERT_NONNULL(field).present) {
    return ExecutableRootSelection{
        optional ? ExecutableRootAdmission::NoBody : ExecutableRootAdmission::Malformed, 0};
  }
  return ExecutableRootSelection{
      validExecutableChild(syntax, ZC_ASSERT_NONNULL(field), requiresBlock)
          ? ExecutableRootAdmission::Executable
          : ExecutableRootAdmission::Malformed,
      ZC_ASSERT_NONNULL(field).childOrdinal};
}

ExecutableRootAdmission providerExecutableRoot(const binder::ModuleBodySyntax& syntax) {
  return providerExecutableRootSelection(syntax).admission;
}

zc::Maybe<DetachedFieldProjection> verifierFieldAt(const binder::DetachedModuleBodyNode& node,
                                                   uint32_t targetFieldIndex) {
  if (node.kind() != binder::DetachedModuleBodyNodeKind::Syntax) { return zc::none; }
  auto schema = ast::lookupNodeSchema(ZC_ASSERT_NONNULL(node.syntaxKind()));
  if (schema == nullptr || targetFieldIndex >= schema->fieldCount) { return zc::none; }
  identity::CanonicalDecoder decoder(node.canonicalPayload());
  auto count = decoder.decodeSequenceSize(schema->fieldCount);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) != schema->fieldCount) { return zc::none; }
  uint32_t childOrdinal = 0;
  zc::Maybe<DetachedFieldProjection> selected;
  for (uint32_t index = 0; index < schema->fieldCount; ++index) {
    const auto& field = schema->fields[index];
    auto storage = decoder.decodeUint8();
    auto optional = decoder.decodeBool();
    if (storage == zc::none || optional == zc::none ||
        ZC_ASSERT_NONNULL(storage) != static_cast<uint8_t>(field.storage) + 1 ||
        ZC_ASSERT_NONNULL(optional) != field.optional) {
      return zc::none;
    }
    switch (field.storage) {
      case ast::NodeSchemaFieldStorage::NodeId: {
        auto present = decoder.decodeBool();
        if (present == zc::none || (!field.optional && !ZC_ASSERT_NONNULL(present))) {
          return zc::none;
        }
        if (index == targetFieldIndex) {
          selected = DetachedFieldProjection{ZC_ASSERT_NONNULL(present), childOrdinal};
        }
        if (ZC_ASSERT_NONNULL(present)) { ++childOrdinal; }
        break;
      }
      case ast::NodeSchemaFieldStorage::NodeList: {
        auto sequence = decoder.decodeSequenceSize(kMaximumDetachedNodes);
        if (sequence == zc::none || ZC_ASSERT_NONNULL(sequence) > UINT32_MAX - childOrdinal) {
          return zc::none;
        }
        childOrdinal += static_cast<uint32_t>(ZC_ASSERT_NONNULL(sequence));
        break;
      }
      case ast::NodeSchemaFieldStorage::IdentList: {
        auto sequence = decoder.decodeSequenceSize(kMaximumIdentifierList);
        if (sequence == zc::none) { return zc::none; }
        ZC_IF_SOME(value, sequence) {
          for (uint64_t item = 0; item < value; ++item) {
            if (decoder.decodeByteString(kMaximumScalarBytes) == zc::none) { return zc::none; }
          }
        }
        break;
      }
      case ast::NodeSchemaFieldStorage::StringId:
      case ast::NodeSchemaFieldStorage::IdentId:
      case ast::NodeSchemaFieldStorage::BigIntId:
      case ast::NodeSchemaFieldStorage::FloatId: {
        auto present = decoder.decodeBool();
        if (present == zc::none || (!field.optional && !ZC_ASSERT_NONNULL(present)) ||
            (ZC_ASSERT_NONNULL(present) &&
             decoder.decodeByteString(kMaximumScalarBytes) == zc::none)) {
          return zc::none;
        }
        break;
      }
      case ast::NodeSchemaFieldStorage::Bool:
        if (decoder.decodeBool() == zc::none) { return zc::none; }
        break;
      case ast::NodeSchemaFieldStorage::UInt8:
        if (decoder.decodeUint8() == zc::none) { return zc::none; }
        break;
      case ast::NodeSchemaFieldStorage::UInt16:
      case ast::NodeSchemaFieldStorage::UInt32:
      case ast::NodeSchemaFieldStorage::Enum:
        if (decoder.decodeUint32() == zc::none) { return zc::none; }
        break;
      case ast::NodeSchemaFieldStorage::UInt64:
        if (decoder.decodeUint64() == zc::none) { return zc::none; }
        break;
    }
  }
  if (!decoder.finished() || childOrdinal != node.childCount()) { return zc::none; }
  return selected;
}

ExecutableRootSelection verifierExecutableRootSelection(const binder::ModuleBodySyntax& syntax) {
  if (syntax.rootCount() != 1 || syntax.nodes().size() == 0) {
    return ExecutableRootSelection{ExecutableRootAdmission::Malformed, 0};
  }
  const auto& root = syntax.nodes()[0];
  if (root.kind() != binder::DetachedModuleBodyNodeKind::Syntax) {
    return ExecutableRootSelection{ExecutableRootAdmission::Malformed, 0};
  }
  uint32_t fieldIndex = 0;
  bool block = false;
  bool required = false;
  switch (ZC_ASSERT_NONNULL(root.syntaxKind())) {
    case ast::SyntaxKind::FunctionDecl:
      fieldIndex = 5;
      block = true;
      required = true;
      break;
    case ast::SyntaxKind::MethodDecl:
      fieldIndex = 5;
      block = true;
      break;
    case ast::SyntaxKind::ConstructorDecl:
    case ast::SyntaxKind::DestructorDecl:
      fieldIndex = 3;
      block = true;
      required = true;
      break;
    case ast::SyntaxKind::FieldDecl:
    case ast::SyntaxKind::ClassConstDecl:
      fieldIndex = 2;
      break;
    default:
      return ExecutableRootSelection{ExecutableRootAdmission::NoBody, 0};
  }
  auto selected = verifierFieldAt(root, fieldIndex);
  if (selected == zc::none) {
    return ExecutableRootSelection{ExecutableRootAdmission::Malformed, 0};
  }
  if (!ZC_ASSERT_NONNULL(selected).present) {
    return ExecutableRootSelection{
        required ? ExecutableRootAdmission::Malformed : ExecutableRootAdmission::NoBody, 0};
  }
  return ExecutableRootSelection{validExecutableChild(syntax, ZC_ASSERT_NONNULL(selected), block)
                                     ? ExecutableRootAdmission::Executable
                                     : ExecutableRootAdmission::Malformed,
                                 ZC_ASSERT_NONNULL(selected).childOrdinal};
}

ExecutableRootAdmission verifierExecutableRoot(const binder::ModuleBodySyntax& syntax) {
  return verifierExecutableRootSelection(syntax).admission;
}

zc::Maybe<binder::ModuleBodySyntax> projectProviderExecutableSyntax(
    const binder::ModuleBodySyntax& syntax, uint32_t childOrdinal) {
  auto start = immediateChildIndex(syntax.nodes(), childOrdinal);
  if (start == zc::none) { return zc::none; }
  uint64_t remaining = 1;
  size_t end = ZC_ASSERT_NONNULL(start);
  while (remaining != 0) {
    if (end >= syntax.nodes().size() || remaining > UINT64_MAX - syntax.nodes()[end].childCount()) {
      return zc::none;
    }
    --remaining;
    remaining += syntax.nodes()[end].childCount();
    ++end;
  }
  zc::Vector<binder::DetachedModuleBodyNode> nodes(end - ZC_ASSERT_NONNULL(start));
  for (size_t index = ZC_ASSERT_NONNULL(start); index < end; ++index) {
    nodes.add(syntax.nodes()[index].clone());
  }
  return binder::ModuleBodySyntax::from(1, zc::mv(nodes));
}

zc::Maybe<binder::ModuleBodyProvenance> projectProviderExecutableProvenance(
    const binder::ModuleBodyProvenance& provenance, uint32_t childOrdinal) {
  zc::Vector<binder::ModuleBodyProvenanceEntry> entries;
  for (const auto& entry : provenance.entries()) {
    const auto components = entry.path.components();
    if (components.size() < 2 || components[0] != 0 || components[1] != childOrdinal) { continue; }
    zc::Vector<uint32_t> rebased(components.size());
    rebased.add(0);
    for (size_t index = 2; index < components.size(); ++index) { rebased.add(components[index]); }
    auto path = binder::LocalSyntaxPath::from(zc::mv(rebased));
    if (path == zc::none) { return zc::none; }
    entries.add(binder::ModuleBodyProvenanceEntry{zc::mv(ZC_ASSERT_NONNULL(path)), entry.node,
                                                  entry.byteStart, entry.byteEnd});
  }
  return binder::ModuleBodyProvenance::from(provenance.source().clone(), zc::mv(entries));
}

zc::Maybe<binder::ModuleBodySyntax> projectVerifierExecutableSyntax(
    const binder::ModuleBodySyntax& syntax, uint32_t childOrdinal) {
  auto first = immediateChildIndex(syntax.nodes(), childOrdinal);
  if (first == zc::none) { return zc::none; }
  zc::Vector<binder::DetachedModuleBodyNode> nodes;
  uint64_t openNodes = 1;
  for (size_t index = ZC_ASSERT_NONNULL(first); openNodes != 0; ++index) {
    if (index >= syntax.nodes().size() ||
        openNodes > UINT64_MAX - syntax.nodes()[index].childCount()) {
      return zc::none;
    }
    nodes.add(syntax.nodes()[index].clone());
    --openNodes;
    openNodes += syntax.nodes()[index].childCount();
  }
  return binder::ModuleBodySyntax::from(1, zc::mv(nodes));
}

zc::Maybe<binder::ModuleBodyProvenance> projectVerifierExecutableProvenance(
    const binder::ModuleBodyProvenance& provenance, uint32_t childOrdinal) {
  zc::Vector<binder::ModuleBodyProvenanceEntry> entries;
  for (const auto& entry : provenance.entries()) {
    const auto components = entry.path.components();
    if (components.size() < 2 || components[0] != 0 || components[1] != childOrdinal) { continue; }
    zc::Vector<uint32_t> rebased;
    rebased.add(0);
    for (size_t component = 2; component < components.size(); ++component) {
      rebased.add(components[component]);
    }
    auto path = binder::LocalSyntaxPath::from(zc::mv(rebased));
    if (path == zc::none) { return zc::none; }
    entries.add(binder::ModuleBodyProvenanceEntry{zc::mv(ZC_ASSERT_NONNULL(path)), entry.node,
                                                  entry.byteStart, entry.byteEnd});
  }
  return binder::ModuleBodyProvenance::from(provenance.source().clone(), zc::mv(entries));
}

bool samePath(zc::ArrayPtr<const uint32_t> expected, const binder::LocalSyntaxPath& actual) {
  return expected == actual.components();
}

bool providerProvenanceMatches(const binder::OwnerBodySyntax& syntax,
                               const binder::ModuleBodyProvenance& provenance) {
  if (!provenance.source().belongsTo(syntax.owningModule().crate())) { return false; }
  const auto nodes = syntax.detachedSyntax().nodes();
  const auto entries = provenance.entries();
  zc::Vector<uint32_t> path;
  zc::Vector<uint32_t> childCounts;
  size_t entryIndex = 0;
  uint32_t rootIndex = 0;
  for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
    if (path.size() == 0) { path.add(rootIndex); }
    const auto& node = nodes[nodeIndex];
    if (node.kind() == binder::DetachedModuleBodyNodeKind::Syntax) {
      if (entryIndex >= entries.size() || !samePath(path.asPtr(), entries[entryIndex].path)) {
        return false;
      }
      ++entryIndex;
    } else if (node.childCount() != 0) {
      return false;
    }
    if (node.childCount() != 0) {
      childCounts.add(node.childCount());
      path.add(0);
      continue;
    }
    while (childCounts.size() != 0) {
      const uint32_t next = path.back() + 1;
      if (next < childCounts.back()) {
        path.back() = next;
        break;
      }
      childCounts.removeLast();
      path.removeLast();
    }
    if (childCounts.size() == 0) {
      path.clear();
      ++rootIndex;
    }
  }
  return entryIndex == entries.size() && rootIndex == syntax.detachedSyntax().rootCount();
}

bool verifierProvenanceMatches(const binder::OwnerBodySyntax& syntax,
                               const binder::ModuleBodyProvenance& provenance) {
  if (!provenance.source().belongsTo(syntax.owningModule().crate())) { return false; }
  const auto nodes = syntax.detachedSyntax().nodes();
  const auto entries = provenance.entries();
  size_t nodeIndex = 0;
  size_t entryIndex = 0;
  for (uint32_t root = 0; root < syntax.detachedSyntax().rootCount(); ++root) {
    zc::Vector<uint32_t> path;
    path.add(root);
    zc::Vector<uint32_t> remaining;
    bool rootComplete = false;
    while (!rootComplete) {
      if (nodeIndex >= nodes.size()) { return false; }
      const auto& node = nodes[nodeIndex++];
      if (node.kind() == binder::DetachedModuleBodyNodeKind::Syntax) {
        if (entryIndex >= entries.size() || entries[entryIndex].path.components() != path.asPtr()) {
          return false;
        }
        ++entryIndex;
      } else if (node.childCount() != 0) {
        return false;
      }
      if (node.childCount() != 0) {
        remaining.add(node.childCount());
        path.add(0);
        continue;
      }
      while (remaining.size() != 0 && path.back() + 1 == remaining.back()) {
        remaining.removeLast();
        path.removeLast();
      }
      if (remaining.size() == 0) {
        rootComplete = true;
      } else {
        ++path.back();
      }
    }
  }
  return nodeIndex == nodes.size() && entryIndex == entries.size();
}

template <typename Value, typename Upstream>
query::TypedQueryResult<Value> propagate(const query::TypedQueryResult<Upstream>& upstream) {
  if (upstream.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(upstream.runtimeFailure());
  }
  if (upstream.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<Value>::semanticFailure(
        zc::heapArray<uint8_t>(upstream.semanticFailureBytes()));
  }
  return query::TypedQueryResult<Value>::runtimeFailure(
      query::QueryRuntimeFailure::InvariantViolation);
}

template <typename TargetDescriptor, typename SourceDescriptor>
query::CapabilityProviderResult<TargetDescriptor> forwardSourceRejection(
    const query::CapabilityDemandResult<SourceDescriptor>& source) {
  using SourceContract =
      query::CapabilityFailureContract<SourceDescriptor,
                                       query::SourceRejection<diagnostics::DiagnosticFact>>;
  using TargetContract =
      query::CapabilityFailureContract<TargetDescriptor,
                                       query::SourceRejection<diagnostics::DiagnosticFact>>;
  auto diagnostics = TargetContract::decode(SourceContract::encode(source.diagnostics()).asPtr());
  if (diagnostics == zc::none) {
    return query::CapabilityProviderResult<TargetDescriptor>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::CapabilityProviderResult<TargetDescriptor>::template sourceRejected<
      diagnostics::DiagnosticFact>(zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
}

template <typename TargetDescriptor, typename SourceDescriptor>
query::CapabilityProviderResult<TargetDescriptor> forwardKeyRejection(
    const query::CapabilityDemandResult<SourceDescriptor>& source) {
  return query::CapabilityProviderResult<TargetDescriptor>::template keyRejected<
      binder::BinderKeyFailure>(source.keyFailure().clone());
}

zc::Maybe<binder::BinderKeyFailure> definitionWithoutBodyFailure(
    const ContextualBodyOwnerKey& key) {
  zc::Maybe<binder::LocalSyntaxPath> noPath;
  return binder::BinderKeyFailure::from(binder::BinderKeyFailureKind::DefinitionWithoutBody,
                                        binder::BinderQueryOwner::body(key.body().clone()),
                                        zc::mv(noPath));
}

query::TypedQueryResult<binder::ModuleBodyOwners> providerModuleOwners(
    query::QueryContext& context, const ContextualModuleKey& key) {
  auto stableModule = StableModuleQueryKey::fromVerified(key.module());
  if (stableModule == zc::none) {
    return query::TypedQueryResult<binder::ModuleBodyOwners>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto module = decodeModule(ZC_ASSERT_NONNULL(stableModule));
  if (module == zc::none) {
    return query::TypedQueryResult<binder::ModuleBodyOwners>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto inventory = context.get<NamedDefinitionInventoryQuery>(ZC_ASSERT_NONNULL(stableModule));
  if (inventory.kind() != query::QueryValueKind::Value || inventory.isRuntimeFailure()) {
    return propagate<binder::ModuleBodyOwners>(inventory);
  }
  zc::Vector<identity::DefinitionKey> definitions(inventory.value().entries().size());
  zc::Vector<ContextualDefinitionKey> keys(inventory.value().entries().size());
  for (const auto& entry : inventory.value().entries()) {
    definitions.add(entry.key().clone());
    keys.add(ContextualDefinitionKey::from(
        key.contextRoots().clone(),
        binder::StableDefinitionQueryKey::from(key.module().clone(), entry.key().clone())));
  }
  zc::Vector<binder::StableBodyOwnerKey> owners(keys.size() + 1);
  owners.add(binder::StableBodyOwnerKey::module(ZC_ASSERT_NONNULL(module).clone()));
  for (size_t index = 0; index < keys.size(); ++index) {
    auto syntax = context.get<NamedItemSyntaxQuery>(keys[index]);
    if (syntax.kind() != query::QueryValueKind::Value || syntax.isRuntimeFailure()) {
      return propagate<binder::ModuleBodyOwners>(syntax);
    }
    if (!sameModule(syntax.value().owningModule(), ZC_ASSERT_NONNULL(module))) {
      return query::TypedQueryResult<binder::ModuleBodyOwners>::semanticFailure(
          encodeFailure(OwnerBodyFailureKind::ForeignOwner));
    }
    const auto admission = providerExecutableRoot(syntax.value().detachedSyntax());
    if (admission == ExecutableRootAdmission::Malformed) {
      return query::TypedQueryResult<binder::ModuleBodyOwners>::semanticFailure(
          encodeFailure(OwnerBodyFailureKind::MalformedDetachedSyntax));
    }
    if (admission == ExecutableRootAdmission::Executable) {
      owners.add(binder::StableBodyOwnerKey::definition(definitions[index].clone()));
    }
  }
  sortOwners(owners);
  auto value = binder::ModuleBodyOwners::from(ZC_ASSERT_NONNULL(module).clone(), zc::mv(owners));
  if (value == zc::none) {
    return query::TypedQueryResult<binder::ModuleBodyOwners>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<binder::ModuleBodyOwners>::value(zc::mv(ZC_ASSERT_NONNULL(value)));
}

bool verifierModuleOwners(query::QueryContext& context, const ContextualModuleKey& key,
                          const query::TypedQueryResult<binder::ModuleBodyOwners>& result) {
  auto stableModule = StableModuleQueryKey::fromVerified(key.module());
  if (stableModule == zc::none) { return false; }
  auto module = decodeModule(ZC_ASSERT_NONNULL(stableModule));
  if (module == zc::none) { return false; }
  auto inventory = context.get<NamedDefinitionInventoryQuery>(ZC_ASSERT_NONNULL(stableModule));
  if (inventory.isRuntimeFailure()) { return false; }
  if (inventory.kind() == query::QueryValueKind::SemanticFailure) {
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == inventory.semanticFailureBytes();
  }
  if (inventory.kind() != query::QueryValueKind::Value) { return false; }
  zc::Vector<identity::DefinitionKey> definitions(inventory.value().entries().size());
  zc::Vector<ContextualDefinitionKey> keys(inventory.value().entries().size());
  for (const auto& entry : inventory.value().entries()) {
    definitions.add(entry.key().clone());
    keys.add(ContextualDefinitionKey::from(
        key.contextRoots().clone(),
        binder::StableDefinitionQueryKey::from(key.module().clone(), entry.key().clone())));
  }
  zc::Vector<binder::StableBodyOwnerKey> expectedOwners(keys.size() + 1);
  expectedOwners.add(binder::StableBodyOwnerKey::module(ZC_ASSERT_NONNULL(module).clone()));
  for (size_t index = 0; index < keys.size(); ++index) {
    auto syntax = context.get<NamedItemSyntaxQuery>(keys[index]);
    if (syntax.isRuntimeFailure()) { return false; }
    if (syntax.kind() == query::QueryValueKind::SemanticFailure) {
      return result.kind() == query::QueryValueKind::SemanticFailure &&
             result.semanticFailureBytes() == syntax.semanticFailureBytes();
    }
    if (syntax.kind() != query::QueryValueKind::Value) { return false; }
    if (!sameModule(syntax.value().owningModule(), ZC_ASSERT_NONNULL(module))) {
      return result.kind() == query::QueryValueKind::SemanticFailure &&
             result.semanticFailureBytes() ==
                 encodeFailure(OwnerBodyFailureKind::ForeignOwner).asPtr();
    }
    const auto admission = verifierExecutableRoot(syntax.value().detachedSyntax());
    if (admission == ExecutableRootAdmission::Malformed) {
      return result.kind() == query::QueryValueKind::SemanticFailure &&
             result.semanticFailureBytes() ==
                 encodeFailure(OwnerBodyFailureKind::MalformedDetachedSyntax).asPtr();
    }
    if (admission == ExecutableRootAdmission::Executable) {
      expectedOwners.add(binder::StableBodyOwnerKey::definition(definitions[index].clone()));
    }
  }
  sortOwners(expectedOwners);
  auto expected =
      binder::ModuleBodyOwners::from(ZC_ASSERT_NONNULL(module).clone(), zc::mv(expectedOwners));
  return result.kind() == query::QueryValueKind::Value && expected != zc::none &&
         ZC_ASSERT_NONNULL(expected) == result.value();
}

}  // namespace

zc::Array<uint8_t> ModuleBodyOwnersQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<ModuleBodyOwnersQuery::Key> ModuleBodyOwnersQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualModuleKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> ModuleBodyOwnersQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ModuleBodyOwnersQuery::Value> ModuleBodyOwnersQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return binder::ModuleBodyOwners::decodeCanonical(bytes);
}

query::TypedQueryResult<ModuleBodyOwnersQuery::Value> ModuleBodyOwnersQuery::provide(
    query::QueryContext& context, const Key& key) {
  return providerModuleOwners(context, key);
}

bool ModuleBodyOwnersQuery::verify(query::QueryContext& context, const Key& key,
                                   const query::TypedQueryResult<Value>& result) {
  return verifierModuleOwners(context, key, result);
}

zc::Array<uint8_t> OwnerBodySyntaxQuery::encodeKey(const Key& key) { return key.encodeCanonical(); }

zc::Maybe<OwnerBodySyntaxQuery::Key> OwnerBodySyntaxQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualBodyOwnerKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> OwnerBodySyntaxQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<OwnerBodySyntaxQuery::Value> OwnerBodySyntaxQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return binder::OwnerBodySyntax::decodeCanonical(bytes);
}

query::TypedQueryResult<OwnerBodySyntaxQuery::Value> OwnerBodySyntaxQuery::provide(
    query::QueryContext& context, const Key& key) {
  const auto& body = key.body();
  const auto& owner = body.owner();
  if (owner.kind() == binder::StableBodyOwnerKind::Module) {
    auto queryKey = StableModuleQueryKey::fromVerified(body.module());
    if (queryKey == zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    auto syntax = context.get<ModuleBodySyntaxQuery>(ZC_ASSERT_NONNULL(queryKey));
    if (syntax.kind() != query::QueryValueKind::Value || syntax.isRuntimeFailure()) {
      return propagate<Value>(syntax);
    }
    auto value =
        binder::OwnerBodySyntax::from(owner.clone(), body.module().clone(), syntax.value().clone());
    if (value == zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(value)));
  }

  const auto& definition = ZC_ASSERT_NONNULL(owner.definitionKey());
  auto definitionKey = ContextualDefinitionKey::from(
      key.contextRoots().clone(),
      binder::StableDefinitionQueryKey::from(body.module().clone(), definition.clone()));
  auto syntax = context.get<NamedItemSyntaxQuery>(definitionKey);
  if (syntax.kind() != query::QueryValueKind::Value || syntax.isRuntimeFailure()) {
    return propagate<Value>(syntax);
  }
  if (!sameModule(syntax.value().owningModule(), body.module())) {
    return query::TypedQueryResult<Value>::semanticFailure(
        encodeFailure(OwnerBodyFailureKind::ForeignOwner));
  }
  const auto selection = providerExecutableRootSelection(syntax.value().detachedSyntax());
  if (selection.admission == ExecutableRootAdmission::NoBody) {
    return query::TypedQueryResult<Value>::semanticFailure(
        encodeFailure(OwnerBodyFailureKind::DefinitionWithoutBody));
  }
  if (selection.admission == ExecutableRootAdmission::Malformed) {
    return query::TypedQueryResult<Value>::semanticFailure(
        encodeFailure(OwnerBodyFailureKind::MalformedDetachedSyntax));
  }
  auto executable =
      projectProviderExecutableSyntax(syntax.value().detachedSyntax(), selection.childOrdinal);
  if (executable == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto value = binder::OwnerBodySyntax::from(owner.clone(), body.module().clone(),
                                             zc::mv(ZC_ASSERT_NONNULL(executable)));
  if (value == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(value)));
}

bool OwnerBodySyntaxQuery::verify(query::QueryContext& context, const Key& key,
                                  const query::TypedQueryResult<Value>& result) {
  const auto& body = key.body();
  const auto& owner = body.owner();
  if (owner.kind() == binder::StableBodyOwnerKind::Module) {
    auto queryKey = StableModuleQueryKey::fromVerified(body.module());
    if (queryKey == zc::none) { return false; }
    auto syntax = context.get<ModuleBodySyntaxQuery>(ZC_ASSERT_NONNULL(queryKey));
    if (syntax.isRuntimeFailure()) { return false; }
    if (syntax.kind() == query::QueryValueKind::SemanticFailure) {
      return result.kind() == query::QueryValueKind::SemanticFailure &&
             result.semanticFailureBytes() == syntax.semanticFailureBytes();
    }
    if (syntax.kind() != query::QueryValueKind::Value) { return false; }
    auto expected =
        binder::OwnerBodySyntax::from(owner.clone(), body.module().clone(), syntax.value().clone());
    return result.kind() == query::QueryValueKind::Value && expected != zc::none &&
           ZC_ASSERT_NONNULL(expected) == result.value();
  }

  const auto& definition = ZC_ASSERT_NONNULL(owner.definitionKey());
  auto definitionKey = ContextualDefinitionKey::from(
      key.contextRoots().clone(),
      binder::StableDefinitionQueryKey::from(body.module().clone(), definition.clone()));
  auto syntax = context.get<NamedItemSyntaxQuery>(definitionKey);
  if (syntax.isRuntimeFailure()) { return false; }
  if (syntax.kind() == query::QueryValueKind::SemanticFailure) {
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == syntax.semanticFailureBytes();
  }
  if (syntax.kind() != query::QueryValueKind::Value) { return false; }
  if (!sameModule(syntax.value().owningModule(), body.module())) {
    auto expectedFailure = encodeFailure(OwnerBodyFailureKind::ForeignOwner);
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == expectedFailure.asPtr();
  }
  const auto selection = verifierExecutableRootSelection(syntax.value().detachedSyntax());
  if (selection.admission != ExecutableRootAdmission::Executable) {
    const auto failure = selection.admission == ExecutableRootAdmission::NoBody
                             ? OwnerBodyFailureKind::DefinitionWithoutBody
                             : OwnerBodyFailureKind::MalformedDetachedSyntax;
    auto expectedFailure = encodeFailure(failure);
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == expectedFailure.asPtr();
  }
  auto executable =
      projectVerifierExecutableSyntax(syntax.value().detachedSyntax(), selection.childOrdinal);
  if (executable == zc::none) { return false; }
  auto expected = binder::OwnerBodySyntax::from(owner.clone(), body.module().clone(),
                                                zc::mv(ZC_ASSERT_NONNULL(executable)));
  return result.kind() == query::QueryValueKind::Value && expected != zc::none &&
         ZC_ASSERT_NONNULL(expected) == result.value();
}

zc::Array<uint8_t> OwnerBodyProvenanceQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<OwnerBodyProvenanceQuery::Key> OwnerBodyProvenanceQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualBodyOwnerKey::decodeCanonical(bytes);
}

query::CapabilityProviderResult<OwnerBodyProvenanceQuery> OwnerBodyProvenanceQuery::provide(
    query::CapabilityQueryContext<OwnerBodyProvenanceQuery>& context, const Key& key) {
  const auto& body = key.body();
  const auto& owner = body.owner();
  zc::Maybe<binder::OwnerBodySyntax> syntax;
  zc::Maybe<binder::ModuleBodyProvenance> retained;
  if (owner.kind() == binder::StableBodyOwnerKind::Module) {
    auto moduleKey = StableModuleQueryKey::fromVerified(body.module());
    if (moduleKey == zc::none) {
      return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    auto provenance =
        context.getCapability<ModuleBodyProvenanceQuery>(ZC_ASSERT_NONNULL(moduleKey));
    if (provenance.isRuntimeRejected()) {
      return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::runtimeRejected(
          provenance.runtimeFailure());
    }
    if (provenance.isKeyRejected()) {
      return forwardKeyRejection<OwnerBodyProvenanceQuery>(provenance);
    }
    if (provenance.isSourceRejected()) {
      return forwardSourceRejection<OwnerBodyProvenanceQuery>(provenance);
    }
    if (!provenance.isPublished()) {
      return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    auto moduleSyntax = context.get<ModuleBodySyntaxQuery>(ZC_ASSERT_NONNULL(moduleKey));
    if (moduleSyntax.isRuntimeFailure()) {
      return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::runtimeRejected(
          moduleSyntax.runtimeFailure());
    }
    if (moduleSyntax.kind() != query::QueryValueKind::Value) {
      return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    syntax = binder::OwnerBodySyntax::from(owner.clone(), body.module().clone(),
                                           moduleSyntax.value().clone());
    if (syntax == zc::none) {
      return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    retained = provenance.lease().capability().clone();
  } else {
    auto definitionKey = ContextualDefinitionKey::from(
        key.contextRoots().clone(),
        binder::StableDefinitionQueryKey::from(body.module().clone(),
                                               ZC_ASSERT_NONNULL(owner.definitionKey()).clone()));
    auto provenance = context.getCapability<NamedItemProvenanceQuery>(definitionKey);
    if (provenance.isRuntimeRejected()) {
      return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::runtimeRejected(
          provenance.runtimeFailure());
    }
    if (provenance.isKeyRejected()) {
      return forwardKeyRejection<OwnerBodyProvenanceQuery>(provenance);
    }
    if (provenance.isSourceRejected()) {
      return forwardSourceRejection<OwnerBodyProvenanceQuery>(provenance);
    }
    if (!provenance.isPublished()) {
      return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    auto namedSyntax = context.get<NamedItemSyntaxQuery>(definitionKey);
    if (namedSyntax.isRuntimeFailure()) {
      return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::runtimeRejected(
          namedSyntax.runtimeFailure());
    }
    if (namedSyntax.kind() != query::QueryValueKind::Value ||
        !sameModule(namedSyntax.value().owningModule(), body.module())) {
      return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    const auto selection = providerExecutableRootSelection(namedSyntax.value().detachedSyntax());
    if (selection.admission == ExecutableRootAdmission::NoBody) {
      auto failure = definitionWithoutBodyFailure(key);
      if (failure == zc::none) {
        return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::runtimeRejected(
            query::QueryRuntimeFailure::InvariantViolation);
      }
      return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::keyRejected<
          binder::BinderKeyFailure>(zc::mv(ZC_ASSERT_NONNULL(failure)));
    }
    if (selection.admission != ExecutableRootAdmission::Executable) {
      return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    auto executable = projectProviderExecutableSyntax(namedSyntax.value().detachedSyntax(),
                                                      selection.childOrdinal);
    auto projectedProvenance = projectProviderExecutableProvenance(
        provenance.lease().capability().detachedProvenance(), selection.childOrdinal);
    if (executable == zc::none || projectedProvenance == zc::none) {
      return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    syntax = binder::OwnerBodySyntax::from(owner.clone(), body.module().clone(),
                                           zc::mv(ZC_ASSERT_NONNULL(executable)));
    if (syntax == zc::none) {
      return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    retained = zc::mv(ZC_ASSERT_NONNULL(projectedProvenance));
  }
  if (retained == zc::none ||
      !providerProvenanceMatches(ZC_ASSERT_NONNULL(syntax), ZC_ASSERT_NONNULL(retained))) {
    return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto value =
      binder::OwnerBodyProvenance::from(owner.clone(), zc::mv(ZC_ASSERT_NONNULL(retained)));
  if (value == zc::none) {
    return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto candidate = zc::heap<Capability>(zc::mv(ZC_ASSERT_NONNULL(value)));
  auto stableWitness =
      query::CapabilityCandidateContract<OwnerBodyProvenanceQuery>::encode(*candidate);
  return query::CapabilityProviderResult<OwnerBodyProvenanceQuery>::candidate(
      zc::mv(candidate), zc::mv(stableWitness));
}

zc::Maybe<zc::Array<uint8_t>> OwnerBodyProvenanceQuery::verify(
    query::CapabilityQueryContext<OwnerBodyProvenanceQuery>& context, const Key& key,
    const Capability& candidate) {
  const auto& body = key.body();
  const auto& owner = body.owner();
  zc::Maybe<binder::OwnerBodySyntax> syntax;
  zc::Maybe<binder::ModuleBodyProvenance> retained;
  if (owner.kind() == binder::StableBodyOwnerKind::Module) {
    auto moduleKey = StableModuleQueryKey::fromVerified(body.module());
    if (moduleKey == zc::none) { return zc::none; }
    auto provenance =
        context.getCapability<ModuleBodyProvenanceQuery>(ZC_ASSERT_NONNULL(moduleKey));
    if (!provenance.isPublished()) { return zc::none; }
    auto moduleSyntax = context.get<ModuleBodySyntaxQuery>(ZC_ASSERT_NONNULL(moduleKey));
    if (moduleSyntax.isRuntimeFailure() || moduleSyntax.kind() != query::QueryValueKind::Value) {
      return zc::none;
    }
    syntax = binder::OwnerBodySyntax::from(owner.clone(), body.module().clone(),
                                           moduleSyntax.value().clone());
    retained = provenance.lease().capability().clone();
  } else {
    auto definitionKey = ContextualDefinitionKey::from(
        key.contextRoots().clone(),
        binder::StableDefinitionQueryKey::from(body.module().clone(),
                                               ZC_ASSERT_NONNULL(owner.definitionKey()).clone()));
    auto provenance = context.getCapability<NamedItemProvenanceQuery>(definitionKey);
    if (!provenance.isPublished()) { return zc::none; }
    auto namedSyntax = context.get<NamedItemSyntaxQuery>(definitionKey);
    if (namedSyntax.isRuntimeFailure() || namedSyntax.kind() != query::QueryValueKind::Value ||
        !sameModule(namedSyntax.value().owningModule(), body.module())) {
      return zc::none;
    }
    const auto selection = verifierExecutableRootSelection(namedSyntax.value().detachedSyntax());
    if (selection.admission != ExecutableRootAdmission::Executable) { return zc::none; }
    auto executable = projectVerifierExecutableSyntax(namedSyntax.value().detachedSyntax(),
                                                      selection.childOrdinal);
    auto projectedProvenance = projectVerifierExecutableProvenance(
        provenance.lease().capability().detachedProvenance(), selection.childOrdinal);
    if (executable == zc::none || projectedProvenance == zc::none) { return zc::none; }
    syntax = binder::OwnerBodySyntax::from(owner.clone(), body.module().clone(),
                                           zc::mv(ZC_ASSERT_NONNULL(executable)));
    retained = zc::mv(ZC_ASSERT_NONNULL(projectedProvenance));
  }
  if (syntax == zc::none || retained == zc::none ||
      !verifierProvenanceMatches(ZC_ASSERT_NONNULL(syntax), ZC_ASSERT_NONNULL(retained))) {
    return zc::none;
  }
  auto expected =
      binder::OwnerBodyProvenance::from(owner.clone(), zc::mv(ZC_ASSERT_NONNULL(retained)));
  if (expected == zc::none || ZC_ASSERT_NONNULL(expected) != candidate) { return zc::none; }
  auto witness = candidate.encodeCanonical();
  auto decoded = binder::OwnerBodyProvenance::decodeCanonical(witness.asPtr());
  if (decoded == zc::none || ZC_ASSERT_NONNULL(decoded) != candidate) { return zc::none; }
  return zc::mv(witness);
}

query::CapabilityRejectionCheck verifyOwnerBodySourceRejection(
    query::CapabilityQueryContext<OwnerBodyProvenanceQuery>& context,
    const OwnerBodyProvenanceQuery::Key& key,
    zc::ArrayPtr<const diagnostics::DiagnosticFact> diagnostics) {
  const auto& body = key.body();
  const auto& owner = body.owner();
  auto actual = binder::encodeStableBindingDiagnosticFacts(diagnostics);
  if (actual == zc::none) { return query::CapabilityRejectionCheck::Rejected; }
  if (owner.kind() == binder::StableBodyOwnerKind::Module) {
    auto moduleKey = StableModuleQueryKey::fromVerified(body.module());
    if (moduleKey == zc::none) { return query::CapabilityRejectionCheck::Rejected; }
    auto provenance =
        context.getCapability<ModuleBodyProvenanceQuery>(ZC_ASSERT_NONNULL(moduleKey));
    if (!provenance.isSourceRejected()) { return query::CapabilityRejectionCheck::Rejected; }
    auto expected = binder::encodeStableBindingDiagnosticFacts(provenance.diagnostics().values());
    return expected != zc::none &&
                   ZC_ASSERT_NONNULL(expected).asPtr() == ZC_ASSERT_NONNULL(actual).asPtr()
               ? query::CapabilityRejectionCheck::Verified
               : query::CapabilityRejectionCheck::Rejected;
  }
  auto definitionKey = ContextualDefinitionKey::from(
      key.contextRoots().clone(),
      binder::StableDefinitionQueryKey::from(body.module().clone(),
                                             ZC_ASSERT_NONNULL(owner.definitionKey()).clone()));
  auto provenance = context.getCapability<NamedItemProvenanceQuery>(definitionKey);
  if (!provenance.isSourceRejected()) { return query::CapabilityRejectionCheck::Rejected; }
  auto expected = binder::encodeStableBindingDiagnosticFacts(provenance.diagnostics().values());
  return expected != zc::none &&
                 ZC_ASSERT_NONNULL(expected).asPtr() == ZC_ASSERT_NONNULL(actual).asPtr()
             ? query::CapabilityRejectionCheck::Verified
             : query::CapabilityRejectionCheck::Rejected;
}

query::CapabilityRejectionCheck verifyOwnerBodyKeyRejection(
    query::CapabilityQueryContext<OwnerBodyProvenanceQuery>& context,
    const OwnerBodyProvenanceQuery::Key& key, const binder::BinderKeyFailure& failure) {
  const auto& body = key.body();
  const auto& owner = body.owner();
  if (owner.kind() == binder::StableBodyOwnerKind::Module) {
    auto moduleKey = StableModuleQueryKey::fromVerified(body.module());
    if (moduleKey == zc::none) { return query::CapabilityRejectionCheck::Rejected; }
    auto provenance =
        context.getCapability<ModuleBodyProvenanceQuery>(ZC_ASSERT_NONNULL(moduleKey));
    return provenance.isKeyRejected() && provenance.keyFailure() == failure
               ? query::CapabilityRejectionCheck::Verified
               : query::CapabilityRejectionCheck::Rejected;
  }
  auto definitionKey = ContextualDefinitionKey::from(
      key.contextRoots().clone(),
      binder::StableDefinitionQueryKey::from(body.module().clone(),
                                             ZC_ASSERT_NONNULL(owner.definitionKey()).clone()));
  auto provenance = context.getCapability<NamedItemProvenanceQuery>(definitionKey);
  if (provenance.isKeyRejected()) {
    return provenance.keyFailure() == failure ? query::CapabilityRejectionCheck::Verified
                                              : query::CapabilityRejectionCheck::Rejected;
  }
  if (!provenance.isPublished()) { return query::CapabilityRejectionCheck::Rejected; }
  auto syntax = context.get<NamedItemSyntaxQuery>(definitionKey);
  if (syntax.isRuntimeFailure() || syntax.kind() != query::QueryValueKind::Value ||
      verifierExecutableRoot(syntax.value().detachedSyntax()) != ExecutableRootAdmission::NoBody) {
    return query::CapabilityRejectionCheck::Rejected;
  }
  auto expected = definitionWithoutBodyFailure(key);
  return expected != zc::none && ZC_ASSERT_NONNULL(expected) == failure
             ? query::CapabilityRejectionCheck::Verified
             : query::CapabilityRejectionCheck::Rejected;
}

}  // namespace zomlang::compiler::driver::incremental_binding_query

namespace zomlang::compiler::binder {
namespace {

template <typename T>
CanonicalSequence<T> emptyFacts() {
  return CanonicalSequence<T>::empty();
}

zc::Maybe<CanonicalNonEmptySequence<diagnostics::DiagnosticFact>> cloneDiagnostics(
    zc::ArrayPtr<const diagnostics::DiagnosticFact> facts) {
  zc::Vector<diagnostics::DiagnosticFact> copies;
  for (const auto& fact : facts) copies.add(fact.clone());
  return StableBindingSequenceBuilder<diagnostics::DiagnosticFact>::fromNonEmpty(zc::mv(copies));
}

query::TypedQueryResult<BindOwnerBody::Value> ownerBodyRuntimeFailure(
    query::QueryRuntimeFailure failure) {
  return query::TypedQueryResult<BindOwnerBody::Value>::runtimeFailure(failure);
}

query::TypedQueryResult<ModuleBindingAllocationPlanQuery::Value> allocationPlanRuntimeFailure(
    query::QueryRuntimeFailure failure) {
  return query::TypedQueryResult<ModuleBindingAllocationPlanQuery::Value>::runtimeFailure(failure);
}

driver::incremental_binding_query::ContextualBodyOwnerKey allocationBodyKey(
    const ModuleBindingAllocationPlanQuery::Key& key, const StableOwnerBodyQueryKey& owner) {
  return driver::incremental_binding_query::ContextualBodyOwnerKey::from(key.contextRoots().clone(),
                                                                         owner.clone());
}

bool sameAllocationModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

}  // namespace

zc::Array<uint8_t> BindOwnerBody::encodeKey(const Key& key) { return key.encodeCanonical(); }

zc::Maybe<BindOwnerBody::Key> BindOwnerBody::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return driver::incremental_binding_query::ContextualBodyOwnerKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> BindOwnerBody::encodeValue(const Value& value) {
  return StableBindingCodec<Value>::encode(value);
}

zc::Maybe<BindOwnerBody::Value> BindOwnerBody::decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
  return StableBindingCodec<Value>::decode(bytes);
}

query::TypedQueryResult<BindOwnerBody::Value> BindOwnerBody::provide(query::QueryContext& context,
                                                                     const Key& key) {
  auto provenance =
      context.getCapability<driver::incremental_binding_query::OwnerBodyProvenanceQuery>(
          key.clone());
  if (provenance.isRuntimeRejected()) {
    return ownerBodyRuntimeFailure(provenance.runtimeFailure());
  }
  if (provenance.isSourceRejected()) {
    auto diagnostics = cloneDiagnostics(provenance.diagnostics().values());
    if (diagnostics == zc::none) {
      return ownerBodyRuntimeFailure(query::QueryRuntimeFailure::InvariantViolation);
    }
    return query::TypedQueryResult<Value>::value(
        Value::sourceRejected(zc::mv(ZC_ASSERT_NONNULL(diagnostics))));
  }
  if (provenance.isKeyRejected()) {
    return query::TypedQueryResult<Value>::value(
        Value::keyRejected(provenance.keyFailure().clone()));
  }
  if (!provenance.isPublished()) {
    return ownerBodyRuntimeFailure(query::QueryRuntimeFailure::InvariantViolation);
  }

  auto skeleton = context.get<BindModuleSkeleton>(key.body().module().clone());
  if (skeleton.isRuntimeFailure()) { return ownerBodyRuntimeFailure(skeleton.runtimeFailure()); }
  if (skeleton.kind() != query::QueryValueKind::Value) {
    return ownerBodyRuntimeFailure(query::QueryRuntimeFailure::InvariantViolation);
  }
  const auto& skeletonResult = skeleton.value().storage();
  if (skeletonResult.is<BinderSourceRejected>()) {
    return query::TypedQueryResult<Value>::value(
        Value::sourceRejected(skeletonResult.get<BinderSourceRejected>().diagnostics.clone()));
  }
  if (skeletonResult.is<BinderKeyRejected>()) {
    return query::TypedQueryResult<Value>::value(
        Value::keyRejected(skeletonResult.get<BinderKeyRejected>().failure.clone()));
  }
  const auto& skeletonValue = skeletonResult.get<BinderQueryValue<BoundModuleSkeleton>>();
  if (skeletonValue.diagnostics.values().size() != 0) {
    return ownerBodyRuntimeFailure(query::QueryRuntimeFailure::InvariantViolation);
  }

  auto syntax = context.get<driver::incremental_binding_query::OwnerBodySyntaxQuery>(key.clone());
  if (syntax.isRuntimeFailure()) { return ownerBodyRuntimeFailure(syntax.runtimeFailure()); }
  if (syntax.kind() != query::QueryValueKind::Value) {
    return ownerBodyRuntimeFailure(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto scopes = OwnerBodyScopeProjection::fromSkeleton(key.body(), syntax.value().detachedSyntax(),
                                                       skeletonValue.value);
  if (scopes == zc::none) {
    return ownerBodyRuntimeFailure(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto bindings = OwnerBodyBindingProjection::from(key.body(), syntax.value().detachedSyntax(),
                                                   ZC_ASSERT_NONNULL(scopes).nodeScopes());
  if (bindings == zc::none) {
    return ownerBodyRuntimeFailure(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto shadows = OwnerBodyShadowProjection::from(key.body(), ZC_ASSERT_NONNULL(scopes).scopes(),
                                                 ZC_ASSERT_NONNULL(bindings).bindings());
  if (shadows == zc::none) {
    return ownerBodyRuntimeFailure(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto lookups = OwnerBodyLookupProjection::from(
      key.body(), syntax.value().detachedSyntax(), skeletonValue.value,
      ZC_ASSERT_NONNULL(scopes).scopes(), ZC_ASSERT_NONNULL(scopes).nodeScopes(),
      ZC_ASSERT_NONNULL(bindings).bindings());
  auto selfTypes = OwnerBodySelfTypeProjection::from(key.body(), syntax.value().detachedSyntax(),
                                                     skeletonValue.value);
  auto receivers = OwnerBodyReceiverProjection::from(key.body(), syntax.value().detachedSyntax(),
                                                     skeletonValue.value);
  auto deferredMembers =
      OwnerBodyDeferredMemberProjection::from(key.body(), syntax.value().detachedSyntax());
  auto closures = OwnerBodyClosureProjection::from(key.body(), syntax.value().detachedSyntax(),
                                                   ZC_ASSERT_NONNULL(scopes).nodeScopes());
  auto labels = OwnerBodyLabelProjection::from(key.body(), syntax.value().detachedSyntax(),
                                               ZC_ASSERT_NONNULL(scopes).nodeScopes());
  if (lookups == zc::none || selfTypes == zc::none || receivers == zc::none ||
      deferredMembers == zc::none || closures == zc::none || labels == zc::none) {
    return ownerBodyRuntimeFailure(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto freeVariables = OwnerBodyFreeVariableProjection::from(
      key.body(), skeletonValue.value, ZC_ASSERT_NONNULL(scopes).scopes(),
      ZC_ASSERT_NONNULL(bindings).bindings(), ZC_ASSERT_NONNULL(closures).closures(),
      ZC_ASSERT_NONNULL(lookups).resolutions());
  auto explicitCaptures = OwnerBodyExplicitCaptureProjection::from(
      key.body(), syntax.value().detachedSyntax(), skeletonValue.value,
      ZC_ASSERT_NONNULL(scopes).scopes(), ZC_ASSERT_NONNULL(scopes).nodeScopes(),
      ZC_ASSERT_NONNULL(bindings).bindings(), ZC_ASSERT_NONNULL(closures).closures());
  if (freeVariables == zc::none || explicitCaptures == zc::none) {
    return ownerBodyRuntimeFailure(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto controls = OwnerBodyControlProjection::from(key.body(), syntax.value().detachedSyntax(),
                                                   ZC_ASSERT_NONNULL(scopes).nodeScopes(),
                                                   ZC_ASSERT_NONNULL(labels).labels());
  if (controls == zc::none) {
    return ownerBodyRuntimeFailure(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto value = BoundOwnerBody::from(
      key.body().clone(), ZC_ASSERT_NONNULL(scopes).scopes().clone(),
      ZC_ASSERT_NONNULL(scopes).nodeScopes().clone(),
      ZC_ASSERT_NONNULL(bindings).bindings().clone(),
      ZC_ASSERT_NONNULL(lookups).resolutions().clone(),
      ZC_ASSERT_NONNULL(deferredMembers).deferredMembers().clone(),
      ZC_ASSERT_NONNULL(selfTypes).selfTypes().clone(),
      ZC_ASSERT_NONNULL(receivers).bindings().clone(), ZC_ASSERT_NONNULL(shadows).shadows().clone(),
      ZC_ASSERT_NONNULL(labels).labels().clone(), ZC_ASSERT_NONNULL(controls).transfers().clone(),
      ZC_ASSERT_NONNULL(closures).closures().clone(),
      ZC_ASSERT_NONNULL(freeVariables).freeVariables().clone(),
      ZC_ASSERT_NONNULL(explicitCaptures).captures().clone(),
      ZC_ASSERT_NONNULL(lookups).failedLookups().clone());
  if (value == zc::none) {
    return ownerBodyRuntimeFailure(query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(
      Value::value(zc::mv(ZC_ASSERT_NONNULL(value)), emptyFacts<diagnostics::DiagnosticFact>()));
}

bool BindOwnerBody::verify(query::QueryContext& context, const Key& key,
                           const query::TypedQueryResult<Value>& result) {
  auto provenance =
      context.getCapability<driver::incremental_binding_query::OwnerBodyProvenanceQuery>(
          key.clone());
  if (!provenance.isPublished()) {
    if (provenance.isSourceRejected()) {
      auto expected = cloneDiagnostics(provenance.diagnostics().values());
      return result.kind() == query::QueryValueKind::Value &&
             result.value().storage().is<BinderSourceRejected>() && expected != zc::none &&
             result.value().storage().get<BinderSourceRejected>().diagnostics ==
                 ZC_ASSERT_NONNULL(expected);
    }
    if (provenance.isKeyRejected()) {
      return result.kind() == query::QueryValueKind::Value &&
             result.value().storage().is<BinderKeyRejected>() &&
             result.value().storage().get<BinderKeyRejected>().failure == provenance.keyFailure();
    }
    return false;
  }
  auto skeleton = context.get<BindModuleSkeleton>(key.body().module().clone());
  if (skeleton.isRuntimeFailure() || skeleton.kind() != query::QueryValueKind::Value) return false;
  const auto& skeletonResult = skeleton.value().storage();
  if (skeletonResult.is<BinderSourceRejected>()) {
    return result.kind() == query::QueryValueKind::Value &&
           result.value().storage().is<BinderSourceRejected>() &&
           result.value().storage().get<BinderSourceRejected>().diagnostics ==
               skeletonResult.get<BinderSourceRejected>().diagnostics;
  }
  if (skeletonResult.is<BinderKeyRejected>()) {
    return result.kind() == query::QueryValueKind::Value &&
           result.value().storage().is<BinderKeyRejected>() &&
           result.value().storage().get<BinderKeyRejected>().failure ==
               skeletonResult.get<BinderKeyRejected>().failure;
  }
  const auto& skeletonValue = skeletonResult.get<BinderQueryValue<BoundModuleSkeleton>>();
  auto syntax = context.get<driver::incremental_binding_query::OwnerBodySyntaxQuery>(key.clone());
  if (syntax.isRuntimeFailure() || syntax.kind() != query::QueryValueKind::Value ||
      result.kind() != query::QueryValueKind::Value ||
      !result.value().storage().is<BinderQueryValue<BoundOwnerBody>>()) {
    return false;
  }
  const auto& value = result.value().storage().get<BinderQueryValue<BoundOwnerBody>>();
  const auto& body = value.value;
  if (skeletonValue.diagnostics.values().size() != 0 || value.diagnostics.values().size() != 0 ||
      body.owner() != key.body() ||
      !OwnerBodyScopeProjection::verifyFromSkeleton(key.body(), syntax.value().detachedSyntax(),
                                                    skeletonValue.value, body.scopes(),
                                                    body.nodeScopes()) ||
      !OwnerBodyBindingProjection::verify(key.body(), syntax.value().detachedSyntax(),
                                          body.nodeScopes(), body.bindings()) ||
      !OwnerBodyShadowProjection::verify(key.body(), body.scopes(), body.bindings(),
                                         body.shadowTargets()) ||
      !OwnerBodyLookupProjection::verify(
          key.body(), syntax.value().detachedSyntax(), skeletonValue.value, body.scopes(),
          body.nodeScopes(), body.bindings(), body.resolutions(), body.failedLookups()) ||
      !OwnerBodySelfTypeProjection::verify(key.body(), syntax.value().detachedSyntax(),
                                           skeletonValue.value, body.selfTypes()) ||
      !OwnerBodyReceiverProjection::verify(key.body(), syntax.value().detachedSyntax(),
                                           skeletonValue.value, body.thisBindings()) ||
      !OwnerBodyDeferredMemberProjection::verify(key.body(), syntax.value().detachedSyntax(),
                                                 body.deferredMembers()) ||
      !OwnerBodyClosureProjection::verify(key.body(), syntax.value().detachedSyntax(),
                                          body.nodeScopes(), body.closures()) ||
      !OwnerBodyFreeVariableProjection::verify(key.body(), skeletonValue.value, body.scopes(),
                                               body.bindings(), body.closures(), body.resolutions(),
                                               body.closureFreeVariables()) ||
      !OwnerBodyLabelProjection::verify(key.body(), syntax.value().detachedSyntax(),
                                        body.nodeScopes(), body.labels()) ||
      !OwnerBodyControlProjection::verify(key.body(), syntax.value().detachedSyntax(),
                                          body.nodeScopes(), body.labels(),
                                          body.controlTransfers())) {
    return false;
  }
  return OwnerBodyExplicitCaptureProjection::verify(
      key.body(), syntax.value().detachedSyntax(), skeletonValue.value, body.scopes(),
      body.nodeScopes(), body.bindings(), body.closures(), body.explicitClosureCaptures());
}

zc::Array<uint8_t> ModuleBindingAllocationPlanQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<ModuleBindingAllocationPlanQuery::Key> ModuleBindingAllocationPlanQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return driver::incremental_binding_query::ContextualModuleKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> ModuleBindingAllocationPlanQuery::encodeValue(const Value& value) {
  return StableBindingCodec<Value>::encode(value);
}

zc::Maybe<ModuleBindingAllocationPlanQuery::Value> ModuleBindingAllocationPlanQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return StableBindingCodec<Value>::decode(bytes);
}

query::TypedQueryResult<ModuleBindingAllocationPlanQuery::Value>
ModuleBindingAllocationPlanQuery::provide(query::QueryContext& context, const Key& key) {
  auto skeleton = context.get<BindModuleSkeleton>(key.module().clone());
  if (skeleton.isRuntimeFailure()) {
    return allocationPlanRuntimeFailure(skeleton.runtimeFailure());
  }
  if (skeleton.kind() != query::QueryValueKind::Value) {
    return allocationPlanRuntimeFailure(query::QueryRuntimeFailure::InvariantViolation);
  }
  const auto& skeletonResult = skeleton.value().storage();
  if (skeletonResult.is<BinderSourceRejected>()) {
    return query::TypedQueryResult<Value>::value(
        Value::sourceRejected(skeletonResult.get<BinderSourceRejected>().diagnostics.clone()));
  }
  if (skeletonResult.is<BinderKeyRejected>()) {
    return query::TypedQueryResult<Value>::value(
        Value::keyRejected(skeletonResult.get<BinderKeyRejected>().failure.clone()));
  }
  const auto& skeletonValue = skeletonResult.get<BinderQueryValue<BoundModuleSkeleton>>();
  if (skeletonValue.diagnostics.values().size() != 0 ||
      !sameAllocationModule(skeletonValue.value.module(), key.module())) {
    return allocationPlanRuntimeFailure(query::QueryRuntimeFailure::InvariantViolation);
  }

  zc::Vector<BoundOwnerBody> bodies;
  for (const auto& owner : skeletonValue.value.bodyOwners().values()) {
    auto body = context.get<BindOwnerBody>(allocationBodyKey(key, owner));
    if (body.isRuntimeFailure()) { return allocationPlanRuntimeFailure(body.runtimeFailure()); }
    if (body.kind() != query::QueryValueKind::Value) {
      return allocationPlanRuntimeFailure(query::QueryRuntimeFailure::InvariantViolation);
    }
    const auto& bodyResult = body.value().storage();
    if (bodyResult.is<BinderSourceRejected>()) {
      return query::TypedQueryResult<Value>::value(
          Value::sourceRejected(bodyResult.get<BinderSourceRejected>().diagnostics.clone()));
    }
    if (bodyResult.is<BinderKeyRejected>()) {
      return query::TypedQueryResult<Value>::value(
          Value::keyRejected(bodyResult.get<BinderKeyRejected>().failure.clone()));
    }
    const auto& bodyValue = bodyResult.get<BinderQueryValue<BoundOwnerBody>>();
    if (bodyValue.diagnostics.values().size() != 0 || bodyValue.value.owner() != owner) {
      return allocationPlanRuntimeFailure(query::QueryRuntimeFailure::InvariantViolation);
    }
    bodies.add(bodyValue.value.clone());
  }
  auto plan = ModuleBindingAllocationPlanner::from(skeletonValue.value, bodies.asPtr().asConst());
  if (plan == zc::none) {
    return allocationPlanRuntimeFailure(query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(
      Value::value(zc::mv(ZC_ASSERT_NONNULL(plan)), emptyFacts<diagnostics::DiagnosticFact>()));
}

bool ModuleBindingAllocationPlanQuery::verify(query::QueryContext& context, const Key& key,
                                              const query::TypedQueryResult<Value>& result) {
  auto skeleton = context.get<BindModuleSkeleton>(key.module().clone());
  if (skeleton.isRuntimeFailure() || skeleton.kind() != query::QueryValueKind::Value) return false;
  const auto& skeletonResult = skeleton.value().storage();
  if (skeletonResult.is<BinderSourceRejected>()) {
    return result.kind() == query::QueryValueKind::Value &&
           result.value().storage().is<BinderSourceRejected>() &&
           result.value().storage().get<BinderSourceRejected>().diagnostics ==
               skeletonResult.get<BinderSourceRejected>().diagnostics;
  }
  if (skeletonResult.is<BinderKeyRejected>()) {
    return result.kind() == query::QueryValueKind::Value &&
           result.value().storage().is<BinderKeyRejected>() &&
           result.value().storage().get<BinderKeyRejected>().failure ==
               skeletonResult.get<BinderKeyRejected>().failure;
  }
  const auto& skeletonValue = skeletonResult.get<BinderQueryValue<BoundModuleSkeleton>>();
  if (skeletonValue.diagnostics.values().size() != 0 ||
      !sameAllocationModule(skeletonValue.value.module(), key.module())) {
    return false;
  }

  zc::Vector<BoundOwnerBody> bodies;
  for (const auto& owner : skeletonValue.value.bodyOwners().values()) {
    auto body = context.get<BindOwnerBody>(allocationBodyKey(key, owner));
    if (body.isRuntimeFailure() || body.kind() != query::QueryValueKind::Value) return false;
    const auto& bodyResult = body.value().storage();
    if (bodyResult.is<BinderSourceRejected>()) {
      return result.kind() == query::QueryValueKind::Value &&
             result.value().storage().is<BinderSourceRejected>() &&
             result.value().storage().get<BinderSourceRejected>().diagnostics ==
                 bodyResult.get<BinderSourceRejected>().diagnostics;
    }
    if (bodyResult.is<BinderKeyRejected>()) {
      return result.kind() == query::QueryValueKind::Value &&
             result.value().storage().is<BinderKeyRejected>() &&
             result.value().storage().get<BinderKeyRejected>().failure ==
                 bodyResult.get<BinderKeyRejected>().failure;
    }
    const auto& bodyValue = bodyResult.get<BinderQueryValue<BoundOwnerBody>>();
    if (bodyValue.diagnostics.values().size() != 0 || bodyValue.value.owner() != owner)
      return false;
    bodies.add(bodyValue.value.clone());
  }
  if (result.kind() != query::QueryValueKind::Value ||
      !result.value().storage().is<BinderQueryValue<ModuleBindingAllocationPlan>>()) {
    return false;
  }
  const auto& value = result.value().storage().get<BinderQueryValue<ModuleBindingAllocationPlan>>();
  return value.diagnostics.values().size() == 0 &&
         ModuleBindingAllocationPlanner::verify(skeletonValue.value, bodies.asPtr().asConst(),
                                                value.value);
}

}  // namespace zomlang::compiler::binder

namespace zomlang::compiler::query {

using OwnerBodyProvenanceDescriptor = driver::incremental_binding_query::OwnerBodyProvenanceQuery;

StableWitnessBytes CapabilityCandidateContract<OwnerBodyProvenanceDescriptor>::encode(
    const OwnerBodyProvenanceDescriptor::Capability& candidate) {
  return StableWitnessBytes(candidate.encodeCanonical());
}

zc::Maybe<zc::Own<OwnerBodyProvenanceDescriptor::Capability>> CapabilityCandidateContract<
    OwnerBodyProvenanceDescriptor>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  auto candidate = binder::OwnerBodyProvenance::decodeCanonical(bytes);
  if (candidate == zc::none || ZC_ASSERT_NONNULL(candidate).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::heap<OwnerBodyProvenanceDescriptor::Capability>(zc::mv(ZC_ASSERT_NONNULL(candidate)));
}

zc::Array<uint8_t> CapabilityFailureContract<
    OwnerBodyProvenanceDescriptor,
    SourceRejection<diagnostics::DiagnosticFact>>::encode(const Sequence& diagnostics) {
  auto encoded = binder::encodeStableBindingDiagnosticFacts(diagnostics.values());
  return zc::mv(ZC_ASSERT_NONNULL(encoded));
}

zc::Maybe<CapabilityFailureContract<OwnerBodyProvenanceDescriptor,
                                    SourceRejection<diagnostics::DiagnosticFact>>::Sequence>
CapabilityFailureContract<
    OwnerBodyProvenanceDescriptor,
    SourceRejection<diagnostics::DiagnosticFact>>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  auto facts = binder::decodeStableBindingDiagnosticFacts(bytes);
  if (facts == zc::none) { return zc::none; }
  return Sequence(zc::mv(ZC_ASSERT_NONNULL(facts)));
}

CapabilityRejectionCheck CapabilityFailureContract<OwnerBodyProvenanceDescriptor,
                                                   SourceRejection<diagnostics::DiagnosticFact>>::
    verify(CapabilityQueryContext<OwnerBodyProvenanceDescriptor>& context,
           const OwnerBodyProvenanceDescriptor::Key& key, const Sequence& diagnostics) {
  return driver::incremental_binding_query::verifyOwnerBodySourceRejection(context, key,
                                                                           diagnostics.values());
}

zc::Array<uint8_t> CapabilityFailureContract<
    OwnerBodyProvenanceDescriptor,
    KeyRejection<binder::BinderKeyFailure>>::encode(const binder::BinderKeyFailure& failure) {
  return binder::StableBindingCodec<binder::BinderKeyFailure>::encode(failure);
}

zc::Maybe<binder::BinderKeyFailure> CapabilityFailureContract<
    OwnerBodyProvenanceDescriptor,
    KeyRejection<binder::BinderKeyFailure>>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  return binder::StableBindingCodec<binder::BinderKeyFailure>::decode(bytes);
}

CapabilityRejectionCheck
CapabilityFailureContract<OwnerBodyProvenanceDescriptor, KeyRejection<binder::BinderKeyFailure>>::
    verify(CapabilityQueryContext<OwnerBodyProvenanceDescriptor>& context,
           const OwnerBodyProvenanceDescriptor::Key& key, const binder::BinderKeyFailure& failure) {
  return driver::incremental_binding_query::verifyOwnerBodyKeyRejection(context, key, failure);
}

}  // namespace zomlang::compiler::query
