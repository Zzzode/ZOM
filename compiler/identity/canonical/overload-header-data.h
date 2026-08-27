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

#include "zc/core/one-of.h"
#include "compiler/identity/canonical/overload-header.h"

namespace zomlang::compiler::identity::canonical_overload_header_detail {

struct CallableResultUnitData final {};
struct CallableResultConstructorSelfData final {};
struct CallableResultTypeData final {
  CanonicalHeaderTypeSyntax type;
};

struct CanonicalCallableResultData final {
  zc::OneOf<CallableResultUnitData, CallableResultConstructorSelfData, CallableResultTypeData>
      value;
};

struct CanonicalGenericParameterData final {
  zc::Maybe<CanonicalHeaderTypeSyntax> defaultType;
};

struct CanonicalBoundObligationData final {
  CanonicalHeaderTypeSyntax subject;
  CanonicalHeaderTypeSyntax bound;
};

struct CanonicalCallableParameterData final {
  SemanticIdentifier label;
  CanonicalHeaderTypeSyntax type;
  bool hasDefault;
};

struct CanonicalOverloadHeaderData final {
  CallableHeaderKind callableKind;
  DeclaredDefinitionName name;
  zc::Maybe<ReceiverShape> receiver;
  zc::Vector<CanonicalGenericParameter> genericParameters;
  zc::Vector<CanonicalBoundObligation> obligations;
  zc::Vector<CanonicalCallableParameter> parameters;
  CanonicalCallableResult result;
  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> raises;
  zc::Maybe<ExternalAbi> externalAbi;
};

}  // namespace zomlang::compiler::identity::canonical_overload_header_detail
