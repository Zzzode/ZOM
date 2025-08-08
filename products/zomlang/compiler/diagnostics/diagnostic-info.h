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

#include "zc/core/string.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"

namespace zomlang {
namespace compiler {
namespace diagnostics {

struct DiagnosticInfo {
  DiagID id;
  DiagSeverity severity;
  zc::StringPtr message;
  size_t argCount;
};

template <DiagID Id>
struct DiagnosticTraits;

#define DIAG(Name, Severity, Message, Args)                          \
  template <>                                                        \
  struct DiagnosticTraits<DiagID::Name> {                            \
    static constexpr DiagSeverity severity = DiagSeverity::Severity; \
    static constexpr zc::StringPtr message = Message##_zcc;          \
    static constexpr size_t argCount = Args;                         \
  };
#include "zomlang/compiler/diagnostics/diagnostics-common.def"
#include "zomlang/compiler/diagnostics/diagnostics-parse.def"
#include "zomlang/compiler/diagnostics/diagnostics-sema.def"
#undef DIAG

namespace detail {

template <DiagID Id>
constexpr DiagnosticInfo getDiagnosticInfoImpl() {
  return DiagnosticInfo{
      Id,
      DiagnosticTraits<Id>::severity,
      DiagnosticTraits<Id>::message,
      DiagnosticTraits<Id>::argCount,
  };
}

}  // namespace detail

constexpr DiagnosticInfo getDiagnosticInfo(const DiagID id) {
  switch (id) {
#define DIAG(Name, ...) \
  case DiagID::Name:    \
    return detail::getDiagnosticInfoImpl<DiagID::Name>();
#include "zomlang/compiler/diagnostics/diagnostics-common.def"
#include "zomlang/compiler/diagnostics/diagnostics-parse.def"
#include "zomlang/compiler/diagnostics/diagnostics-sema.def"

#undef DIAG
    default:
      // Handle unknown DiagID
      return DiagnosticInfo{id, DiagSeverity::kError, "Unknown diagnostic"_zcc, 0};
  }
}

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang