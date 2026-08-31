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

#include "zc/core/common.h"

namespace zomlang::compiler::ide {

/// \brief One LSP document version for one editor open lifecycle.
///
/// RFC 0023 "IDE Semantic Snapshots" (Normative Terms, L305): a document version
/// is one LSP `integer`, a signed 32-bit value, that strictly increases within
/// one open lifecycle. The `didOpen` version may be any value in
/// [-2^31, 2^31 - 1]; negative values are valid (L370-377). Successive versions
/// need not be consecutive, so ordering is decided by strict `>` alone, never by
/// a `previous + 1` relation.
///
/// This is an immutable 32-bit value with no interior mutable state and no
/// synchronization; it makes no thread-safety promise. Strict monotonicity within
/// one open lifecycle is enforced serially by the LSP adapter (RFC 0023
/// L370-377); this value type only supplies the `succeeds` decision the adapter
/// consumes. Out-of-range, equal, and lower versions are invalid-parameters
/// protocol violations handled by the adapter, not rejected here: every 32-bit
/// value constructs.
class DocumentVersion final {
public:
  /// \brief The version an open lifecycle begins at.
  ///
  /// Any signed 32-bit value is a valid `didOpen` version, so this construction
  /// never fails.
  ///
  /// \param value The LSP integer version supplied by the editor.
  /// \return The document version wrapping `value`.
  ZC_NODISCARD static constexpr DocumentVersion initial(int32_t value) noexcept {
    return DocumentVersion(value);
  }

  /// \brief Whether this version strictly follows `previous` in one lifecycle.
  ///
  /// A `didChange` version must be strictly greater than the preceding version;
  /// values need not be consecutive. Equal or lower versions do not succeed.
  ///
  /// \param previous The preceding version in the same open lifecycle.
  /// \return `true` when this version is strictly greater than `previous`.
  ZC_NODISCARD constexpr bool succeeds(DocumentVersion previous) const noexcept {
    return _value > previous._value;
  }

  /// \brief The underlying signed 32-bit LSP integer.
  ZC_NODISCARD constexpr int32_t raw() const noexcept { return _value; }

  ZC_NODISCARD constexpr bool operator==(DocumentVersion other) const noexcept {
    return _value == other._value;
  }
  ZC_NODISCARD constexpr bool operator!=(DocumentVersion other) const noexcept {
    return _value != other._value;
  }

private:
  explicit constexpr DocumentVersion(int32_t value) noexcept : _value(value) {}

  int32_t _value;
};

}  // namespace zomlang::compiler::ide
