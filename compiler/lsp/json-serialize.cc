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

#include "compiler/lsp/json-serialize.h"

#include <cmath>

#include "compiler/basic/string-escape.h"
#include "zc/core/debug.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::lsp {
namespace {

void appendStr(zc::Vector<uint8_t>& out, zc::StringPtr text) {
  for (char c : text) { out.add(static_cast<uint8_t>(c)); }
}

// Emits a JSON string literal: opening quote, escaped contents, closing quote.
void writeString(zc::Vector<uint8_t>& out, zc::StringPtr text) {
  out.add(static_cast<uint8_t>('"'));
  appendStr(out, basic::escapeJsonString(text));
  out.add(static_cast<uint8_t>('"'));
}

void writeValue(zc::Vector<uint8_t>& out, const JsonValue& value) {
  if (value.isNull()) {
    appendStr(out, "null"_zc);
  } else if (value.isBool()) {
    appendStr(out, value.asBool() ? "true"_zc : "false"_zc);
  } else if (value.isNumber()) {
    const double n = value.asNumber();
    // Every constructed JsonValue number is finite (JsonValue::number fails
    // closed on NaN/Infinity), so this is an invariant check, not data handling:
    // a non-finite value here is a construction-path bug, not a value to silently
    // rewrite. Fail loudly rather than emit a wrong or invalid token.
    ZC_ASSERT(std::isfinite(n), "JsonValue number invariant violated: non-finite");
    appendStr(out, zc::str(n));
  } else if (value.isString()) {
    writeString(out, value.asString());
  } else if (value.isArray()) {
    out.add(static_cast<uint8_t>('['));
    bool first = true;
    for (const auto& element : value.asArray()) {
      if (!first) { out.add(static_cast<uint8_t>(',')); }
      first = false;
      writeValue(out, element);
    }
    out.add(static_cast<uint8_t>(']'));
  } else {
    // Object: emit members in stored order for a deterministic round trip.
    out.add(static_cast<uint8_t>('{'));
    bool first = true;
    for (const auto& member : value.asObject()) {
      if (!first) { out.add(static_cast<uint8_t>(',')); }
      first = false;
      writeString(out, member.key);
      out.add(static_cast<uint8_t>(':'));
      writeValue(out, member.value);
    }
    out.add(static_cast<uint8_t>('}'));
  }
}

}  // namespace

zc::Array<uint8_t> serializeJson(const JsonValue& value) {
  zc::Vector<uint8_t> out;
  writeValue(out, value);
  return out.releaseAsArray();
}

}  // namespace zomlang::compiler::lsp
