// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstddef>
#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"

namespace zomlang::compiler::identity {
class IdentityDiagnosticProjector;
}

namespace zomlang::compiler::checker {
class CheckerDiagnosticProjector;
}

namespace zomlang::compiler::binder {
class BinderDiagnosticProjector;
class ModuleGraphDiagnosticProjector;
}  // namespace zomlang::compiler::binder

namespace zomlang::compiler::basic {

enum class CompilerIncidentDomain : uint16_t {
  Frontend = 0x0001,
  Query = 0x0002,
  Identity = 0x0003,
  Package = 0x0004,
  Binder = 0x0005,
  Checker = 0x0006,
  Ownership = 0x0007,
  Ir = 0x0008,
  Backend = 0x0009,
  Driver = 0x000a,
  Diagnostics = 0x000b,
};

/// \brief Registry-assigned incident phase key within one domain.
class CompilerIncidentPhaseKey final {
public:
  constexpr bool operator==(const CompilerIncidentPhaseKey&) const noexcept = default;
  constexpr bool operator<(const CompilerIncidentPhaseKey& other) const noexcept {
    return value < other.value;
  }
  ZC_NODISCARD constexpr uint32_t tag() const noexcept { return value; }

private:
  explicit constexpr CompilerIncidentPhaseKey(uint32_t value) noexcept : value(value) {}
  uint32_t value = 0;

  friend class CompilerIncidentDescriptor;
  friend class identity::IdentityDiagnosticProjector;
  friend class checker::CheckerDiagnosticProjector;
  friend class binder::BinderDiagnosticProjector;
  friend class binder::ModuleGraphDiagnosticProjector;
};

/// \brief Registry-assigned incident kind key within one domain.
class CompilerIncidentKindKey final {
public:
  constexpr bool operator==(const CompilerIncidentKindKey&) const noexcept = default;
  constexpr bool operator<(const CompilerIncidentKindKey& other) const noexcept {
    return value < other.value;
  }
  ZC_NODISCARD constexpr uint32_t tag() const noexcept { return value; }

private:
  explicit constexpr CompilerIncidentKindKey(uint32_t value) noexcept : value(value) {}
  uint32_t value = 0;

  friend class CompilerIncidentDescriptor;
  friend class identity::IdentityDiagnosticProjector;
  friend class checker::CheckerDiagnosticProjector;
  friend class binder::BinderDiagnosticProjector;
  friend class binder::ModuleGraphDiagnosticProjector;
};

/// \brief Registry-assigned producer key within one domain.
class CompilerIncidentProducerKey final {
public:
  constexpr bool operator==(const CompilerIncidentProducerKey&) const noexcept = default;
  constexpr bool operator<(const CompilerIncidentProducerKey& other) const noexcept {
    return value < other.value;
  }
  ZC_NODISCARD constexpr uint32_t tag() const noexcept { return value; }

private:
  explicit constexpr CompilerIncidentProducerKey(uint32_t value) noexcept : value(value) {}
  uint32_t value = 0;

  friend class CompilerIncidentDescriptor;
  friend class identity::IdentityDiagnosticProjector;
  friend class checker::CheckerDiagnosticProjector;
  friend class binder::BinderDiagnosticProjector;
  friend class binder::ModuleGraphDiagnosticProjector;
};

/// \brief Privacy-preserving shape of one internal compiler failure.
class CompilerIncidentDescriptor final {
public:
  ZC_NODISCARD constexpr CompilerIncidentDomain domain() const noexcept { return domainValue; }
  ZC_NODISCARD constexpr CompilerIncidentPhaseKey phase() const noexcept { return phaseValue; }
  ZC_NODISCARD constexpr CompilerIncidentKindKey kind() const noexcept { return kindValue; }
  ZC_NODISCARD constexpr CompilerIncidentProducerKey producer() const noexcept {
    return producerValue;
  }
  ZC_NODISCARD constexpr uint64_t occurrences() const noexcept { return occurrenceCount; }
  ZC_NODISCARD constexpr CompilerIncidentDescriptor repeated(uint64_t occurrences) const noexcept {
    return CompilerIncidentDescriptor(domainValue, phaseValue, kindValue, producerValue,
                                      occurrences);
  }

  constexpr bool operator==(const CompilerIncidentDescriptor&) const noexcept = default;
  constexpr bool operator<(const CompilerIncidentDescriptor& other) const noexcept;

private:
  constexpr CompilerIncidentDescriptor() noexcept = default;
  constexpr CompilerIncidentDescriptor(CompilerIncidentDomain domain,
                                       CompilerIncidentPhaseKey phase, CompilerIncidentKindKey kind,
                                       CompilerIncidentProducerKey producer,
                                       uint64_t occurrences) noexcept
      : domainValue(domain),
        phaseValue(phase),
        kindValue(kind),
        producerValue(producer),
        occurrenceCount(occurrences) {}

  CompilerIncidentDomain domainValue = CompilerIncidentDomain::Diagnostics;
  CompilerIncidentPhaseKey phaseValue{0};
  CompilerIncidentKindKey kindValue{0};
  CompilerIncidentProducerKey producerValue{0};
  uint64_t occurrenceCount = 0;

  friend class BoundedIncidentSet;
  friend class identity::IdentityDiagnosticProjector;
  friend class checker::CheckerDiagnosticProjector;
  friend class binder::BinderDiagnosticProjector;
  friend class binder::ModuleGraphDiagnosticProjector;
};

/// \brief Allocation-free deterministic aggregation of registered incident shapes.
class BoundedIncidentSet final {
public:
  static constexpr size_t maximumDescriptors = 12 * 13 * 14 + 4 * 10 + 4 * 5 + 5 * 3 + 5 * 8 * 5;

  ZC_NODISCARD bool add(CompilerIncidentDescriptor descriptor) noexcept;
  ZC_NODISCARD bool merge(const BoundedIncidentSet& other) noexcept;
  ZC_NODISCARD constexpr bool empty() const noexcept { return descriptorCount == 0; }
  ZC_NODISCARD constexpr size_t size() const noexcept { return descriptorCount; }
  ZC_NODISCARD zc::ArrayPtr<const CompilerIncidentDescriptor> descriptors() const noexcept {
    return zc::arrayPtr(storage, descriptorCount);
  }

private:
  CompilerIncidentDescriptor storage[maximumDescriptors];
  size_t descriptorCount = 0;
};

constexpr bool CompilerIncidentDescriptor::operator<(
    const CompilerIncidentDescriptor& other) const noexcept {
  if (domainValue != other.domainValue) { return domainValue < other.domainValue; }
  if (!(phaseValue == other.phaseValue)) { return phaseValue < other.phaseValue; }
  if (!(kindValue == other.kindValue)) { return kindValue < other.kindValue; }
  return producerValue < other.producerValue;
}

}  // namespace zomlang::compiler::basic
