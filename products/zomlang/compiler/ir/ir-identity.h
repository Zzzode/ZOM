// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/common.h"
#include "zomlang/compiler/checker/checked-facts.h"
#include "zomlang/compiler/identity/brand.h"
#include "zomlang/compiler/identity/frozen-registry.h"

namespace zomlang::compiler::mir {

/// \brief Deterministic one-based identity of a block in one MIR function.
class MirBlockId final {
public:
  constexpr MirBlockId() noexcept = default;

  /// \brief Constructs a valid block identity from deterministic builder order.
  /// \param ordinal One-based block ordinal.
  /// \return The block identity, or none for zero.
  ZC_NODISCARD static zc::Maybe<MirBlockId> fromOrdinal(uint32_t ordinal) noexcept {
    if (ordinal == 0) { return zc::none; }
    return MirBlockId(ordinal);
  }

  ZC_NODISCARD constexpr bool isValid() const noexcept { return value != 0; }
  ZC_NODISCARD constexpr uint32_t ordinal() const noexcept { return value; }

  constexpr bool operator==(MirBlockId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(MirBlockId other) const noexcept { return !(*this == other); }

private:
  explicit constexpr MirBlockId(uint32_t ordinal) noexcept : value(ordinal) {}

  uint32_t value = 0;
};

}  // namespace zomlang::compiler::mir

namespace zomlang::compiler::lir {

/// \brief Deterministic one-based identity of a block in one LIR function.
class LirBlockId final {
public:
  constexpr LirBlockId() noexcept = default;

  /// \brief Constructs a valid block identity from deterministic builder order.
  /// \param ordinal One-based block ordinal.
  /// \return The block identity, or none for zero.
  ZC_NODISCARD static zc::Maybe<LirBlockId> fromOrdinal(uint32_t ordinal) noexcept {
    if (ordinal == 0) { return zc::none; }
    return LirBlockId(ordinal);
  }

  ZC_NODISCARD constexpr bool isValid() const noexcept { return value != 0; }
  ZC_NODISCARD constexpr uint32_t ordinal() const noexcept { return value; }

  constexpr bool operator==(LirBlockId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(LirBlockId other) const noexcept { return !(*this == other); }

private:
  explicit constexpr LirBlockId(uint32_t ordinal) noexcept : value(ordinal) {}

  uint32_t value = 0;
};

}  // namespace zomlang::compiler::lir

namespace zomlang::compiler::ir {

/// \brief Canonical cross-layer identity of one monomorphized callable instance.
class InstanceId final {
public:
  constexpr InstanceId() noexcept = default;

  /// \brief Constructs an instance identity from one context-bound definition and evidence pair.
  /// \param context Semantic context that must own every component.
  /// \param definition Callable definition being instantiated.
  /// \param substitution Canonical generic substitution record.
  /// \param witnesses Canonical witness-argument record.
  /// \return The identity, or none when any component is invalid or foreign.
  ZC_NODISCARD static zc::Maybe<InstanceId> from(
      identity::SemanticContextBrand context, identity::DefId definition,
      checker::checked::CanonicalSubstitutionId substitution,
      checker::checked::WitnessArgumentsId witnesses) noexcept {
    if (!context.isValid() || !definition.belongsTo(context) || !substitution.belongsTo(context) ||
        !witnesses.belongsTo(context)) {
      return zc::none;
    }
    return InstanceId(definition, substitution, witnesses);
  }

  ZC_NODISCARD constexpr bool isValid() const noexcept {
    return definitionValue.isValid() && substitutionValue.isValid() && witnessValue.isValid();
  }
  ZC_NODISCARD constexpr identity::DefId definition() const noexcept { return definitionValue; }
  ZC_NODISCARD constexpr checker::checked::CanonicalSubstitutionId substitution() const noexcept {
    return substitutionValue;
  }
  ZC_NODISCARD constexpr checker::checked::WitnessArgumentsId witnesses() const noexcept {
    return witnessValue;
  }

  constexpr bool operator==(InstanceId other) const noexcept {
    return definitionValue == other.definitionValue &&
           substitutionValue == other.substitutionValue && witnessValue == other.witnessValue;
  }
  constexpr bool operator!=(InstanceId other) const noexcept { return !(*this == other); }

private:
  constexpr InstanceId(identity::DefId definition,
                       checker::checked::CanonicalSubstitutionId substitution,
                       checker::checked::WitnessArgumentsId witnesses) noexcept
      : definitionValue(definition), substitutionValue(substitution), witnessValue(witnesses) {}

  identity::DefId definitionValue;
  checker::checked::CanonicalSubstitutionId substitutionValue;
  checker::checked::WitnessArgumentsId witnessValue;
};

}  // namespace zomlang::compiler::ir
