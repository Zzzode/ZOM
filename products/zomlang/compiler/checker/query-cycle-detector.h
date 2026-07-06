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

#pragma once

#include <cstdint>

#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/type/type-interner.h"

namespace zomlang {
namespace compiler {
namespace checker {

/// \brief Query families that participate in RFC 0005 cycle detection.
enum class QueryKind { SignatureOf, TypeAliasOf, AssociatedProjection, MarkerDerivation };

/// \brief A single query stack key.
class QueryKey final {
public:
  static QueryKey signatureOf(uint32_t symbolId);
  static QueryKey typeAliasOf(uint32_t symbolId);
  static QueryKey associatedProjection(type::TypeId base, zc::StringPtr name);
  static QueryKey markerDerivation(type::TypeId type, uint32_t markerId);

  QueryKind getKind() const;
  uint32_t getPrimaryId() const;
  uint32_t getSecondaryId() const;
  type::TypeId getTypeId() const;
  zc::StringPtr getName() const;

  bool equals(const QueryKey& other) const;
  zc::String toString() const;

private:
  QueryKey(QueryKind kind, uint32_t primaryId, uint32_t secondaryId, type::TypeId typeId,
           zc::StringPtr name);

  QueryKind kind;
  uint32_t primaryId;
  uint32_t secondaryId;
  type::TypeId typeId;
  zc::StringPtr name;
};

/// \brief Detects recursive checker/type queries.
class QueryCycleDetector final {
public:
  class Guard final {
  public:
    Guard(QueryCycleDetector& detector, QueryKey key, bool entered);
    ~Guard() noexcept(false);

    ZC_DISALLOW_COPY(Guard);
    Guard(Guard&& other) noexcept;
    Guard& operator=(Guard&& other) noexcept;

    bool hasCycle() const;

  private:
    QueryCycleDetector* detector;
    QueryKey key;
    bool entered;
  };

  QueryCycleDetector();
  ~QueryCycleDetector() noexcept(false);

  ZC_DISALLOW_COPY(QueryCycleDetector);
  QueryCycleDetector(QueryCycleDetector&& other) noexcept;
  QueryCycleDetector& operator=(QueryCycleDetector&& other) noexcept;

  bool wouldCycle(const QueryKey& key) const;
  Guard enter(QueryKey key);
  void leave(const QueryKey& key);
  size_t depth() const;
  void clear();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
