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

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"

namespace zomlang::compiler::lsp {

class JsonValue;

/// \brief One member of a JSON object: an owned key and its value.
///
/// RFC 0023 "IDE Semantic Snapshots" LSP transport (T1): members are stored in
/// document order in a `zc::Array<JsonMember>`. The parser rejects duplicate keys
/// before an object value is built, so a member's key is unique within its object.
struct JsonMember;

/// \brief An in-memory JSON value used only by the LSP transport layer.
///
/// RFC 0023 confines all JSON knowledge to the transport boundary and forbids
/// adding JSON serialization to any compiler or IDE semantic type; this value is
/// the transport's own model. It is a closed sum over the six JSON kinds. Arrays
/// and objects hold their children by value through `zc::Array`, which is
/// pointer-sized, so the recursive type is well-formed while incomplete.
///
/// Numbers are IEEE-754 doubles restricted to finite values: the parser rejects
/// NaN and infinities (JSON has no literal for them) and out-of-range magnitudes.
class JsonValue final {
public:
  /// \brief The JSON null literal.
  struct Null {};

  JsonValue(JsonValue&&) noexcept = default;
  JsonValue& operator=(JsonValue&&) noexcept = default;
  ZC_DISALLOW_COPY(JsonValue);
  ~JsonValue() noexcept = default;

  /// \brief Builds a null value.
  ZC_NODISCARD static JsonValue null() { return JsonValue(Null{}); }
  /// \brief Builds a boolean value.
  ZC_NODISCARD static JsonValue boolean(bool value) { return JsonValue(value); }
  /// \brief Builds a finite-number value, or none when `value` is not finite.
  ///
  /// JSON has no literal for NaN or the infinities, so a non-finite number can
  /// never be a valid JSON value. Construction fails closed rather than admitting
  /// a value that would have to be silently rewritten at serialization time. This
  /// makes "every constructed `JsonValue` number is finite" an invariant the
  /// parser, `clone`, and the serializer all rely on.
  ZC_NODISCARD static zc::Maybe<JsonValue> number(double value);
  /// \brief Builds a string value from owned UTF-8 bytes.
  ZC_NODISCARD static JsonValue string(zc::String value) { return JsonValue(zc::mv(value)); }
  /// \brief Builds an array value from owned elements.
  ZC_NODISCARD static JsonValue array(zc::Array<JsonValue> elements) {
    return JsonValue(zc::mv(elements));
  }
  /// \brief Builds an object value from owned, order-preserving members.
  ZC_NODISCARD static JsonValue object(zc::Array<JsonMember> members) {
    return JsonValue(zc::mv(members));
  }

  ZC_NODISCARD bool isNull() const noexcept { return storage.is<Null>(); }
  ZC_NODISCARD bool isBool() const noexcept { return storage.is<bool>(); }
  ZC_NODISCARD bool isNumber() const noexcept { return storage.is<double>(); }
  ZC_NODISCARD bool isString() const noexcept { return storage.is<zc::String>(); }
  ZC_NODISCARD bool isArray() const noexcept { return storage.is<zc::Array<JsonValue>>(); }
  ZC_NODISCARD bool isObject() const noexcept { return storage.is<zc::Array<JsonMember>>(); }

  /// \brief The boolean payload; valid only when `isBool()`.
  ZC_NODISCARD bool asBool() const { return storage.get<bool>(); }
  /// \brief The number payload; valid only when `isNumber()`.
  ZC_NODISCARD double asNumber() const { return storage.get<double>(); }
  /// \brief The string payload; valid only when `isString()`.
  ZC_NODISCARD zc::StringPtr asString() const ZC_LIFETIMEBOUND { return storage.get<zc::String>(); }
  /// \brief The array elements; valid only when `isArray()`.
  ZC_NODISCARD zc::ArrayPtr<const JsonValue> asArray() const ZC_LIFETIMEBOUND {
    return storage.get<zc::Array<JsonValue>>();
  }
  /// \brief The object members in document order; valid only when `isObject()`.
  ZC_NODISCARD zc::ArrayPtr<const JsonMember> asObject() const ZC_LIFETIMEBOUND {
    return storage.get<zc::Array<JsonMember>>();
  }

  /// \brief Finds an object member by key, or none.
  ///
  /// Returns none when this value is not an object or no member has `key`. The
  /// parser guarantees unique keys, so at most one member matches.
  ZC_NODISCARD zc::Maybe<const JsonValue&> find(zc::StringPtr key) const ZC_LIFETIMEBOUND;

  /// \brief A deep copy; JSON values are otherwise move-only.
  ZC_NODISCARD JsonValue clone() const;

private:
  explicit JsonValue(Null value) : storage(value) {}
  explicit JsonValue(bool value) : storage(value) {}
  explicit JsonValue(double value) : storage(value) {}
  explicit JsonValue(zc::String value) : storage(zc::mv(value)) {}
  explicit JsonValue(zc::Array<JsonValue> value) : storage(zc::mv(value)) {}
  explicit JsonValue(zc::Array<JsonMember> value) : storage(zc::mv(value)) {}

  zc::OneOf<Null, bool, double, zc::String, zc::Array<JsonValue>, zc::Array<JsonMember>> storage;
};

struct JsonMember final {
  zc::String key;
  JsonValue value;
};

}  // namespace zomlang::compiler::lsp
