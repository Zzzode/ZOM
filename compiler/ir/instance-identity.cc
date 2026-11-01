// Copyright (c) 2026 Zode.Z. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/ir/instance-identity.h"

#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "zc/core/debug.h"

namespace zomlang::compiler::ir {

zc::Maybe<zc::Array<uint8_t>> encodeInstanceIdentity(
    zc::ArrayPtr<const uint8_t> definitionKey, zc::ArrayPtr<const uint8_t> substitutionRecord,
    zc::ArrayPtr<const uint8_t> witnessRecord) {
  if (definitionKey.size() == 0 || substitutionRecord.size() == 0 ||
      witnessRecord.size() == 0) {
    return zc::none;
  }
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(kInstanceIdentityDomainTag);
  encoder.encodeByteString(definitionKey);
  encoder.encodeByteString(substitutionRecord);
  encoder.encodeByteString(witnessRecord);
  return encoder.finish();
}

zc::Maybe<DecodedInstanceIdentity> decodeInstanceIdentity(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto tag = decoder.decodeUint8();
  if (tag == zc::none || ZC_ASSERT_NONNULL(tag) != kInstanceIdentityDomainTag) {
    return zc::none;
  }
  // The 32-byte definition key and the two frozen-store records are all bounded
  // by the frame itself.
  const uint64_t maximum = bytes.size();
  auto definitionKey = decoder.decodeByteString(maximum);
  auto substitutionRecord = decoder.decodeByteString(maximum);
  auto witnessRecord = decoder.decodeByteString(maximum);
  if (definitionKey == zc::none || substitutionRecord == zc::none ||
      witnessRecord == zc::none || !decoder.finished()) {
    return zc::none;
  }
  return DecodedInstanceIdentity{zc::mv(ZC_ASSERT_NONNULL(definitionKey)),
                                 zc::mv(ZC_ASSERT_NONNULL(substitutionRecord)),
                                 zc::mv(ZC_ASSERT_NONNULL(witnessRecord))};
}

}  // namespace zomlang::compiler::ir
