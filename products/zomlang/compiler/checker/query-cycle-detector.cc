// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/checker/query-cycle-detector.h"

namespace zomlang {
namespace compiler {
namespace checker {

QueryKey::QueryKey(QueryKind kind, uint32_t primaryId, uint32_t secondaryId, type::TypeId typeId,
                   zc::StringPtr name)
    : kind(kind), primaryId(primaryId), secondaryId(secondaryId), typeId(typeId), name(name) {}

QueryKey QueryKey::signatureOf(uint32_t symbolId) {
  return QueryKey(QueryKind::SignatureOf, symbolId, 0, type::TypeId(), ""_zc);
}

QueryKey QueryKey::typeAliasOf(uint32_t symbolId) {
  return QueryKey(QueryKind::TypeAliasOf, symbolId, 0, type::TypeId(), ""_zc);
}

QueryKey QueryKey::associatedProjection(type::TypeId base, zc::StringPtr name) {
  return QueryKey(QueryKind::AssociatedProjection, 0, 0, base, name);
}

QueryKey QueryKey::markerDerivation(type::TypeId type, uint32_t markerId) {
  return QueryKey(QueryKind::MarkerDerivation, 0, markerId, type, ""_zc);
}

QueryKind QueryKey::getKind() const { return kind; }

uint32_t QueryKey::getPrimaryId() const { return primaryId; }

uint32_t QueryKey::getSecondaryId() const { return secondaryId; }

type::TypeId QueryKey::getTypeId() const { return typeId; }

zc::StringPtr QueryKey::getName() const { return name; }

bool QueryKey::equals(const QueryKey& other) const {
  return kind == other.kind && primaryId == other.primaryId && secondaryId == other.secondaryId &&
         typeId == other.typeId && name == other.name;
}

zc::String QueryKey::toString() const {
  switch (kind) {
    case QueryKind::SignatureOf:
      return zc::str("SignatureOf(", primaryId, ")");
    case QueryKind::TypeAliasOf:
      return zc::str("TypeAliasOf(", primaryId, ")");
    case QueryKind::AssociatedProjection:
      return zc::str("AssociatedProjection(", typeId.value, "::", name, ")");
    case QueryKind::MarkerDerivation:
      return zc::str("MarkerDerivation(", typeId.value, ",", secondaryId, ")");
  }
  return zc::str("<unknown-query>");
}

struct QueryCycleDetector::Impl {
  zc::Vector<QueryKey> stack;
};

QueryCycleDetector::Guard::Guard(QueryCycleDetector& detector, QueryKey key, bool entered)
    : detector(&detector), key(key), entered(entered) {}

QueryCycleDetector::Guard::~Guard() noexcept(false) {
  if (detector != nullptr && entered) { detector->leave(key); }
}

QueryCycleDetector::Guard::Guard(Guard&& other) noexcept
    : detector(other.detector), key(other.key), entered(other.entered) {
  other.detector = nullptr;
  other.entered = false;
}

QueryCycleDetector::Guard& QueryCycleDetector::Guard::operator=(Guard&& other) noexcept {
  if (this != &other) {
    if (detector != nullptr && entered) { detector->leave(key); }
    detector = other.detector;
    key = other.key;
    entered = other.entered;
    other.detector = nullptr;
    other.entered = false;
  }
  return *this;
}

bool QueryCycleDetector::Guard::hasCycle() const { return !entered; }

QueryCycleDetector::QueryCycleDetector() : impl(zc::heap<Impl>()) {}

QueryCycleDetector::~QueryCycleDetector() noexcept(false) = default;

QueryCycleDetector::QueryCycleDetector(QueryCycleDetector&& other) noexcept = default;

QueryCycleDetector& QueryCycleDetector::operator=(QueryCycleDetector&& other) noexcept = default;

bool QueryCycleDetector::wouldCycle(const QueryKey& key) const {
  for (size_t i = 0; i < impl->stack.size(); ++i) {
    if (impl->stack[i].equals(key)) { return true; }
  }
  return false;
}

QueryCycleDetector::Guard QueryCycleDetector::enter(QueryKey key) {
  if (wouldCycle(key)) { return Guard(*this, key, false); }
  impl->stack.add(key);
  return Guard(*this, key, true);
}

void QueryCycleDetector::leave(const QueryKey& key) {
  ZC_IREQUIRE(!impl->stack.empty(), "QueryCycleDetector::leave: empty stack");
  ZC_IREQUIRE(impl->stack.back().equals(key), "QueryCycleDetector::leave: key mismatch");
  impl->stack.removeLast();
}

size_t QueryCycleDetector::depth() const { return impl->stack.size(); }

void QueryCycleDetector::clear() { impl->stack.clear(); }

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
