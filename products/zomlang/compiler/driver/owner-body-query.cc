#include "zomlang/compiler/driver/owner-body-query.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/ast/generated/node-accessors.h"
#include "zomlang/compiler/ast/generated/node-schema.h"
#include "zomlang/compiler/driver/named-identity-inventory-query.h"
#include "zomlang/compiler/driver/named-item-query.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

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

query::QueryKindContract retainedSemanticContract(zc::StringPtr domain) {
  auto contract = query::QueryKindContract::derived(domain, query::ReuseClass::Semantic,
                                                    query::RetentionClass::Retained);
  return zc::mv(ZC_REQUIRE_NONNULL(contract));
}

query::QueryKindContract evictableSemanticContract(zc::StringPtr domain) {
  auto contract = query::QueryKindContract::derived(domain, query::ReuseClass::Semantic,
                                                    query::RetentionClass::Evictable);
  return zc::mv(ZC_REQUIRE_NONNULL(contract));
}

query::QueryKindContract revisionLocalContract(zc::StringPtr domain) {
  auto contract = query::QueryKindContract::derived(domain, query::ReuseClass::RevisionLocal,
                                                    query::RetentionClass::Retained);
  return zc::mv(ZC_REQUIRE_NONNULL(contract));
}

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

bool skipProviderScalar(identity::CanonicalDecoder& decoder, const ast::NodeSchemaFieldEntry& field,
                        uint32_t& childOrdinal, zc::Maybe<DetachedFieldProjection>& selected,
                        zc::StringPtr target) {
  auto storage = decoder.decodeUint8();
  auto optional = decoder.decodeBool();
  if (storage == zc::none || optional == zc::none ||
      ZC_ASSERT_NONNULL(storage) != static_cast<uint8_t>(field.storage) + 1 ||
      ZC_ASSERT_NONNULL(optional) != field.optional) {
    return false;
  }
  const bool isTarget = zc::StringPtr(field.name) == target;
  switch (field.storage) {
    case ast::NodeSchemaFieldStorage::NodeId: {
      auto present = decoder.decodeBool();
      if (present == zc::none || (!field.optional && !ZC_ASSERT_NONNULL(present))) { return false; }
      if (isTarget) {
        selected = DetachedFieldProjection{ZC_ASSERT_NONNULL(present), childOrdinal};
      }
      if (ZC_ASSERT_NONNULL(present)) { ++childOrdinal; }
      return true;
    }
    case ast::NodeSchemaFieldStorage::NodeList: {
      auto count = decoder.decodeSequenceSize(kMaximumDetachedNodes);
      if (count == zc::none || ZC_ASSERT_NONNULL(count) > UINT32_MAX - childOrdinal) {
        return false;
      }
      if (isTarget) { return false; }
      childOrdinal += static_cast<uint32_t>(ZC_ASSERT_NONNULL(count));
      return true;
    }
    case ast::NodeSchemaFieldStorage::IdentList: {
      auto count = decoder.decodeSequenceSize(kMaximumIdentifierList);
      if (count == zc::none || isTarget) { return false; }
      ZC_IF_SOME(value, count) {
        for (uint64_t index = 0; index < value; ++index) {
          if (decoder.decodeByteString(kMaximumScalarBytes) == zc::none) { return false; }
        }
      }
      return true;
    }
    case ast::NodeSchemaFieldStorage::StringId:
    case ast::NodeSchemaFieldStorage::IdentId:
    case ast::NodeSchemaFieldStorage::BigIntId:
    case ast::NodeSchemaFieldStorage::FloatId: {
      auto present = decoder.decodeBool();
      if (present == zc::none || (!field.optional && !ZC_ASSERT_NONNULL(present)) || isTarget) {
        return false;
      }
      return !ZC_ASSERT_NONNULL(present) ||
             decoder.decodeByteString(kMaximumScalarBytes) != zc::none;
    }
    case ast::NodeSchemaFieldStorage::Bool:
      return !isTarget && decoder.decodeBool() != zc::none;
    case ast::NodeSchemaFieldStorage::UInt8:
      return !isTarget && decoder.decodeUint8() != zc::none;
    case ast::NodeSchemaFieldStorage::UInt16:
    case ast::NodeSchemaFieldStorage::UInt32:
    case ast::NodeSchemaFieldStorage::Enum:
      return !isTarget && decoder.decodeUint32() != zc::none;
    case ast::NodeSchemaFieldStorage::UInt64:
      return !isTarget && decoder.decodeUint64() != zc::none;
  }
  return false;
}

zc::Maybe<DetachedFieldProjection> providerField(const binder::DetachedModuleBodyNode& node,
                                                 zc::StringPtr fieldName) {
  if (node.kind() != binder::DetachedModuleBodyNodeKind::Syntax) { return zc::none; }
  auto schema = ast::lookupNodeSchema(ZC_ASSERT_NONNULL(node.syntaxKind()));
  if (schema == nullptr) { return zc::none; }
  identity::CanonicalDecoder decoder(node.canonicalPayload());
  auto count = decoder.decodeSequenceSize(schema->fieldCount);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) != schema->fieldCount) { return zc::none; }
  uint32_t childOrdinal = 0;
  zc::Maybe<DetachedFieldProjection> selected;
  for (uint32_t index = 0; index < schema->fieldCount; ++index) {
    if (!skipProviderScalar(decoder, schema->fields[index], childOrdinal, selected, fieldName)) {
      return zc::none;
    }
  }
  if (!decoder.finished() || childOrdinal != node.childCount()) { return zc::none; }
  return selected;
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

ExecutableRootAdmission providerExecutableRoot(const binder::ModuleBodySyntax& syntax) {
  if (syntax.rootCount() != 1 || syntax.nodes().size() == 0 ||
      syntax.nodes()[0].kind() != binder::DetachedModuleBodyNodeKind::Syntax) {
    return ExecutableRootAdmission::Malformed;
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
      return ExecutableRootAdmission::NoBody;
  }
  auto field = providerField(syntax.nodes()[0], fieldName);
  if (field == zc::none) { return ExecutableRootAdmission::Malformed; }
  if (!ZC_ASSERT_NONNULL(field).present) {
    return optional ? ExecutableRootAdmission::NoBody : ExecutableRootAdmission::Malformed;
  }
  return validExecutableChild(syntax, ZC_ASSERT_NONNULL(field), requiresBlock)
             ? ExecutableRootAdmission::Executable
             : ExecutableRootAdmission::Malformed;
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

ExecutableRootAdmission verifierExecutableRoot(const binder::ModuleBodySyntax& syntax) {
  if (syntax.rootCount() != 1 || syntax.nodes().size() == 0) {
    return ExecutableRootAdmission::Malformed;
  }
  const auto& root = syntax.nodes()[0];
  if (root.kind() != binder::DetachedModuleBodyNodeKind::Syntax) {
    return ExecutableRootAdmission::Malformed;
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
      return ExecutableRootAdmission::NoBody;
  }
  auto selected = verifierFieldAt(root, fieldIndex);
  if (selected == zc::none) { return ExecutableRootAdmission::Malformed; }
  if (!ZC_ASSERT_NONNULL(selected).present) {
    return required ? ExecutableRootAdmission::Malformed : ExecutableRootAdmission::NoBody;
  }
  return validExecutableChild(syntax, ZC_ASSERT_NONNULL(selected), block)
             ? ExecutableRootAdmission::Executable
             : ExecutableRootAdmission::Malformed;
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

template <typename Capability, typename UpstreamResult>
query::CapabilityProviderResult<Capability> propagateCapability(const UpstreamResult& upstream) {
  if (upstream.isRuntimeFailure()) {
    return query::CapabilityProviderResult<Capability>::runtimeFailure(upstream.runtimeFailure());
  }
  if (upstream.kind() == query::QueryValueKind::SemanticFailure) {
    return query::CapabilityProviderResult<Capability>::semanticFailure(
        zc::heapArray<uint8_t>(upstream.semanticFailureBytes()));
  }
  return query::CapabilityProviderResult<Capability>::runtimeFailure(
      query::QueryRuntimeFailure::InvariantViolation);
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
  auto syntaxItems = context.getParallel<NamedItemSyntaxQuery>(keys.asPtr());
  if (syntaxItems.size() != keys.size()) {
    return query::TypedQueryResult<binder::ModuleBodyOwners>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  zc::Vector<binder::StableBodyOwnerKey> owners(keys.size() + 1);
  owners.add(binder::StableBodyOwnerKey::module(ZC_ASSERT_NONNULL(module).clone()));
  for (size_t index = 0; index < syntaxItems.size(); ++index) {
    const auto& syntax = syntaxItems[index];
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
  auto syntaxItems = context.getParallel<NamedItemSyntaxQuery>(keys.asPtr());
  if (syntaxItems.size() != keys.size()) { return false; }
  zc::Vector<binder::StableBodyOwnerKey> expectedOwners(keys.size() + 1);
  expectedOwners.add(binder::StableBodyOwnerKey::module(ZC_ASSERT_NONNULL(module).clone()));
  for (size_t index = 0; index < syntaxItems.size(); ++index) {
    const auto& syntax = syntaxItems[index];
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

zc::StringPtr ModuleBodyOwnersQuery::domain() { return "zom.query.module-body-owners"_zc; }

query::QueryKindContract ModuleBodyOwnersQuery::contract() {
  return retainedSemanticContract(domain());
}

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

zc::StringPtr OwnerBodySyntaxQuery::domain() { return "zom.query.owner-body-syntax"_zc; }

query::QueryKindContract OwnerBodySyntaxQuery::contract() {
  return evictableSemanticContract(domain());
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
  const auto admission = providerExecutableRoot(syntax.value().detachedSyntax());
  if (admission == ExecutableRootAdmission::NoBody) {
    return query::TypedQueryResult<Value>::semanticFailure(
        encodeFailure(OwnerBodyFailureKind::DefinitionWithoutBody));
  }
  if (admission == ExecutableRootAdmission::Malformed) {
    return query::TypedQueryResult<Value>::semanticFailure(
        encodeFailure(OwnerBodyFailureKind::MalformedDetachedSyntax));
  }
  auto value = binder::OwnerBodySyntax::from(owner.clone(), body.module().clone(),
                                             syntax.value().detachedSyntax().clone());
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
  const auto admission = verifierExecutableRoot(syntax.value().detachedSyntax());
  if (admission != ExecutableRootAdmission::Executable) {
    const auto failure = admission == ExecutableRootAdmission::NoBody
                             ? OwnerBodyFailureKind::DefinitionWithoutBody
                             : OwnerBodyFailureKind::MalformedDetachedSyntax;
    auto expectedFailure = encodeFailure(failure);
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == expectedFailure.asPtr();
  }
  auto expected = binder::OwnerBodySyntax::from(owner.clone(), body.module().clone(),
                                                syntax.value().detachedSyntax().clone());
  return result.kind() == query::QueryValueKind::Value && expected != zc::none &&
         ZC_ASSERT_NONNULL(expected) == result.value();
}

zc::StringPtr OwnerBodyProvenanceQuery::domain() { return "zom.query.owner-body-provenance"_zc; }

query::QueryKindContract OwnerBodyProvenanceQuery::contract() {
  return revisionLocalContract(domain());
}

zc::Array<uint8_t> OwnerBodyProvenanceQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<OwnerBodyProvenanceQuery::Key> OwnerBodyProvenanceQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualBodyOwnerKey::decodeCanonical(bytes);
}

query::CapabilityProviderResult<OwnerBodyProvenanceQuery::Capability>
OwnerBodyProvenanceQuery::provide(query::CapabilityQueryContext& context, const Key& key) {
  auto syntax = context.get<OwnerBodySyntaxQuery>(key);
  if (syntax.kind() != query::QueryValueKind::Value || syntax.isRuntimeFailure()) {
    return propagateCapability<Capability>(syntax);
  }
  const auto& body = key.body();
  const auto& owner = body.owner();
  zc::Maybe<binder::ModuleBodyProvenance> retained;
  if (owner.kind() == binder::StableBodyOwnerKind::Module) {
    auto moduleKey = StableModuleQueryKey::fromVerified(body.module());
    if (moduleKey == zc::none) {
      return query::CapabilityProviderResult<Capability>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    auto provenance =
        context.getCapability<ModuleBodyProvenanceQuery>(ZC_ASSERT_NONNULL(moduleKey));
    if (provenance.kind() != query::QueryValueKind::Value || provenance.isRuntimeFailure()) {
      return propagateCapability<Capability>(provenance);
    }
    retained = provenance.value().capability().clone();
  } else {
    auto definitionKey = ContextualDefinitionKey::from(
        key.contextRoots().clone(),
        binder::StableDefinitionQueryKey::from(body.module().clone(),
                                               ZC_ASSERT_NONNULL(owner.definitionKey()).clone()));
    auto provenance = context.getCapability<NamedItemProvenanceQuery>(definitionKey);
    if (provenance.kind() != query::QueryValueKind::Value || provenance.isRuntimeFailure()) {
      return propagateCapability<Capability>(provenance);
    }
    retained = provenance.value().capability().detachedProvenance().clone();
  }
  if (retained == zc::none ||
      !providerProvenanceMatches(syntax.value(), ZC_ASSERT_NONNULL(retained))) {
    return query::CapabilityProviderResult<Capability>::semanticFailure(
        encodeFailure(OwnerBodyFailureKind::MissingProvenance));
  }
  auto value =
      binder::OwnerBodyProvenance::from(owner.clone(), zc::mv(ZC_ASSERT_NONNULL(retained)));
  if (value == zc::none) {
    return query::CapabilityProviderResult<Capability>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto stableWitness = ZC_ASSERT_NONNULL(value).encodeCanonical();
  return query::CapabilityProviderResult<Capability>::value(
      zc::heap<Capability>(zc::mv(ZC_ASSERT_NONNULL(value))), zc::mv(stableWitness));
}

zc::Maybe<zc::Array<uint8_t>> OwnerBodyProvenanceQuery::verify(
    query::CapabilityQueryContext& context, const Key& key, const Capability& candidate) {
  auto syntax = context.get<OwnerBodySyntaxQuery>(key);
  if (syntax.isRuntimeFailure() || syntax.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  const auto& body = key.body();
  const auto& owner = body.owner();
  zc::Maybe<binder::ModuleBodyProvenance> retained;
  if (owner.kind() == binder::StableBodyOwnerKind::Module) {
    auto moduleKey = StableModuleQueryKey::fromVerified(body.module());
    if (moduleKey == zc::none) { return zc::none; }
    auto provenance =
        context.getCapability<ModuleBodyProvenanceQuery>(ZC_ASSERT_NONNULL(moduleKey));
    if (provenance.isRuntimeFailure() || provenance.kind() != query::QueryValueKind::Value) {
      return zc::none;
    }
    retained = provenance.value().capability().clone();
  } else {
    auto definitionKey = ContextualDefinitionKey::from(
        key.contextRoots().clone(),
        binder::StableDefinitionQueryKey::from(body.module().clone(),
                                               ZC_ASSERT_NONNULL(owner.definitionKey()).clone()));
    auto provenance = context.getCapability<NamedItemProvenanceQuery>(definitionKey);
    if (provenance.isRuntimeFailure() || provenance.kind() != query::QueryValueKind::Value) {
      return zc::none;
    }
    retained = provenance.value().capability().detachedProvenance().clone();
  }
  if (retained == zc::none ||
      !verifierProvenanceMatches(syntax.value(), ZC_ASSERT_NONNULL(retained))) {
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

}  // namespace zomlang::compiler::driver::incremental_binding_query
