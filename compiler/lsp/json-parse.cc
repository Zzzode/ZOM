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

#include "compiler/lsp/json-parse.h"

#include <cmath>
#include <cstdint>

#include "yyjson.h"
#include "zc/core/debug.h"
#include "zc/core/encoding.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::lsp {
namespace {

// Owns a yyjson_doc so every early return frees the whole DOM exactly once. The
// yyjson reader allocates the DOM up front; on any conversion failure this
// disposer runs the single yyjson_doc_free cleanup.
class YyjsonDoc final {
public:
  explicit YyjsonDoc(yyjson_doc* doc) : doc(doc) {}
  ~YyjsonDoc() {
    if (doc != nullptr) { yyjson_doc_free(doc); }
  }
  YyjsonDoc(YyjsonDoc&&) = delete;
  YyjsonDoc& operator=(YyjsonDoc&&) = delete;
  YyjsonDoc(const YyjsonDoc&) = delete;
  YyjsonDoc& operator=(const YyjsonDoc&) = delete;

  ZC_NODISCARD yyjson_doc* get() const { return doc; }

private:
  yyjson_doc* doc;
};

// The largest integer magnitude a double represents exactly. An integer whose
// magnitude exceeds this cannot round-trip through a double, so the parser
// rejects it rather than silently rounding a JSON-RPC id or other precise value.
constexpr uint64_t kMaxExactDouble = 1ull << 53;

// Running budget threaded through the conversion so a document that yyjson
// accepts still fails closed when it exceeds any structural bound.
struct Budget final {
  const JsonLimits& limits;
  size_t nodes = 0;
  size_t totalStringBytes = 0;

  // Charges one materialized value against the node budget.
  ZC_NODISCARD bool chargeNode() {
    if (nodes >= limits.maxNodes) { return false; }
    ++nodes;
    return true;
  }

  // Charges one decoded string's bytes against the per-string and total-string
  // budgets.
  ZC_NODISCARD bool chargeString(size_t bytes) {
    if (bytes > limits.maxStringBytes) { return false; }
    if (bytes > limits.maxTotalStringBytes - totalStringBytes) { return false; }
    totalStringBytes += bytes;
    return true;
  }
};

// Whether `bytes` is well-formed UTF-8. yyjson already rejects invalid unicode
// under the strict flags, but the transport re-validates decoded strings as
// defense in depth.
bool isValidUtf8(zc::ArrayPtr<const char> bytes) { return !zc::encodeUtf16(bytes).hadErrors; }

// Converts one yyjson value into a JsonValue, enforcing the depth, node, size,
// duplicate-key, UTF-8, and number-precision bounds. `depth` is the current
// nesting depth; the array/object cases check it before descending.
zc::Maybe<JsonValue> convert(yyjson_val* value, uint32_t depth, Budget& budget) {
  if (value == nullptr) { return zc::none; }
  if (!budget.chargeNode()) { return zc::none; }

  if (yyjson_is_null(value)) { return JsonValue::null(); }
  if (yyjson_is_bool(value)) { return JsonValue::boolean(yyjson_get_bool(value)); }

  if (yyjson_is_uint(value)) {
    const uint64_t raw = yyjson_get_uint(value);
    if (raw > kMaxExactDouble) { return zc::none; }  // not lossless as a double
    return JsonValue::number(static_cast<double>(raw));
  }
  if (yyjson_is_sint(value)) {
    const int64_t raw = yyjson_get_sint(value);
    const uint64_t magnitude =
        raw < 0 ? static_cast<uint64_t>(-(raw + 1)) + 1u : static_cast<uint64_t>(raw);
    if (magnitude > kMaxExactDouble) { return zc::none; }
    return JsonValue::number(static_cast<double>(raw));
  }
  if (yyjson_is_real(value)) {
    const double raw = yyjson_get_real(value);
    if (!std::isfinite(raw)) { return zc::none; }  // JSON has no NaN/Infinity
    return JsonValue::number(raw);
  }
  // A big integer read as raw (YYJSON_READ_BIGNUM_AS_RAW) cannot be represented
  // as a finite double without losing precision, so it is rejected.
  if (yyjson_is_raw(value)) { return zc::none; }

  if (yyjson_is_str(value)) {
    auto text = zc::arrayPtr(yyjson_get_str(value), yyjson_get_len(value));
    if (!budget.chargeString(text.size())) { return zc::none; }
    if (!isValidUtf8(text)) { return zc::none; }
    return JsonValue::string(zc::heapString(text));
  }

  if (yyjson_is_arr(value)) {
    if (depth >= budget.limits.maxDepth) { return zc::none; }
    zc::Vector<JsonValue> elements;
    yyjson_arr_iter iter = yyjson_arr_iter_with(value);
    yyjson_val* element = nullptr;
    while ((element = yyjson_arr_iter_next(&iter)) != nullptr) {
      if (elements.size() >= budget.limits.maxArrayElements) { return zc::none; }
      auto converted = convert(element, depth + 1, budget);
      if (converted == zc::none) { return zc::none; }
      elements.add(zc::mv(ZC_ASSERT_NONNULL(converted)));
    }
    return JsonValue::array(elements.releaseAsArray());
  }

  if (yyjson_is_obj(value)) {
    if (depth >= budget.limits.maxDepth) { return zc::none; }
    zc::Vector<JsonMember> members;
    yyjson_obj_iter iter = yyjson_obj_iter_with(value);
    yyjson_val* key = nullptr;
    while ((key = yyjson_obj_iter_next(&iter)) != nullptr) {
      if (members.size() >= budget.limits.maxObjectMembers) { return zc::none; }
      auto keyText = zc::arrayPtr(yyjson_get_str(key), yyjson_get_len(key));
      if (!budget.chargeString(keyText.size())) { return zc::none; }
      if (!isValidUtf8(keyText)) { return zc::none; }
      // yyjson permits duplicate keys; the transport rejects them.
      for (const auto& existing : members) {
        if (existing.key == keyText) { return zc::none; }
      }
      auto converted = convert(yyjson_obj_iter_get_val(key), depth + 1, budget);
      if (converted == zc::none) { return zc::none; }
      members.add(JsonMember{zc::heapString(keyText), zc::mv(ZC_ASSERT_NONNULL(converted))});
    }
    return JsonValue::object(members.releaseAsArray());
  }

  return zc::none;  // an unexpected yyjson value kind
}

}  // namespace

zc::Maybe<JsonValue> parseJson(zc::ArrayPtr<const uint8_t> bytes, const JsonLimits& limits) {
  if (bytes.size() > limits.maxInputBytes) { return zc::none; }
  // yyjson does not modify the input without YYJSON_READ_INSITU, so a const cast
  // to its char* parameter is safe. Strict flags reject comments, trailing
  // commas, infinities, NaN, and invalid unicode; without
  // YYJSON_READ_STOP_WHEN_DONE, trailing non-whitespace after the top-level value
  // is an error. YYJSON_READ_BIGNUM_AS_RAW makes an integer too large for a finite
  // double a raw value, which the converter rejects rather than rounding.
  yyjson_read_err err;
  yyjson_doc* raw =
      yyjson_read_opts(const_cast<char*>(reinterpret_cast<const char*>(bytes.begin())),
                       bytes.size(), YYJSON_READ_BIGNUM_AS_RAW, nullptr, &err);
  if (raw == nullptr) { return zc::none; }
  YyjsonDoc doc(raw);
  Budget budget{limits};
  return convert(yyjson_doc_get_root(doc.get()), 0, budget);
}

}  // namespace zomlang::compiler::lsp
