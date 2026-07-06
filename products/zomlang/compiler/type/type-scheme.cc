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

#include "zomlang/compiler/type/type-scheme.h"

#include "zc/core/string.h"
#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {
namespace type {

struct TypeScheme::Impl {
  zc::Vector<zc::Own<GenericParam>> params;
  zc::Own<Type> body;

  Impl(zc::Vector<zc::Own<GenericParam>> p, zc::Own<Type> b) : params(zc::mv(p)), body(zc::mv(b)) {}
};

TypeScheme::TypeScheme(zc::Vector<zc::Own<GenericParam>> params, zc::Own<Type> body)
    : impl(zc::heap<Impl>(zc::mv(params), zc::mv(body))) {}

TypeScheme::~TypeScheme() noexcept(false) = default;

TypeScheme::TypeScheme(TypeScheme&& other) noexcept = default;

TypeScheme& TypeScheme::operator=(TypeScheme&& other) noexcept = default;

size_t TypeScheme::getParamCount() const { return impl->params.size(); }

const GenericParam& TypeScheme::getParam(size_t index) const { return *impl->params[index]; }

const Type& TypeScheme::getBody() const { return *impl->body; }

bool TypeScheme::isMonomorphic() const { return impl->params.size() == 0; }

zc::String TypeScheme::toString() const {
  if (isMonomorphic()) { return impl->body->toString(); }

  zc::String result = zc::heapString("∀");
  for (size_t i = 0; i < impl->params.size(); ++i) {
    if (i > 0) { result = zc::str(result, ","); }
    result = zc::str(result, impl->params[i]->name);
  }
  result = zc::str(result, ". ", impl->body->toString());
  return result;
}

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
