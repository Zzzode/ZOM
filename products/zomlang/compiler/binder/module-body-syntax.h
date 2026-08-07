// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/binder/identity-pre-admission.h"
#include "zomlang/compiler/binder/local-identity.h"
#include "zomlang/compiler/binder/parsed-module.h"
#include "zomlang/compiler/identity/definition-key.h"
#include "zomlang/compiler/identity/source-key.h"

namespace zomlang::compiler::binder {

namespace module_body_syntax_detail {
struct DetachedModuleBodyNodeData;
struct ModuleBodySyntaxData;
struct ModuleBodyProvenanceData;
}  // namespace module_body_syntax_detail

/// \brief Closed detached node alternatives admitted into one semantic module body tree.
enum class DetachedModuleBodyNodeKind : uint8_t { Syntax = 0x01, DefinitionBoundary = 0x02 };

/// \brief One schema child field mapped into detached preorder child ordinals.
struct DetachedModuleBodyChildField final {
  bool present;
  uint32_t firstChildOrdinal;
  uint32_t childCount;
};

/// \brief One handle-free canonical syntax record or stable-item boundary leaf.
class DetachedModuleBodyNode final {
public:
  ~DetachedModuleBodyNode() noexcept(false);
  DetachedModuleBodyNode(DetachedModuleBodyNode&&) noexcept;
  DetachedModuleBodyNode& operator=(DetachedModuleBodyNode&&) noexcept;
  ZC_DISALLOW_COPY(DetachedModuleBodyNode);

  ZC_NODISCARD DetachedModuleBodyNode clone() const;
  /// \brief Admits one schema-complete detached syntax record.
  ZC_NODISCARD static zc::Maybe<DetachedModuleBodyNode> syntax(ast::SyntaxKind kind,
                                                               zc::Array<uint8_t>&& canonicalFields,
                                                               uint32_t childCount);
  /// \brief Constructs one stable definition boundary leaf.
  ZC_NODISCARD static DetachedModuleBodyNode definitionBoundary(const identity::DefinitionKey& key);
  ZC_NODISCARD DetachedModuleBodyNodeKind kind() const noexcept;
  ZC_NODISCARD zc::Maybe<ast::SyntaxKind> syntaxKind() const noexcept;
  /// \brief Returns the schema-ordered scalar and child-cardinality record for syntax nodes.
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalPayload() const ZC_LIFETIMEBOUND;
  /// \brief Returns the number of immediate detached children in preorder storage.
  ZC_NODISCARD uint32_t childCount() const noexcept;
  /// \brief Maps one schema node field to its immediate detached child ordinals.
  ZC_NODISCARD zc::Maybe<DetachedModuleBodyChildField> childField(uint32_t fieldIndex) const;
  /// \brief Decodes one canonical identifier scalar from an identifier schema field.
  ZC_NODISCARD zc::Maybe<identity::DeclaredDefinitionName> identifierField(
      uint32_t fieldIndex) const;
  /// \brief Decodes one canonical identifier sequence from an identifier-list schema field.
  ZC_NODISCARD zc::Maybe<zc::Vector<identity::DeclaredDefinitionName>> identifierListField(
      uint32_t fieldIndex) const;
  bool operator==(const DetachedModuleBodyNode& other) const noexcept;
  bool operator!=(const DetachedModuleBodyNode& other) const noexcept { return !(*this == other); }

private:
  explicit DetachedModuleBodyNode(
      zc::Own<module_body_syntax_detail::DetachedModuleBodyNodeData>&& impl) noexcept;

  zc::Own<module_body_syntax_detail::DetachedModuleBodyNodeData> impl;

  friend class ModuleBodySyntax;
  friend class ModuleBodySyntaxProducer;
  friend class ModuleBodySyntaxVerifier;
};

/// \brief Semantic detached module-owned syntax in canonical preorder.
class ModuleBodySyntax final {
public:
  ~ModuleBodySyntax() noexcept(false);
  ModuleBodySyntax(ModuleBodySyntax&&) noexcept;
  ModuleBodySyntax& operator=(ModuleBodySyntax&&) noexcept;
  ZC_DISALLOW_COPY(ModuleBodySyntax);

  /// \brief Decodes one complete bounded canonical semantic module body.
  ZC_NODISCARD static zc::Maybe<ModuleBodySyntax> decodeCanonical(
      zc::ArrayPtr<const uint8_t> encoded);
  /// \brief Admits a complete canonical preorder forest with exact root coverage.
  ZC_NODISCARD static zc::Maybe<ModuleBodySyntax> from(uint32_t rootCount,
                                                       zc::Vector<DetachedModuleBodyNode>&& nodes);
  ZC_NODISCARD ModuleBodySyntax clone() const;
  ZC_NODISCARD uint32_t rootCount() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const DetachedModuleBodyNode> nodes() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const ModuleBodySyntax& other) const noexcept;
  bool operator!=(const ModuleBodySyntax& other) const noexcept { return !(*this == other); }

private:
  explicit ModuleBodySyntax(
      zc::Own<module_body_syntax_detail::ModuleBodySyntaxData>&& impl) noexcept;

  zc::Own<module_body_syntax_detail::ModuleBodySyntaxData> impl;

  friend class ModuleBodySyntaxProducer;
  friend class ModuleBodySyntaxVerifier;
};

/// \brief One current AST node and byte range for an admitted module-owner syntax path.
struct ModuleBodyProvenanceEntry final {
  LocalSyntaxPath path;
  ast::NodeId node;
  uint64_t byteStart;
  uint64_t byteEnd;

  ZC_NODISCARD ModuleBodyProvenanceEntry clone() const;
};

/// \brief Revision-local total path map for one selected module source.
class ModuleBodyProvenance final {
public:
  ~ModuleBodyProvenance() noexcept(false);
  ModuleBodyProvenance(ModuleBodyProvenance&&) noexcept;
  ModuleBodyProvenance& operator=(ModuleBodyProvenance&&) noexcept;
  ZC_DISALLOW_COPY(ModuleBodyProvenance);

  /// \brief Decodes one complete bounded canonical module-body provenance map.
  ZC_NODISCARD static zc::Maybe<ModuleBodyProvenance> decodeCanonical(
      zc::ArrayPtr<const uint8_t> encoded);
  /// \brief Admits one source-complete, strictly path-sorted provenance map.
  ZC_NODISCARD static zc::Maybe<ModuleBodyProvenance> from(
      identity::SourceFileKey&& source, zc::Vector<ModuleBodyProvenanceEntry>&& entries);
  ZC_NODISCARD ModuleBodyProvenance clone() const;
  ZC_NODISCARD const identity::SourceFileKey& source() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ModuleBodyProvenanceEntry> entries() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const ModuleBodyProvenance& other) const;
  bool operator!=(const ModuleBodyProvenance& other) const { return !(*this == other); }

private:
  explicit ModuleBodyProvenance(
      zc::Own<module_body_syntax_detail::ModuleBodyProvenanceData>&& impl) noexcept;

  zc::Own<module_body_syntax_detail::ModuleBodyProvenanceData> impl;

  friend class ModuleBodySyntaxProducer;
  friend class ModuleBodySyntaxVerifier;
};

/// \brief Semantic detached syntax for one active named definition.
class NamedItemSyntax final {
public:
  NamedItemSyntax(NamedItemSyntax&&) noexcept = default;
  NamedItemSyntax& operator=(NamedItemSyntax&&) noexcept = default;
  ZC_DISALLOW_COPY(NamedItemSyntax);

  ZC_NODISCARD static zc::Maybe<NamedItemSyntax> from(identity::ModuleKey&& owningModule,
                                                      ModuleBodySyntax&& syntax);
  ZC_NODISCARD static zc::Maybe<NamedItemSyntax> decodeCanonical(
      zc::ArrayPtr<const uint8_t> encoded);
  ZC_NODISCARD NamedItemSyntax clone() const;
  ZC_NODISCARD const identity::ModuleKey& owningModule() const noexcept;
  ZC_NODISCARD const ModuleBodySyntax& detachedSyntax() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const NamedItemSyntax& other) const noexcept;
  bool operator!=(const NamedItemSyntax& other) const noexcept { return !(*this == other); }

private:
  NamedItemSyntax(identity::ModuleKey&& owningModule, ModuleBodySyntax&& syntax) noexcept;

  identity::ModuleKey owningModuleField;
  ModuleBodySyntax syntaxField;
};

/// \brief Revision-local total path map for one active named definition.
class NamedItemProvenance final {
public:
  NamedItemProvenance(NamedItemProvenance&&) noexcept = default;
  NamedItemProvenance& operator=(NamedItemProvenance&&) noexcept = default;
  ZC_DISALLOW_COPY(NamedItemProvenance);

  ZC_NODISCARD static zc::Maybe<NamedItemProvenance> from(ModuleBodyProvenance&& provenance);
  ZC_NODISCARD static zc::Maybe<NamedItemProvenance> decodeCanonical(
      zc::ArrayPtr<const uint8_t> encoded);
  ZC_NODISCARD NamedItemProvenance clone() const;
  ZC_NODISCARD const ModuleBodyProvenance& detachedProvenance() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const NamedItemProvenance& other) const;
  bool operator!=(const NamedItemProvenance& other) const { return !(*this == other); }

private:
  explicit NamedItemProvenance(ModuleBodyProvenance&& provenance) noexcept;

  ModuleBodyProvenance provenanceField;
};

/// \brief Current stable definition site consumed while pruning module-owned syntax.
struct ModuleBodyDefinitionBoundaryInput final {
  ast::NodeId node;
  identity::DefinitionKey key;
};

/// \brief Semantic and revision-local halves produced from one exact selected source.
struct ModuleBodySyntaxProjection final {
  ModuleBodySyntax syntax;
  ModuleBodyProvenance provenance;
};

enum class ModuleBodySyntaxFailureKind : uint8_t {
  None = 0x00,
  InvalidSource = 0x01,
  InvalidModuleRoot = 0x02,
  InvalidBoundaryInventory = 0x03,
  InvalidDetachedSyntax = 0x04,
  InvalidProvenance = 0x05,
  ProjectionMismatch = 0x06
};

struct ModuleBodySyntaxFailure final {
  ModuleBodySyntaxFailureKind kind;
  ast::NodeId node;
};

using ModuleBodySyntaxProjectionResult =
    zc::OneOf<ModuleBodySyntaxProjection, ModuleBodySyntaxFailure>;

/// \brief Produces detached module-owned syntax and current provenance without semantic handles.
class ModuleBodySyntaxProducer final {
public:
  ZC_NODISCARD static ModuleBodySyntaxProjectionResult produce(
      const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
      ast::NodeId moduleNode, const StableIdentityAdmission& admission);
  ZC_NODISCARD static ModuleBodySyntaxProjectionResult produce(
      const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
      ast::NodeId moduleNode, zc::ArrayPtr<const ModuleBodyDefinitionBoundaryInput> definitions);

  ZC_NODISCARD static ModuleBodySyntaxProjectionResult produceNamedItem(
      const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
      ast::NodeId moduleNode, ast::NodeId definitionNode, const identity::DefinitionKey& definition,
      zc::ArrayPtr<const ModuleBodyDefinitionBoundaryInput> definitions);
};

/// \brief Independently reconstructs pruning, canonical syntax, and total provenance coverage.
class ModuleBodySyntaxVerifier final {
public:
  ZC_NODISCARD static ModuleBodySyntaxProjectionResult reconstruct(
      const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
      ast::NodeId moduleNode, const StableIdentityAdmission& admission);
  ZC_NODISCARD static ModuleBodySyntaxProjectionResult reconstruct(
      const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
      ast::NodeId moduleNode, zc::ArrayPtr<const ModuleBodyDefinitionBoundaryInput> definitions);

  ZC_NODISCARD static ModuleBodySyntaxProjectionResult reconstructNamedItem(
      const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
      ast::NodeId moduleNode, ast::NodeId definitionNode, const identity::DefinitionKey& definition,
      zc::ArrayPtr<const ModuleBodyDefinitionBoundaryInput> definitions);

  ZC_NODISCARD static ModuleBodySyntaxFailureKind verify(
      const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
      ast::NodeId moduleNode, zc::ArrayPtr<const ModuleBodyDefinitionBoundaryInput> definitions,
      const ModuleBodySyntaxProjection& projection);
};

}  // namespace zomlang::compiler::binder
