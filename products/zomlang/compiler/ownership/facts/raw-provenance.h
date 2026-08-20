// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>

#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ownership/facts/paths.h"
#include "zomlang/compiler/ownership/facts/refs.h"

namespace zomlang::compiler::ownership::facts {

/// \brief Canonical identity of one raw-provenance carrier.
///
/// A carrier names one static generation of a raw-pointer value. The root
/// carrier has introduction == the root-seeding event and destination == the
/// root move path. A derived carrier is keyed by the copy, move, or assignment
/// event and its destination move path.
struct RawProvenanceCarrierKey final {
  MirEventKey introduction;
  MovePathKey destination;

  ZC_NODISCARD RawProvenanceCarrierKey clone() const {
    return RawProvenanceCarrierKey{
        introduction, MovePathKey{destination.owner, destination.place.clone()}};
  }
};

/// \brief Reference-origin seed for a reference-to-raw conversion root.
///
/// Each distinct reaching `ReferenceOrigin` at a reference-to-raw conversion
/// creates one `Reference` row in the raw-origin universe.
struct RawReferenceOrigin final {
  ReferenceInputOrigin origin;
};

/// \brief Raw-typed receiver or parameter seed.
///
/// A raw-typed function receiver or parameter creates one `RawInput` row in
/// the raw-origin universe. `isReceiver` is true for the receiver and false
/// for a parameter; `parameterIndex` is valid only when `isReceiver` is false.
struct RawInputOrigin final {
  bool isReceiver;
  uint32_t parameterIndex;

  bool operator==(const RawInputOrigin&) const = default;
};

/// \brief Built MIR static-address root seed.
///
/// Each Built MIR static-address root creates one `StaticAddress` row in the
/// raw-origin universe.
struct RawStaticAddressOrigin final {
  MirEventKey creation;

  bool operator==(const RawStaticAddressOrigin&) const = default;
};

/// \brief Acknowledged unsafe operation that creates a raw address.
///
/// An acknowledged unsafe operation that produces a raw address without a raw
/// or reference predecessor creates one `UnsafeAddress` row in the raw-origin
/// universe, tied to that boundary.
struct RawUnsafeAddressOrigin final {
  UnsafeBoundaryKey boundary;

  bool operator==(const RawUnsafeAddressOrigin&) const = default;
};

/// \brief Four-class raw-provenance origin universe.
///
/// The raw-origin universe is the sorted union of exactly these four root
/// classes and no others. Derived carriers cannot introduce an origin; every
/// origin in a derived carrier must reach it through its predecessors.
using RawProvenanceOrigin =
    zc::OneOf<RawReferenceOrigin, RawInputOrigin, RawStaticAddressOrigin, RawUnsafeAddressOrigin>;

/// \brief Deep-copies one raw-provenance origin.
///
/// `RawReferenceOrigin` is move-only because it carries a `ReferenceInputOrigin`
/// whose referent move path contains a move-only `MirPlace`; the other three
/// variants are trivially copyable.
inline RawProvenanceOrigin cloneRawProvenanceOrigin(const RawProvenanceOrigin& origin) {
  if (origin.is<RawReferenceOrigin>()) {
    return RawProvenanceOrigin{
        RawReferenceOrigin{origin.get<RawReferenceOrigin>().origin.clone()}};
  }
  if (origin.is<RawInputOrigin>()) {
    return RawProvenanceOrigin{origin.get<RawInputOrigin>()};
  }
  if (origin.is<RawStaticAddressOrigin>()) {
    return RawProvenanceOrigin{origin.get<RawStaticAddressOrigin>()};
  }
  return RawProvenanceOrigin{origin.get<RawUnsafeAddressOrigin>()};
}

/// \brief One verified raw-provenance carrier fact.
///
/// A root carrier has an empty predecessor sequence. A derived carrier has one
/// predecessor for every reaching input carrier on every reaching path. The
/// origins sequence is the least fixed-point union of the predecessor origins,
/// seeded by the root carrier's seed origins. Every encoded carrier must be
/// reachable from at least one root; therefore every origins sequence is
/// non-empty.
struct RawProvenanceFact final {
  RawProvenanceCarrierKey key;
  zc::Vector<RawProvenanceCarrierKey> predecessors;
  zc::Vector<RawProvenanceOrigin> origins;

  ZC_NODISCARD RawProvenanceFact clone() const {
    zc::Vector<RawProvenanceCarrierKey> clonedPredecessors;
    for (const auto& predecessor : predecessors) clonedPredecessors.add(predecessor.clone());
    zc::Vector<RawProvenanceOrigin> clonedOrigins;
    for (const auto& origin : origins) clonedOrigins.add(cloneRawProvenanceOrigin(origin));
    return RawProvenanceFact{key.clone(), zc::mv(clonedPredecessors), zc::mv(clonedOrigins)};
  }
};

}  // namespace zomlang::compiler::ownership::facts
