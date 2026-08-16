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

#include "zomlang/compiler/identity/canonical/canonical-scalar.h"

#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/identity/text/unicode-normalization.h"
#include "zomlang/compiler/lexer/utils.h"

namespace zomlang::compiler::identity {
namespace {

bool isAsciiLower(char value) { return value >= 'a' && value <= 'z'; }

bool isAsciiUpper(char value) { return value >= 'A' && value <= 'Z'; }

bool isAsciiDigit(char value) { return value >= '0' && value <= '9'; }

bool hasLowerIdentifierShape(zc::StringPtr value, bool allowHyphen) {
  if (value.size() == 0 || value.size() > 64 || !isAsciiLower(value[0])) { return false; }
  for (size_t index = 1; index < value.size(); ++index) {
    const char current = value[index];
    if (!isAsciiLower(current) && !isAsciiDigit(current) && current != '_' &&
        (!allowHyphen || current != '-')) {
      return false;
    }
  }
  return true;
}

bool hasTargetComponentShape(zc::StringPtr value) {
  if (value.size() == 0 || value.size() > 64 ||
      (!isAsciiLower(value[0]) && !isAsciiDigit(value[0]))) {
    return false;
  }
  for (size_t index = 1; index < value.size(); ++index) {
    const char current = value[index];
    if (!isAsciiLower(current) && !isAsciiDigit(current) && current != '_' && current != '.' &&
        current != '-') {
      return false;
    }
  }
  return true;
}

bool hasEnvironmentShape(zc::StringPtr value) {
  if (value.size() == 0 || value.size() > 128 || (!isAsciiUpper(value[0]) && value[0] != '_')) {
    return false;
  }
  for (size_t index = 1; index < value.size(); ++index) {
    const char current = value[index];
    if (!isAsciiUpper(current) && !isAsciiDigit(current) && current != '_') { return false; }
  }
  return true;
}

bool isReservedKeyword(zc::StringPtr value) {
  return lexer::isReservedKeyword(lexer::getKeywordKind(value.asBytes()));
}

bool isSemanticIdentifier(zc::StringPtr value) {
  return lexer::isValidIdentifier(value) && !isReservedKeyword(value);
}

bool isPathSegment(zc::StringPtr value) {
  if (value.size() == 0 || value == "."_zc || value == ".."_zc) { return false; }
  for (char current : value) {
    if (current == '\0' || current == '/' || current == '\\') { return false; }
  }
  return true;
}

bool validate(CanonicalScalarDomain domain, zc::StringPtr value) {
  switch (domain) {
    case CanonicalScalarDomain::PathSegment:
      return isPathSegment(value);
    case CanonicalScalarDomain::PackageName:
      return hasLowerIdentifierShape(value, false);
    case CanonicalScalarDomain::TargetName:
    case CanonicalScalarDomain::DependencyAlias:
      return hasLowerIdentifierShape(value, false) && !isReservedKeyword(value);
    case CanonicalScalarDomain::FeatureName:
      return hasLowerIdentifierShape(value, true);
    case CanonicalScalarDomain::TargetComponentName:
    case CanonicalScalarDomain::TargetFeatureName:
      return hasTargetComponentShape(value);
    case CanonicalScalarDomain::SemanticEnvironmentName:
      return hasEnvironmentShape(value);
    case CanonicalScalarDomain::SemanticIdentifier:
    case CanonicalScalarDomain::ModulePathSegment:
      return isSemanticIdentifier(value);
    case CanonicalScalarDomain::DeclaredDefinitionName:
      return value == "this"_zc || value == "init"_zc || value == "deinit"_zc ||
             value == "get"_zc || value == "set"_zc || isSemanticIdentifier(value);
  }
  return false;
}

uint64_t maximumCanonicalBytes(CanonicalScalarDomain domain) {
  switch (domain) {
    case CanonicalScalarDomain::PackageName:
    case CanonicalScalarDomain::TargetName:
    case CanonicalScalarDomain::DependencyAlias:
    case CanonicalScalarDomain::FeatureName:
    case CanonicalScalarDomain::TargetComponentName:
    case CanonicalScalarDomain::TargetFeatureName:
      return 64;
    case CanonicalScalarDomain::SemanticEnvironmentName:
      return 128;
    case CanonicalScalarDomain::PathSegment:
    case CanonicalScalarDomain::SemanticIdentifier:
    case CanonicalScalarDomain::ModulePathSegment:
    case CanonicalScalarDomain::DeclaredDefinitionName:
      return 4096;
  }
  ZC_UNREACHABLE
}

zc::Maybe<zc::String> admit(CanonicalScalarDomain domain, zc::StringPtr input,
                            bool requireCanonical) {
  if (input.size() > maximumCanonicalBytes(domain)) { return zc::none; }
  auto normalized = normalizeNfc(input);
  ZC_IF_SOME(value, normalized) {
    if (requireCanonical && value != input) { return zc::none; }
    if (!validate(domain, value)) { return zc::none; }
    return zc::mv(value);
  }
  return zc::none;
}

}  // namespace

template <CanonicalScalarDomain Domain>
CanonicalScalar<Domain>::CanonicalScalar(zc::String&& canonical) noexcept
    : value(zc::mv(canonical)) {}

template <CanonicalScalarDomain Domain>
zc::Maybe<CanonicalScalar<Domain>> CanonicalScalar<Domain>::fromSource(zc::StringPtr input) {
  ZC_IF_SOME(canonical, admit(Domain, input, false)) { return CanonicalScalar(zc::mv(canonical)); }
  return zc::none;
}

template <CanonicalScalarDomain Domain>
zc::Maybe<CanonicalScalar<Domain>> CanonicalScalar<Domain>::fromCanonical(zc::StringPtr input) {
  ZC_IF_SOME(canonical, admit(Domain, input, true)) { return CanonicalScalar(zc::mv(canonical)); }
  return zc::none;
}

template <CanonicalScalarDomain Domain>
zc::Maybe<CanonicalScalar<Domain>> CanonicalScalar<Domain>::fromCanonical(
    zc::MemoryResource& resource, zc::StringPtr input) {
  if (input.size() > maximumCanonicalBytes(Domain)) { return zc::none; }
  ZC_IF_SOME(canonical, normalizeNfc(resource, input)) {
    if (canonical != input || !validate(Domain, canonical)) { return zc::none; }
    return CanonicalScalar(zc::mv(canonical));
  }
  return zc::none;
}

template <CanonicalScalarDomain Domain>
zc::Maybe<CanonicalScalar<Domain>> CanonicalScalar<Domain>::decodeCanonical(
    CanonicalDecoder& decoder) {
  auto bytes = decoder.decodeByteString(maximumCanonicalBytes(Domain));
  ZC_IF_SOME(value, bytes) {
    auto text = zc::str(value.asChars());
    return fromCanonical(text);
  }
  return zc::none;
}

template <CanonicalScalarDomain Domain>
CanonicalScalar<Domain> CanonicalScalar<Domain>::clone() const {
  return CanonicalScalar(zc::heapString(value));
}

template <CanonicalScalarDomain Domain>
CanonicalScalar<Domain> CanonicalScalar<Domain>::clone(zc::MemoryResource& resource) const {
  return CanonicalScalar(zc::resourceHeapString(resource, value));
}

template <CanonicalScalarDomain Domain>
zc::StringPtr CanonicalScalar<Domain>::text() const noexcept {
  return value;
}

template <CanonicalScalarDomain Domain>
void CanonicalScalar<Domain>::encode(CanonicalEncoder& encoder) const {
  encoder.encodeByteString(value.asBytes());
}

template <CanonicalScalarDomain Domain>
bool CanonicalScalar<Domain>::operator==(const CanonicalScalar& other) const noexcept {
  return value == other.value;
}

template <CanonicalScalarDomain Domain>
bool CanonicalScalar<Domain>::operator<(const CanonicalScalar& other) const noexcept {
  return value < other.value;
}

template class CanonicalScalar<CanonicalScalarDomain::PathSegment>;
template class CanonicalScalar<CanonicalScalarDomain::PackageName>;
template class CanonicalScalar<CanonicalScalarDomain::TargetName>;
template class CanonicalScalar<CanonicalScalarDomain::DependencyAlias>;
template class CanonicalScalar<CanonicalScalarDomain::FeatureName>;
template class CanonicalScalar<CanonicalScalarDomain::TargetComponentName>;
template class CanonicalScalar<CanonicalScalarDomain::TargetFeatureName>;
template class CanonicalScalar<CanonicalScalarDomain::SemanticEnvironmentName>;
template class CanonicalScalar<CanonicalScalarDomain::SemanticIdentifier>;
template class CanonicalScalar<CanonicalScalarDomain::ModulePathSegment>;
template class CanonicalScalar<CanonicalScalarDomain::DeclaredDefinitionName>;

}  // namespace zomlang::compiler::identity
