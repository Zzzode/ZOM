// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include "compiler/diagnostics/core/diagnostic-ids.h"
#include "zc/core/string.h"

namespace zomlang {
namespace compiler {
namespace diagnostics {

struct DiagnosticInfo {
  DiagID id;
  zc::StringPtr symbolicName;
  DiagSeverity severity;
  zc::StringPtr message;
  size_t argCount;
};

template <DiagID Id>
struct DiagnosticTraits;

#define DIAG(Code, Name, Severity, Message, Args)                    \
  template <>                                                        \
  struct DiagnosticTraits<DiagID::Name> {                            \
    static constexpr DiagSeverity severity = DiagSeverity::Severity; \
    static constexpr zc::StringPtr message = Message##_zcc;          \
    static constexpr size_t argCount = Args;                         \
  };
#include "compiler/diagnostics/defs/diagnostics-binder.def"
#include "compiler/diagnostics/defs/diagnostics-checker.def"
#include "compiler/diagnostics/defs/diagnostics-common.def"
#include "compiler/diagnostics/defs/diagnostics-lowering.def"
#include "compiler/diagnostics/defs/diagnostics-module.def"
#include "compiler/diagnostics/defs/diagnostics-package.def"
#include "compiler/diagnostics/defs/diagnostics-parse.def"
#undef DIAG

namespace detail {

inline constexpr DiagnosticInfo diagnosticCatalog[] = {
#define DIAG(Code, Name, Severity, Message, Args)                                                \
  {DiagID::Name, zc::StringPtr(#Name, sizeof(#Name) - 1), DiagSeverity::Severity, Message##_zcc, \
   Args},
#include "compiler/diagnostics/defs/diagnostics-binder.def"
#include "compiler/diagnostics/defs/diagnostics-checker.def"
#include "compiler/diagnostics/defs/diagnostics-common.def"
#include "compiler/diagnostics/defs/diagnostics-lowering.def"
#include "compiler/diagnostics/defs/diagnostics-module.def"
#include "compiler/diagnostics/defs/diagnostics-package.def"
#include "compiler/diagnostics/defs/diagnostics-parse.def"
#undef DIAG
};

constexpr bool isAllocatedCode(uint32_t code) {
  return (code >= 2000 && code <= 4999) || (code >= 6000 && code <= 7999);
}

constexpr bool hasValidTemplate(const DiagnosticInfo& entry) {
  if (entry.argCount > 3) { return false; }
  bool seen[3] = {false, false, false};
  for (size_t index = 0; index < entry.message.size(); ++index) {
    const unsigned char byte = static_cast<unsigned char>(entry.message[index]);
    if (byte < 0x20 || byte == 0x7f) { return false; }
    if (entry.message[index] == '}') { return false; }
    if (entry.message[index] != '{') { continue; }
    if (index + 2 >= entry.message.size() || entry.message[index + 2] != '}' ||
        entry.message[index + 1] < '0' || entry.message[index + 1] > '9') {
      return false;
    }
    const size_t argument = static_cast<size_t>(entry.message[index + 1] - '0');
    if (argument >= entry.argCount || seen[argument]) { return false; }
    seen[argument] = true;
    index += 2;
  }
  for (size_t argument = 0; argument < entry.argCount; ++argument) {
    if (!seen[argument]) { return false; }
  }
  return true;
}

constexpr bool isValidCatalog() {
  for (size_t index = 0; index < sizeof(diagnosticCatalog) / sizeof(diagnosticCatalog[0]);
       ++index) {
    const auto& entry = diagnosticCatalog[index];
    const uint32_t code = static_cast<uint32_t>(entry.id);
    if (!isAllocatedCode(code) || (code >= 9900 && code <= 9999) ||
        entry.symbolicName.size() == 0 || entry.message.size() == 0 || !hasValidTemplate(entry)) {
      return false;
    }
    for (size_t other = 0; other < index; ++other) {
      if (diagnosticCatalog[other].id == entry.id ||
          diagnosticCatalog[other].symbolicName == entry.symbolicName) {
        return false;
      }
    }
  }
  return true;
}

static_assert(isValidCatalog(), "diagnostic catalog contract violation");

}  // namespace detail

constexpr zc::ArrayPtr<const DiagnosticInfo> getDiagnosticCatalog() noexcept {
  return zc::arrayPtr(detail::diagnosticCatalog);
}

inline const DiagnosticInfo& getDiagnosticInfo(const DiagID id) {
  for (const auto& entry : detail::diagnosticCatalog) {
    if (entry.id == id) { return entry; }
  }
  ZC_IREQUIRE(false, "unknown diagnostic identifier");
  ZC_UNREACHABLE;
}

constexpr bool isKnownDiagnostic(const DiagID id) {
  for (const auto& entry : detail::diagnosticCatalog) {
    if (entry.id == id) { return true; }
  }
  return false;
}

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang
