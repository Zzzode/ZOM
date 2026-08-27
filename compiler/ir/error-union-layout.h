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
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::ir {

/// \brief Tag-discriminant width of an error-union representation (RFC 0006).
///
/// Tags are `U8 = 0x01`, `U16 = 0x02`, `U32 = 0x03`, and `U64 = 0x04`.
enum class ErrorUnionTagWidth : uint8_t { U8 = 0x01, U16 = 0x02, U32 = 0x03, U64 = 0x04 };

/// \brief Role of one error-union alternative (RFC 0006).
///
/// Tags are `Success = 0x01` and `Residual = 0x02`.
enum class ErrorUnionAlternativeKind : uint8_t { Success = 0x01, Residual = 0x02 };

/// \brief One alternative of an error-union layout.
///
/// The type key is the opaque RFC 0005 canonical structural key; the codec only
/// byte-strings it. `tag` is the discriminant value (`0` for the success
/// alternative, `1..n` for residual alternatives in canonical residual-key
/// order). `payloadSize` and `payloadAlign` describe the alternative payload.
struct ErrorUnionAlternativeLayout final {
  uint64_t tag = 0;
  zc::Array<uint8_t> typeKey;
  ErrorUnionAlternativeKind kind = ErrorUnionAlternativeKind::Success;
  uint64_t payloadSize = 0;
  uint64_t payloadAlign = 0;

  ZC_NODISCARD ErrorUnionAlternativeLayout clone() const {
    zc::Array<uint8_t> keyCopy = zc::heapArray<uint8_t>(typeKey.asPtr());
    return ErrorUnionAlternativeLayout{tag, zc::mv(keyCopy), kind, payloadSize, payloadAlign};
  }
};

/// \brief Complete target-independent-input layout descriptor for one error union.
///
/// Fields encode in declaration order (RFC 0006). Every `SemanticTypeKey` is the
/// opaque RFC 0005 canonical structural key represented as its raw bytes; the
/// codec contributes each as a byte string. `checkedFactsRevision`,
/// `dispatchFactsRevision`, and `targetSpecId` are 32-byte digests. Integer
/// fields use RFC 0011 `UInt` (8-byte big-endian) encoding.
struct ErrorUnionLayoutDescriptor final {
  zc::Array<uint8_t> valueTypeKey;
  zc::Array<uint8_t> successTypeKey;
  zc::Vector<zc::Array<uint8_t>> residualTypeKeys;
  zc::Array<uint8_t> checkedFactsRevision;
  zc::Array<uint8_t> dispatchFactsRevision;
  zc::Array<uint8_t> targetSpecId;
  ErrorUnionTagWidth tagWidth = ErrorUnionTagWidth::U8;
  uint64_t tagOffset = 0;
  uint64_t payloadOffset = 0;
  uint64_t payloadSize = 0;
  uint64_t payloadAlign = 0;
  uint64_t size = 0;
  uint64_t align = 0;
  zc::Vector<ErrorUnionAlternativeLayout> alternatives;

  ZC_NODISCARD ErrorUnionLayoutDescriptor clone() const {
    zc::Vector<zc::Array<uint8_t>> residualCopy;
    for (const auto& key : residualTypeKeys) {
      residualCopy.add(zc::heapArray<uint8_t>(key.asPtr()));
    }
    zc::Vector<ErrorUnionAlternativeLayout> alternativeCopy;
    for (const auto& alternative : alternatives) { alternativeCopy.add(alternative.clone()); }
    return ErrorUnionLayoutDescriptor{zc::heapArray<uint8_t>(valueTypeKey.asPtr()),
                                      zc::heapArray<uint8_t>(successTypeKey.asPtr()),
                                      zc::mv(residualCopy),
                                      zc::heapArray<uint8_t>(checkedFactsRevision.asPtr()),
                                      zc::heapArray<uint8_t>(dispatchFactsRevision.asPtr()),
                                      zc::heapArray<uint8_t>(targetSpecId.asPtr()),
                                      tagWidth,
                                      tagOffset,
                                      payloadOffset,
                                      payloadSize,
                                      payloadAlign,
                                      size,
                                      align,
                                      zc::mv(alternativeCopy)};
  }
};

/// \brief One already-verified error-union layout: encoded descriptor plus its revision.
///
/// `encodedDescriptor` is the canonical `zom.error-union-layout` byte stream of the
/// descriptor (as produced by ErrorUnionLayoutCodec::encode); the manifest codec
/// byte-strings this blob directly rather than re-encoding, so the descriptor and
/// its paired revision are bound exactly as verified. The revision digest is paired
/// with the descriptor immediately preceding it in the manifest stream.
struct VerifiedErrorUnionLayout final {
  zc::Array<uint8_t> encodedDescriptor;
  zc::Array<uint8_t> revision;

  ZC_NODISCARD VerifiedErrorUnionLayout clone() const {
    return VerifiedErrorUnionLayout{zc::heapArray<uint8_t>(encodedDescriptor.asPtr()),
                                    zc::heapArray<uint8_t>(revision.asPtr())};
  }
};

/// \brief Cross-module target-artifact ABI manifest (RFC 0006).
///
/// Binds one module's interface revision and target-spec identity to the sorted
/// unique sequence of verified error-union layouts it publishes. `moduleKey` is
/// the expanded canonical module key bytes; `interfaceRevision` and `targetSpecId`
/// are 32-byte digests.
struct TargetArtifactAbiManifest final {
  zc::Array<uint8_t> moduleKey;
  zc::Array<uint8_t> interfaceRevision;
  zc::Array<uint8_t> targetSpecId;
  zc::Vector<VerifiedErrorUnionLayout> errorUnionLayouts;
};

}  // namespace zomlang::compiler::ir
