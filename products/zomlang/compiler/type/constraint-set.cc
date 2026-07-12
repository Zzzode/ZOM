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

#include "zomlang/compiler/type/constraint-set.h"

namespace zomlang {
namespace compiler {
namespace type {

struct ConstraintSet::Impl {
  zc::Vector<Constraint> constraints;
};

ConstraintSet::ConstraintSet() : impl(zc::heap<Impl>()) {}

ConstraintSet::~ConstraintSet() noexcept(false) = default;

ConstraintSet::ConstraintSet(ConstraintSet&& other) noexcept = default;

ConstraintSet& ConstraintSet::operator=(ConstraintSet&& other) noexcept = default;

void ConstraintSet::addEq(SemanticTypeId left, SemanticTypeId right, zc::String reason) {
  impl->constraints.add(Constraint{ConstraintKind::Eq, left, right, zc::mv(reason)});
}

void ConstraintSet::addSub(SemanticTypeId source, SemanticTypeId target, zc::String reason) {
  impl->constraints.add(Constraint{ConstraintKind::Sub, source, target, zc::mv(reason)});
}

void ConstraintSet::addObligation(SemanticTypeId type, SemanticTypeId interfaceType,
                                  zc::String reason) {
  impl->constraints.add(
      Constraint{ConstraintKind::Obligation, type, interfaceType, zc::mv(reason)});
}

void ConstraintSet::addProjectionEq(SemanticTypeId projection, SemanticTypeId value,
                                    zc::String reason) {
  impl->constraints.add(
      Constraint{ConstraintKind::ProjectionEq, projection, value, zc::mv(reason)});
}

size_t ConstraintSet::size() const { return impl->constraints.size(); }

bool ConstraintSet::empty() const { return impl->constraints.empty(); }

const Constraint& ConstraintSet::get(size_t index) const { return impl->constraints[index]; }

zc::ArrayPtr<const Constraint> ConstraintSet::constraints() const {
  return impl->constraints.asPtr();
}

void ConstraintSet::clear() { impl->constraints.clear(); }

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
