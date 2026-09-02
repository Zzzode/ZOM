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
// See the License for the specific language governing permissions and limitations under
// the License.

#include "compiler/lsp/json-value.h"

#include <cmath>

#include "zc/core/debug.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::lsp {

zc::Maybe<JsonValue> JsonValue::number(double value) {
  if (!std::isfinite(value)) { return zc::none; }  // NaN/Infinity are not JSON
  return JsonValue(value);
}

zc::Maybe<const JsonValue&> JsonValue::find(zc::StringPtr key) const {
  if (!isObject()) { return zc::none; }
  for (const auto& member : asObject()) {
    if (member.key == key) { return member.value; }
  }
  return zc::none;
}

JsonValue JsonValue::clone() const {
  if (isNull()) { return JsonValue::null(); }
  if (isBool()) { return JsonValue::boolean(asBool()); }
  if (isNumber()) {
    // The finite invariant holds for every constructed number, so cloning one
    // always succeeds; assert rather than propagate an impossible failure.
    auto copy = JsonValue::number(asNumber());
    return zc::mv(ZC_ASSERT_NONNULL(copy));
  }
  if (isString()) { return JsonValue::string(zc::heapString(asString())); }
  if (isArray()) {
    auto source = asArray();
    zc::Vector<JsonValue> elements(source.size());
    for (const auto& element : source) { elements.add(element.clone()); }
    return JsonValue::array(elements.releaseAsArray());
  }
  // Object.
  auto source = asObject();
  zc::Vector<JsonMember> members(source.size());
  for (const auto& member : source) {
    members.add(JsonMember{zc::heapString(member.key), member.value.clone()});
  }
  return JsonValue::object(members.releaseAsArray());
}

}  // namespace zomlang::compiler::lsp
