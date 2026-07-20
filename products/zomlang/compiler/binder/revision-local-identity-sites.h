#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/binder/identity-pre-admission.h"
#include "zomlang/compiler/binder/named-identity-inventory.h"

namespace zomlang::compiler::binder {

/// \brief One current AST occurrence of a stable named definition.
class RevisionLocalDefinitionSite final {
public:
  RevisionLocalDefinitionSite(RevisionLocalDefinitionSite&&) noexcept = default;
  RevisionLocalDefinitionSite& operator=(RevisionLocalDefinitionSite&&) noexcept = default;
  ZC_DISALLOW_COPY(RevisionLocalDefinitionSite);

  ZC_NODISCARD static zc::Maybe<RevisionLocalDefinitionSite> from(
      ast::NodeId node, identity::DefinitionKey&& definition, IdentitySyntaxSiteKey&& site,
      uint64_t byteStart, uint64_t byteEnd);
  ZC_NODISCARD RevisionLocalDefinitionSite clone() const;
  ZC_NODISCARD ast::NodeId node() const noexcept;
  ZC_NODISCARD const identity::DefinitionKey& definition() const noexcept;
  ZC_NODISCARD const IdentitySyntaxSiteKey& site() const noexcept;
  ZC_NODISCARD uint64_t byteStart() const noexcept;
  ZC_NODISCARD uint64_t byteEnd() const noexcept;

private:
  RevisionLocalDefinitionSite(ast::NodeId node, identity::DefinitionKey&& definition,
                              IdentitySyntaxSiteKey&& site, uint64_t byteStart,
                              uint64_t byteEnd) noexcept;

  ast::NodeId nodeField;
  identity::DefinitionKey definitionField;
  IdentitySyntaxSiteKey siteField;
  uint64_t byteStartField;
  uint64_t byteEndField;
};

/// \brief Complete current named-definition occurrence map for one selected source.
class RevisionLocalDefinitionSites final {
public:
  RevisionLocalDefinitionSites(RevisionLocalDefinitionSites&&) noexcept = default;
  RevisionLocalDefinitionSites& operator=(RevisionLocalDefinitionSites&&) noexcept = default;
  ZC_DISALLOW_COPY(RevisionLocalDefinitionSites);

  ZC_NODISCARD static zc::Maybe<RevisionLocalDefinitionSites> fromVerified(
      const identity::ModuleKey& module, const identity::SourceFileKey& source,
      const NamedDefinitionInventory& inventory,
      zc::Vector<RevisionLocalDefinitionSite>&& sites);
  ZC_NODISCARD static zc::Maybe<RevisionLocalDefinitionSites> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD RevisionLocalDefinitionSites clone() const;
  ZC_NODISCARD zc::ArrayPtr<const RevisionLocalDefinitionSite> entries() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  ZC_NODISCARD bool sameAs(const RevisionLocalDefinitionSites& other) const;

private:
  explicit RevisionLocalDefinitionSites(
      zc::Vector<RevisionLocalDefinitionSite>&& sites) noexcept;

  zc::Vector<RevisionLocalDefinitionSite> siteFields;
};

/// \brief One current AST occurrence of a stable implementation.
class RevisionLocalImplementationSite final {
public:
  RevisionLocalImplementationSite(RevisionLocalImplementationSite&&) noexcept = default;
  RevisionLocalImplementationSite& operator=(RevisionLocalImplementationSite&&) noexcept = default;
  ZC_DISALLOW_COPY(RevisionLocalImplementationSite);

  ZC_NODISCARD static zc::Maybe<RevisionLocalImplementationSite> from(
      ast::NodeId node, ImplSourceOccurrenceKey&& occurrence, uint64_t byteStart,
      uint64_t byteEnd);
  ZC_NODISCARD RevisionLocalImplementationSite clone() const;
  ZC_NODISCARD ast::NodeId node() const noexcept;
  ZC_NODISCARD const ImplSourceOccurrenceKey& occurrence() const noexcept;
  ZC_NODISCARD uint64_t byteStart() const noexcept;
  ZC_NODISCARD uint64_t byteEnd() const noexcept;

private:
  RevisionLocalImplementationSite(ast::NodeId node, ImplSourceOccurrenceKey&& occurrence,
                                  uint64_t byteStart, uint64_t byteEnd) noexcept;

  ast::NodeId nodeField;
  ImplSourceOccurrenceKey occurrenceField;
  uint64_t byteStartField;
  uint64_t byteEndField;
};

/// \brief Complete current implementation occurrence map for one selected source.
class RevisionLocalImplementationSites final {
public:
  RevisionLocalImplementationSites(RevisionLocalImplementationSites&&) noexcept = default;
  RevisionLocalImplementationSites& operator=(RevisionLocalImplementationSites&&) noexcept =
      default;
  ZC_DISALLOW_COPY(RevisionLocalImplementationSites);

  ZC_NODISCARD static zc::Maybe<RevisionLocalImplementationSites> fromVerified(
      const identity::ModuleKey& module, const identity::SourceFileKey& source,
      const NamedImplementationInventory& inventory,
      zc::Vector<RevisionLocalImplementationSite>&& sites);
  ZC_NODISCARD static zc::Maybe<RevisionLocalImplementationSites> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD RevisionLocalImplementationSites clone() const;
  ZC_NODISCARD zc::ArrayPtr<const RevisionLocalImplementationSite> entries() const
      ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  ZC_NODISCARD bool sameAs(const RevisionLocalImplementationSites& other) const;

private:
  explicit RevisionLocalImplementationSites(
      zc::Vector<RevisionLocalImplementationSite>&& sites) noexcept;

  zc::Vector<RevisionLocalImplementationSite> siteFields;
};

}  // namespace zomlang::compiler::binder
