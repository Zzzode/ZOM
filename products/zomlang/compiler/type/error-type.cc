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

#include "zomlang/compiler/type/error-type.h"

namespace zomlang {
namespace compiler {
namespace type {

struct ErrorType::Impl {
  zc::StringPtr message;

  Impl() : message(""_zc) {}
  explicit Impl(zc::StringPtr msg) : message(msg) {}
};

ErrorType::ErrorType() : Type(TypeKind::Error), impl(zc::heap<Impl>()) {}

ErrorType::ErrorType(zc::StringPtr message)
    : Type(TypeKind::Error), impl(zc::heap<Impl>(message)) {}

ErrorType::~ErrorType() noexcept(false) = default;

ErrorType::ErrorType(ErrorType&& other) noexcept = default;

ErrorType& ErrorType::operator=(ErrorType&& other) noexcept = default;

zc::StringPtr ErrorType::getMessage() const { return impl->message; }

zc::String ErrorType::toString() const {
  if (impl->message.size() > 0) { return zc::str("<error: ", impl->message, ">"); }
  return zc::heapString("<error>");
}

bool ErrorType::equals(const Type& other) const {
  if (this == &other) { return true; }
  // All error types are considered equal for error recovery
  return other.getKind() == TypeKind::Error;
}

bool ErrorType::isSubtypeOf(const Type& other) const {
  // Error type is compatible with everything to prevent cascading errors
  return true;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
