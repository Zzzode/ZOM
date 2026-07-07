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

#include "zomlang/compiler/type/object-type.h"

#include "zc/core/arena.h"
#include "zc/core/array.h"
#include "zc/core/map.h"
#include "zc/core/vector.h"

namespace zomlang {
namespace compiler {
namespace type {

struct ObjectType::Impl {
  struct StoredMember {
    zc::StringPtr name;
    zc::Own<Type> type;
  };

  zc::Arena memberNameArena;
  zc::Vector<StoredMember> members;

  Impl() = default;
};

ObjectType::ObjectType() : impl(zc::heap<Impl>()) {}

ObjectType::~ObjectType() noexcept(false) = default;

ObjectType::ObjectType(ObjectType&& other) noexcept = default;

ObjectType& ObjectType::operator=(ObjectType&& other) noexcept = default;

void ObjectType::addMember(zc::StringPtr name, zc::Own<Type> type) {
  auto storedName = impl->memberNameArena.copyString(name);
  impl->members.add(Impl::StoredMember{storedName, zc::mv(type)});
}

zc::Maybe<const Type&> ObjectType::getMember(zc::StringPtr name) const {
  for (size_t i = 0; i < impl->members.size(); ++i) {
    if (impl->members[i].name == name) { return *impl->members[i].type; }
  }
  return zc::none;
}

size_t ObjectType::getMemberCount() const { return impl->members.size(); }

bool ObjectType::hasMember(zc::StringPtr name) const { return getMember(name) != zc::none; }

zc::Array<ObjectType::MemberEntry> ObjectType::getMembers() const {
  auto builder = zc::heapArrayBuilder<MemberEntry>(impl->members.size());
  for (size_t i = 0; i < impl->members.size(); ++i) {
    builder.add(MemberEntry{impl->members[i].name, *impl->members[i].type});
  }
  return builder.finish();
}

zc::String ObjectType::toString() const {
  zc::String result = zc::heapString("{ ");

  bool first = true;
  for (size_t i = 0; i < impl->members.size(); ++i) {
    if (!first) { result = zc::str(result, ", "); }
    result = zc::str(result, impl->members[i].name, ": ", impl->members[i].type->toString());
    first = false;
  }

  result = zc::str(result, " }");
  return result;
}

bool ObjectType::equals(const Type& other) const {
  if (this == &other) { return true; }
  if (other.getKind() != TypeKind::Object) { return false; }

  auto& otherObj = static_cast<const ObjectType&>(other);

  if (impl->members.size() != otherObj.impl->members.size()) { return false; }

  // Check all members match
  for (size_t i = 0; i < impl->members.size(); ++i) {
    auto name = impl->members[i].name;
    auto thisType = getMember(name);
    auto otherType = otherObj.getMember(name);

    if (thisType == zc::none || otherType == zc::none) { return false; }

    ZC_IF_SOME(t, thisType) {
      ZC_IF_SOME(o, otherType) {
        if (!t.equals(o)) { return false; }
      }
    }
  }

  return true;
}

bool ObjectType::isSubtypeOf(const Type& other) const {
  if (hasBasicSubtypeRelation(other)) { return true; }

  if (other.getKind() != TypeKind::Object) { return false; }

  auto& otherObj = static_cast<const ObjectType&>(other);

  // Width subtyping: this must have all members of other (and possibly more)
  // Each shared member must be covariant
  for (size_t i = 0; i < otherObj.impl->members.size(); ++i) {
    auto name = otherObj.impl->members[i].name;
    auto thisMember = getMember(name);
    auto otherMember = otherObj.getMember(name);

    if (thisMember == zc::none) {
      // This object is missing a member that the supertype has - not a subtype
      return false;
    }

    ZC_IF_SOME(thisType, thisMember) {
      ZC_IF_SOME(otherType, otherMember) {
        if (!thisType.isSubtypeOf(otherType)) { return false; }
      }
    }
  }

  return true;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
