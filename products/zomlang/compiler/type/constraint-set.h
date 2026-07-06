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

#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/type/type-interner.h"

namespace zomlang {
namespace compiler {
namespace type {

/// \brief Constraint discriminator emitted by the body checker.
enum class ConstraintKind { Eq, Sub, Obligation, ProjectionEq };

/// \brief A single type-system constraint.
struct Constraint {
  ConstraintKind kind;
  TypeId first;
  TypeId second;
  zc::String reason;
};

/// \brief Collection of constraints emitted during body checking.
class ConstraintSet final {
public:
  ConstraintSet();
  ~ConstraintSet() noexcept(false);

  ZC_DISALLOW_COPY(ConstraintSet);
  ConstraintSet(ConstraintSet&& other) noexcept;
  ConstraintSet& operator=(ConstraintSet&& other) noexcept;

  void addEq(TypeId left, TypeId right, zc::String reason);
  void addSub(TypeId source, TypeId target, zc::String reason);
  void addObligation(TypeId type, TypeId interfaceType, zc::String reason);
  void addProjectionEq(TypeId projection, TypeId value, zc::String reason);

  size_t size() const;
  bool empty() const;
  const Constraint& get(size_t index) const;
  zc::ArrayPtr<const Constraint> constraints() const;
  void clear();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
