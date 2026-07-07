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

#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {
namespace type {

/// \brief ObjectType - Represents structural object types.
///
/// Object types describe records/structs with named members.
/// Example: `{ x: i32, y: i32, name: str }`
///
/// Object types support width subtyping: an object with more members
/// is a subtype of one with fewer members.
class ObjectType final : public Type {
public:
  /// \brief Construct an empty object type.
  ObjectType();

  ~ObjectType() noexcept(false);

  ZC_DISALLOW_COPY(ObjectType);

  // Move semantics
  ObjectType(ObjectType&& other) noexcept;
  ObjectType& operator=(ObjectType&& other) noexcept;

  /// \brief Add a member to the object type.
  void addMember(zc::StringPtr name, zc::Own<Type> type);

  /// \brief Get the type of a member by name.
  zc::Maybe<const Type&> getMember(zc::StringPtr name) const;

  /// \brief Get the number of members.
  size_t getMemberCount() const;

  /// \brief Check if a member exists.
  bool hasMember(zc::StringPtr name) const;

  /// \brief Member entry for iteration.
  struct MemberEntry {
    zc::StringPtr name;
    zc::Maybe<const Type&> type;
  };

  /// \brief Get all members (for iteration).
  zc::Array<MemberEntry> getMembers() const;

  // Type overrides
  TypeKind getKind() const override { return TypeKind::Object; }
  zc::String toString() const override;
  bool equals(const Type& other) const override;
  bool isSubtypeOf(const Type& other) const override;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
