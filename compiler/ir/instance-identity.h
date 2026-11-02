// Copyright (c) 2026 Zode.Z. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/string.h"

namespace zomlang::compiler::ir {

/// \brief Leading domain byte of one length-framed monomorphized instance identity.
///
/// A Source call edge is one canonical byte string: a big-endian u64 length
/// prefix whose first byte is always 0x00 (the 32-byte definition key length).
/// The instance frame opens with this non-zero tag, so no byte sequence can be
/// accepted as both framings.
constexpr uint8_t kInstanceIdentityDomainTag = 0x4d;

/// \brief Components of one decoded instance identity frame.
struct DecodedInstanceIdentity final {
  zc::Array<uint8_t> definitionKey;
  zc::Array<uint8_t> substitutionRecord;
  zc::Array<uint8_t> witnessRecord;
};

/// \brief Encodes the RFC 0056 instance identity frame: domain tag followed by
/// three length-framed components (definition key, substitution record, witness
/// record). The encoding is structurally injective for future variable-length
/// witness records.
/// \param definitionKey Raw definition key bytes.
/// \param substitutionRecord Frozen substitution store record, served verbatim.
/// \param witnessRecord Frozen witness store record, served verbatim.
/// \return The framed bytes, or none when any component is invalid.
ZC_NODISCARD zc::Maybe<zc::Array<uint8_t>> encodeInstanceIdentity(
    zc::ArrayPtr<const uint8_t> definitionKey, zc::ArrayPtr<const uint8_t> substitutionRecord,
    zc::ArrayPtr<const uint8_t> witnessRecord);

/// \brief Decodes one instance identity frame produced by encodeInstanceIdentity.
/// \param bytes Frame bytes.
/// \return The decoded components, or none when the framing is malformed or
/// trailing bytes remain.
ZC_NODISCARD zc::Maybe<DecodedInstanceIdentity> decodeInstanceIdentity(
    zc::ArrayPtr<const uint8_t> bytes);

/// \brief Derives the canonical object symbol for one monomorphized instance.
///
/// The symbol is `zom.mono.<definition-key hex>.<digest>`, where the digest is
/// the SHA-256 over the length-framed substitution record followed, from the
/// first non-empty witness record onward, by the length-framed witness record.
/// The domain cannot intersect the reserved `zom.module_init`, `zom.caller`,
/// `zom.callee`, `zom.leaf`, or `zom.standalone_<N>` symbols.
/// \param identityFrame Frame produced by encodeInstanceIdentity.
/// \return The ASCII symbol, or none when the frame is malformed or hashing fails.
ZC_NODISCARD zc::Maybe<zc::String> deriveMonoInstanceSymbol(
    zc::ArrayPtr<const uint8_t> identityFrame);

}  // namespace zomlang::compiler::ir
