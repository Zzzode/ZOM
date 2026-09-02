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

#include "compiler/lsp/json-value.h"
#include "zc/core/array.h"
#include "zc/core/common.h"

namespace zomlang::compiler::lsp {

/// \brief Serializes a JSON value to compact, deterministic UTF-8 bytes.
///
/// RFC 0023 "IDE Semantic Snapshots" LSP transport (T1): the output is compact
/// (no insignificant whitespace) and deterministic. Object members are emitted in
/// their stored order, so a value built by the parser round-trips its key order;
/// strings are escaped by the shared `basic::escapeJsonString`; numbers use a
/// fixed shortest-round-trip formatting. The output is finite: every value is a
/// finite tree, and a non-finite number (which the parser never produces) is
/// emitted as `0` rather than an invalid `NaN`/`Infinity` token.
///
/// \param value The value to serialize.
/// \return The serialized UTF-8 bytes.
ZC_NODISCARD zc::Array<uint8_t> serializeJson(const JsonValue& value);

}  // namespace zomlang::compiler::lsp
