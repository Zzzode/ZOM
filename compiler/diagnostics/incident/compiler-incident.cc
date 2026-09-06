// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/diagnostics/incident/compiler-incident.h"

#include "compiler/identity/crypto/sha256.h"
#include "zc/core/debug.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::diagnostics {
namespace {

constexpr zc::StringPtr kFingerprintDomain = "zom.compiler-incident"_zc;
constexpr zc::StringPtr kSchemaDomain = "zom.compiler-incident-schema"_zc;

void appendUint16(zc::Vector<uint8_t>& output, uint16_t value) {
  output.add(static_cast<uint8_t>(value >> 8U));
  output.add(static_cast<uint8_t>(value));
}

void appendUint32(zc::Vector<uint8_t>& output, uint32_t value) {
  output.add(static_cast<uint8_t>(value >> 24U));
  output.add(static_cast<uint8_t>(value >> 16U));
  output.add(static_cast<uint8_t>(value >> 8U));
  output.add(static_cast<uint8_t>(value));
}

void appendText(zc::Vector<uint8_t>& output, zc::StringPtr text) { output.addAll(text.asBytes()); }

identity::Sha256Digest schemaDigest() {
  zc::Vector<uint8_t> schema;
  appendText(schema, kSchemaDomain);
  schema.add(0);
  appendUint32(schema, 12 * 13 * 14 + 4 * 10 + 4 * 5 + 5 * 3 + 5 * 8 * 5 + 4 * 5 + 19 * 19 * 6 + 3 +
                           3 + 19 + 17 + 12 + 11 + 21);
  for (uint32_t phase = 1; phase <= 12; ++phase) {
    for (uint32_t kind = 1; kind <= 13; ++kind) {
      for (uint32_t producer = 1; producer <= 14; ++producer) {
        appendUint16(schema, static_cast<uint16_t>(basic::CompilerIncidentDomain::Identity));
        appendUint32(schema, phase);
        appendUint32(schema, kind);
        appendUint32(schema, producer);
      }
    }
  }
  for (uint32_t phase = 1; phase <= 4; ++phase) {
    for (uint32_t kind = 1; kind <= 10; ++kind) {
      appendUint16(schema, static_cast<uint16_t>(basic::CompilerIncidentDomain::Checker));
      appendUint32(schema, phase);
      appendUint32(schema, kind);
      appendUint32(schema, 1);
    }
  }
  for (uint32_t phase = 1; phase <= 19; ++phase) {
    const bool backendPhase = phase == 14 || phase == 15 || phase >= 17;
    const auto domain =
        backendPhase ? basic::CompilerIncidentDomain::Backend : basic::CompilerIncidentDomain::Ir;
    for (uint32_t kind = 1; kind <= 19; ++kind) {
      for (uint32_t producer = 1; producer <= 6; ++producer) {
        appendUint16(schema, static_cast<uint16_t>(domain));
        appendUint32(schema, phase);
        appendUint32(schema, kind);
        appendUint32(schema, producer);
      }
    }
  }
  for (uint32_t phase = 1; phase <= 4; ++phase) {
    for (uint32_t kind = 1; kind <= 5; ++kind) {
      appendUint16(schema, static_cast<uint16_t>(basic::CompilerIncidentDomain::Checker));
      appendUint32(schema, 0x100U + phase);
      appendUint32(schema, kind);
      appendUint32(schema, 2);
    }
  }
  for (uint32_t kind = 1; kind <= 5; ++kind) {
    for (const uint32_t producer : {2U, 3U, 5U}) {
      appendUint16(schema, static_cast<uint16_t>(basic::CompilerIncidentDomain::Binder));
      appendUint32(schema, 1);
      appendUint32(schema, kind);
      appendUint32(schema, producer);
    }
  }
  for (uint32_t phase = 1; phase <= 5; ++phase) {
    for (uint32_t kind = 1; kind <= 8; ++kind) {
      for (uint32_t producer = 1; producer <= 5; ++producer) {
        appendUint16(schema, static_cast<uint16_t>(basic::CompilerIncidentDomain::Driver));
        appendUint32(schema, phase);
        appendUint32(schema, kind);
        appendUint32(schema, producer);
      }
    }
  }
  for (uint32_t phase = 1; phase <= 4; ++phase) {
    for (uint32_t kind = 1; kind <= 5; ++kind) {
      appendUint16(schema, static_cast<uint16_t>(basic::CompilerIncidentDomain::Driver));
      appendUint32(schema, phase);
      appendUint32(schema, kind);
      appendUint32(schema, 0x101U);
    }
  }
  for (uint32_t kind = 1; kind <= 3; ++kind) {
    appendUint16(schema, static_cast<uint16_t>(basic::CompilerIncidentDomain::Driver));
    appendUint32(schema, 0x201U);
    appendUint32(schema, 0x100U + kind);
    appendUint32(schema, 0x201U);
  }
  for (uint32_t kind = 1; kind <= 3; ++kind) {
    appendUint16(schema, static_cast<uint16_t>(basic::CompilerIncidentDomain::Driver));
    appendUint32(schema, 0x202U);
    appendUint32(schema, 0x100U + kind);
    appendUint32(schema, 0x202U);
  }
  for (uint32_t kind = 1; kind <= 19; ++kind) {
    appendUint16(schema, static_cast<uint16_t>(basic::CompilerIncidentDomain::Driver));
    appendUint32(schema, 0x202U);
    appendUint32(schema, 0x200U + kind);
    appendUint32(schema, 0x203U);
  }
  for (uint32_t kind = 1; kind <= 17; ++kind) {
    appendUint16(schema, static_cast<uint16_t>(basic::CompilerIncidentDomain::Driver));
    appendUint32(schema, 0x203U);
    appendUint32(schema, 0x300U + kind);
    appendUint32(schema, 0x203U);
  }
  for (uint32_t kind = 1; kind <= 12; ++kind) {
    appendUint16(schema, static_cast<uint16_t>(basic::CompilerIncidentDomain::Driver));
    appendUint32(schema, 0x204U);
    appendUint32(schema, 0x400U + kind);
    appendUint32(schema, 0x204U);
  }
  for (uint32_t kind = 1; kind <= 11; ++kind) {
    appendUint16(schema, static_cast<uint16_t>(basic::CompilerIncidentDomain::Driver));
    appendUint32(schema, 0x204U);
    appendUint32(schema, 0x500U + kind);
    appendUint32(schema, 0x204U);
  }
  for (uint32_t kind = 1; kind <= 21; ++kind) {
    appendUint16(schema, static_cast<uint16_t>(basic::CompilerIncidentDomain::Driver));
    appendUint32(schema, 0x205U);
    appendUint32(schema, 0x600U + kind);
    appendUint32(schema, 0x205U);
  }
  return ZC_REQUIRE_NONNULL(identity::sha256(schema.asPtr()));
}

zc::String lowerHex(zc::ArrayPtr<const uint8_t> bytes) {
  constexpr char digits[] = "0123456789abcdef";
  zc::Vector<char> output(bytes.size() * 2);
  for (const uint8_t byte : bytes) {
    output.add(digits[byte >> 4U]);
    output.add(digits[byte & 0x0fU]);
  }
  return zc::str(output.releaseAsArray());
}

zc::StringPtr displayDomain(const basic::BoundedIncidentSet& incidents) {
  const auto descriptors = incidents.descriptors();
  const auto domain = descriptors[0].domain();
  for (const auto& descriptor : descriptors) {
    if (descriptor.domain() != domain) { return "multiple"_zc; }
  }
  switch (domain) {
    case basic::CompilerIncidentDomain::Frontend:
      return "frontend"_zc;
    case basic::CompilerIncidentDomain::Query:
      return "query"_zc;
    case basic::CompilerIncidentDomain::Identity:
      return "identity"_zc;
    case basic::CompilerIncidentDomain::Package:
      return "package"_zc;
    case basic::CompilerIncidentDomain::Binder:
      return "binder"_zc;
    case basic::CompilerIncidentDomain::Checker:
      return "checker"_zc;
    case basic::CompilerIncidentDomain::Ownership:
      return "ownership"_zc;
    case basic::CompilerIncidentDomain::Ir:
      return "ir"_zc;
    case basic::CompilerIncidentDomain::Backend:
      return "backend"_zc;
    case basic::CompilerIncidentDomain::Driver:
      return "driver"_zc;
    case basic::CompilerIncidentDomain::Diagnostics:
      return "diagnostics"_zc;
  }
  ZC_UNREACHABLE
}

}  // namespace

zc::Maybe<zc::String> renderCompilerIncident(zc::StringPtr compilerVersion,
                                             const basic::BoundedIncidentSet& incidents) {
  if (incidents.empty() || incidents.size() > UINT32_MAX) { return zc::none; }
  const auto schema = schemaDigest();
  zc::Vector<uint8_t> preimage;
  appendText(preimage, kFingerprintDomain);
  preimage.addAll(schema.bytes());
  appendUint32(preimage, static_cast<uint32_t>(incidents.size()));
  for (const auto& descriptor : incidents.descriptors()) {
    appendUint16(preimage, static_cast<uint16_t>(descriptor.domain()));
    appendUint32(preimage, descriptor.phase().tag());
    appendUint32(preimage, descriptor.kind().tag());
    appendUint32(preimage, descriptor.producer().tag());
  }
  const auto fingerprint = identity::sha256(preimage.asPtr());
  if (fingerprint == zc::none) { return zc::none; }
  const auto schemaHex = lowerHex(schema.bytes().first(6));
  const auto fingerprintHex = lowerHex(ZC_ASSERT_NONNULL(fingerprint).bytes().first(16));
  return zc::str("error: internal compiler error\n", "note: compiler: ", compilerVersion,
                 " (incident schema ", schemaHex, ")\n", "note: phase: ", displayDomain(incidents),
                 "\n", "note: incident: ", fingerprintHex, "\n",
                 "note: please report this compiler bug and include the compiler version and "
                 "incident fingerprint");
}

}  // namespace zomlang::compiler::diagnostics
