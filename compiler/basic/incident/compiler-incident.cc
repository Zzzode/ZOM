// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/basic/incident/compiler-incident.h"

#include "zc/core/common.h"

namespace zomlang::compiler::basic {
namespace {

bool sameShape(const CompilerIncidentDescriptor& left,
               const CompilerIncidentDescriptor& right) noexcept {
  return left.domain() == right.domain() && left.phase() == right.phase() &&
         left.kind() == right.kind() && left.producer() == right.producer();
}

}  // namespace

bool BoundedIncidentSet::add(CompilerIncidentDescriptor descriptor) noexcept {
  if (descriptor.occurrences() == 0) { return false; }

  size_t position = 0;
  while (position < descriptorCount && storage[position] < descriptor) { ++position; }
  if (position < descriptorCount && sameShape(storage[position], descriptor)) {
    const uint64_t remaining = UINT64_MAX - storage[position].occurrenceCount;
    storage[position].occurrenceCount +=
        descriptor.occurrenceCount > remaining ? remaining : descriptor.occurrenceCount;
    return true;
  }
  if (descriptorCount == maximumDescriptors) { return false; }
  for (size_t index = descriptorCount; index > position; --index) {
    storage[index] = storage[index - 1];
  }
  storage[position] = descriptor;
  ++descriptorCount;
  return true;
}

bool BoundedIncidentSet::merge(const BoundedIncidentSet& other) noexcept {
  for (const auto& descriptor : other.descriptors()) {
    if (!add(descriptor)) { return false; }
  }
  return true;
}

}  // namespace zomlang::compiler::basic
