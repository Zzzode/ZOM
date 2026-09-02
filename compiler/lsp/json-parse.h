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

#include <cstddef>
#include <cstdint>

#include "compiler/lsp/json-value.h"
#include "zc/core/array.h"
#include "zc/core/common.h"

namespace zomlang::compiler::lsp {

/// \brief Conservative bounds the JSON parser enforces to stay controlled.
///
/// RFC 0023 "IDE Semantic Snapshots" LSP transport (T1): the parser is fed
/// untrusted bytes off the wire, so it fails closed on any input that exceeds a
/// bound rather than consuming unbounded memory. The underlying yyjson reader is
/// iterative, so it does not overflow the native stack on deep nesting; the depth
/// bound below is a semantic policy limit enforced while converting yyjson's DOM
/// into the closed JsonValue model, alongside the node, size, and byte budgets.
struct JsonLimits final {
  /// Maximum total input length in bytes.
  size_t maxInputBytes = 8u * 1024u * 1024u;
  /// Maximum array/object nesting depth. The conversion rejects at depth
  /// `maxDepth + 1`.
  uint32_t maxDepth = 128;
  /// Maximum total number of values materialized from one document.
  size_t maxNodes = 1u << 20;
  /// Maximum number of elements in one array.
  size_t maxArrayElements = 1u << 20;
  /// Maximum number of members in one object.
  size_t maxObjectMembers = 1u << 20;
  /// Maximum decoded length, in bytes, of one JSON string (key or value).
  size_t maxStringBytes = 4u * 1024u * 1024u;
  /// Maximum summed decoded length, in bytes, of all strings in one document.
  size_t maxTotalStringBytes = 64u * 1024u * 1024u;
};

/// \brief Parses one complete JSON document from UTF-8 bytes.
///
/// Reads with the vendored yyjson library under strict options, then converts
/// yyjson's DOM into the closed JsonValue model, enforcing the bounds in
/// `limits`. Returns none, committing nothing, on any of: input longer than the
/// byte bound; malformed structure; a number that is not finite (JSON has no
/// NaN/Infinity literal) or an integer whose magnitude exceeds 2^53 and so cannot
/// convert to a double without losing precision; a duplicate object key;
/// malformed UTF-8 or an invalid `\u` surrogate; a node, string, array, object,
/// depth, or total-string bound exceeded; or any trailing non-whitespace byte
/// after the top-level value.
///
/// \param bytes The UTF-8 document bytes.
/// \param limits The bounds to enforce.
/// \return The parsed value, or none when the input is rejected.
ZC_NODISCARD zc::Maybe<JsonValue> parseJson(zc::ArrayPtr<const uint8_t> bytes,
                                            const JsonLimits& limits = JsonLimits());

}  // namespace zomlang::compiler::lsp
