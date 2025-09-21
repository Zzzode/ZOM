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

#include "zomlang/compiler/symbol/symbol-flags.h"

#include "zc/core/debug.h"
#include "zc/ztest/test.h"

namespace zomlang {
namespace compiler {
namespace symbol {

ZC_TEST("SymbolFlags_BasicOperations") {
  SymbolFlags flags = SymbolFlags::None;

  // Test basic flag operations
  flags |= SymbolFlags::Public;
  ZC_EXPECT(hasFlag(flags, SymbolFlags::Public));
  ZC_EXPECT(!hasFlag(flags, SymbolFlags::Private));

  flags |= SymbolFlags::Static;
  ZC_EXPECT(hasFlag(flags, SymbolFlags::Public));
  ZC_EXPECT(hasFlag(flags, SymbolFlags::Static));

  flags &= ~SymbolFlags::Public;
  ZC_EXPECT(!hasFlag(flags, SymbolFlags::Public));
  ZC_EXPECT(hasFlag(flags, SymbolFlags::Static));
}

ZC_TEST("SymbolFlags_BitwiseOperators") {
  SymbolFlags flags1 = SymbolFlags::Public | SymbolFlags::Static;
  SymbolFlags flags2 = SymbolFlags::Private | SymbolFlags::Instance;

  // Test OR operation
  SymbolFlags combined = flags1 | flags2;
  ZC_EXPECT(hasFlag(combined, SymbolFlags::Public));
  ZC_EXPECT(hasFlag(combined, SymbolFlags::Static));
  ZC_EXPECT(hasFlag(combined, SymbolFlags::Private));
  ZC_EXPECT(hasFlag(combined, SymbolFlags::Instance));

  // Test AND operation
  SymbolFlags intersection = flags1 & flags2;
  ZC_EXPECT(intersection == SymbolFlags::None);

  // Test XOR operation
  SymbolFlags xorResult = flags1 ^ flags2;
  ZC_EXPECT(xorResult == combined);

  // Test NOT operation
  SymbolFlags notFlags = ~SymbolFlags::Public;
  ZC_EXPECT(!hasFlag(notFlags, SymbolFlags::Public));
}

ZC_TEST("SymbolFlags_CompoundAssignmentOperators") {
  SymbolFlags flags = SymbolFlags::Public;

  // Test |= operator
  flags |= SymbolFlags::Static;
  ZC_EXPECT(hasFlag(flags, SymbolFlags::Public));
  ZC_EXPECT(hasFlag(flags, SymbolFlags::Static));

  // Test &= operator
  flags &= SymbolFlags::Public;
  ZC_EXPECT(hasFlag(flags, SymbolFlags::Public));
  ZC_EXPECT(!hasFlag(flags, SymbolFlags::Static));
}

ZC_TEST("SymbolFlags_UtilityFunctions") {
  SymbolFlags flags = SymbolFlags::Public | SymbolFlags::Static | SymbolFlags::Function;

  // Test hasFlag
  ZC_EXPECT(hasFlag(flags, SymbolFlags::Public));
  ZC_EXPECT(hasFlag(flags, SymbolFlags::Static));
  ZC_EXPECT(hasFlag(flags, SymbolFlags::Function));
  ZC_EXPECT(!hasFlag(flags, SymbolFlags::Private));

  // Test hasAnyFlag
  ZC_EXPECT(hasAnyFlag(flags, SymbolFlags::Public | SymbolFlags::Private));
  ZC_EXPECT(!hasAnyFlag(flags, SymbolFlags::Private | SymbolFlags::Protected));
}

ZC_TEST("SymbolFlags_UtilityNamespace") {
  using namespace utility;

  SymbolFlags flags = SymbolFlags::Public | SymbolFlags::Static;

  // Test hasAllFlags
  ZC_EXPECT(hasAllFlags(flags, SymbolFlags::Public));
  ZC_EXPECT(hasAllFlags(flags, SymbolFlags::Static));
  ZC_EXPECT(hasAllFlags(flags, SymbolFlags::Public | SymbolFlags::Static));
  ZC_EXPECT(!hasAllFlags(flags, SymbolFlags::Public | SymbolFlags::Private));

  // Test hasAnyFlags
  ZC_EXPECT(hasAnyFlags(flags, SymbolFlags::Public));
  ZC_EXPECT(hasAnyFlags(flags, SymbolFlags::Public | SymbolFlags::Private));
  ZC_EXPECT(!hasAnyFlags(flags, SymbolFlags::Private | SymbolFlags::Protected));

  // Test addFlags
  SymbolFlags newFlags = addFlags(flags, SymbolFlags::Function);
  ZC_EXPECT(hasFlag(newFlags, SymbolFlags::Public));
  ZC_EXPECT(hasFlag(newFlags, SymbolFlags::Static));
  ZC_EXPECT(hasFlag(newFlags, SymbolFlags::Function));

  // Test removeFlags
  SymbolFlags removedFlags = removeFlags(flags, SymbolFlags::Public);
  ZC_EXPECT(!hasFlag(removedFlags, SymbolFlags::Public));
  ZC_EXPECT(hasFlag(removedFlags, SymbolFlags::Static));
}

ZC_TEST("SymbolFlags_ValidationFunctions") {
  using namespace utility;

  // Test hasValidVisibility
  ZC_EXPECT(hasValidVisibility(SymbolFlags::Public));
  ZC_EXPECT(hasValidVisibility(SymbolFlags::Private));
  ZC_EXPECT(hasValidVisibility(SymbolFlags::Protected));
  ZC_EXPECT(hasValidVisibility(SymbolFlags::Internal));
  ZC_EXPECT(hasValidVisibility(SymbolFlags::None));  // No visibility is valid
  ZC_EXPECT(!hasValidVisibility(SymbolFlags::Public |
                                SymbolFlags::Private));  // Multiple visibility flags

  // Test hasValidStorage
  ZC_EXPECT(hasValidStorage(SymbolFlags::Static));
  ZC_EXPECT(hasValidStorage(SymbolFlags::Instance));
  ZC_EXPECT(hasValidStorage(SymbolFlags::Local));
  ZC_EXPECT(hasValidStorage(SymbolFlags::Global));
  ZC_EXPECT(hasValidStorage(SymbolFlags::None));  // No storage is valid
  ZC_EXPECT(
      !hasValidStorage(SymbolFlags::Static | SymbolFlags::Instance));  // Multiple storage flags

  // Test hasValidMutability
  ZC_EXPECT(hasValidMutability(SymbolFlags::Mutable));
  ZC_EXPECT(hasValidMutability(SymbolFlags::Immutable));
  ZC_EXPECT(hasValidMutability(SymbolFlags::None));  // No mutability is valid
  ZC_EXPECT(!hasValidMutability(SymbolFlags::Mutable |
                                SymbolFlags::Immutable));  // Multiple mutability flags

  // Test hasValidVariance
  ZC_EXPECT(hasValidVariance(SymbolFlags::Covariant));
  ZC_EXPECT(hasValidVariance(SymbolFlags::Contravariant));
  ZC_EXPECT(hasValidVariance(SymbolFlags::Invariant));
  ZC_EXPECT(hasValidVariance(SymbolFlags::None));  // No variance is valid
  ZC_EXPECT(!hasValidVariance(SymbolFlags::Covariant |
                              SymbolFlags::Contravariant));  // Multiple variance flags
}

ZC_TEST("SymbolFlags_ComprehensiveValidation") {
  using namespace utility;

  // Test valid flag combinations
  ZC_EXPECT(isValidSymbolFlags(SymbolFlags::Public | SymbolFlags::Static | SymbolFlags::Function |
                               SymbolFlags::TermKind));
  ZC_EXPECT(isValidSymbolFlags(SymbolFlags::Private | SymbolFlags::Instance |
                               SymbolFlags::Variable | SymbolFlags::TermKind));
  ZC_EXPECT(isValidSymbolFlags(SymbolFlags::Protected | SymbolFlags::Virtual | SymbolFlags::Method |
                               SymbolFlags::TermKind));

  // Test invalid flag combinations
  ZC_EXPECT(
      !isValidSymbolFlags(SymbolFlags::Public | SymbolFlags::Private));  // Multiple visibility
  ZC_EXPECT(!isValidSymbolFlags(SymbolFlags::Static | SymbolFlags::Instance));  // Multiple storage
  ZC_EXPECT(
      !isValidSymbolFlags(SymbolFlags::Mutable | SymbolFlags::Immutable));  // Multiple mutability
  ZC_EXPECT(!isValidSymbolFlags(SymbolFlags::Covariant |
                                SymbolFlags::Contravariant));  // Multiple variance
}

ZC_TEST("SymbolFlags_PreDefinedCombinations") {
  // Test predefined flag combinations
  ZC_EXPECT(hasFlag(SymbolFlags::PublicMethod, SymbolFlags::Public));
  ZC_EXPECT(hasFlag(SymbolFlags::PublicMethod, SymbolFlags::Method));
  ZC_EXPECT(hasFlag(SymbolFlags::PublicMethod, SymbolFlags::TermKind));

  ZC_EXPECT(hasFlag(SymbolFlags::PrivateField, SymbolFlags::Private));
  ZC_EXPECT(hasFlag(SymbolFlags::PrivateField, SymbolFlags::Field));
  ZC_EXPECT(hasFlag(SymbolFlags::PrivateField, SymbolFlags::TermKind));

  ZC_EXPECT(hasFlag(SymbolFlags::AbstractClass, SymbolFlags::Abstract));
  ZC_EXPECT(hasFlag(SymbolFlags::AbstractClass, SymbolFlags::Class));
  ZC_EXPECT(hasFlag(SymbolFlags::AbstractClass, SymbolFlags::TypeKind));

  ZC_EXPECT(hasFlag(SymbolFlags::StaticFunction, SymbolFlags::Static));
  ZC_EXPECT(hasFlag(SymbolFlags::StaticFunction, SymbolFlags::Function));
  ZC_EXPECT(hasFlag(SymbolFlags::StaticFunction, SymbolFlags::TermKind));
}

ZC_TEST("SymbolFlags_MaskOperations") {
  SymbolFlags flags =
      SymbolFlags::Public | SymbolFlags::Static | SymbolFlags::Function | SymbolFlags::Async;

  // Test visibility mask
  SymbolFlags visibilityFlags = flags & SymbolFlags::VisibilityMask;
  ZC_EXPECT(visibilityFlags == SymbolFlags::Public);

  // Test storage mask
  SymbolFlags storageFlags = flags & SymbolFlags::StorageMask;
  ZC_EXPECT(storageFlags == SymbolFlags::Static);

  // Test special mask
  SymbolFlags specialFlags = flags & SymbolFlags::SpecialMask;
  ZC_EXPECT(hasFlag(specialFlags, SymbolFlags::Async));

  // Test function flags
  ZC_EXPECT(hasFlag(flags & SymbolFlags::FunctionFlags, SymbolFlags::Function));
  ZC_EXPECT(hasFlag(flags & SymbolFlags::FunctionFlags, SymbolFlags::Async));
}

ZC_TEST("SymbolFlags_TypeClassification") {
  using namespace utility;

  // Test function classification
  ZC_EXPECT(isFunction(SymbolFlags::Function));
  ZC_EXPECT(isFunction(SymbolFlags::Method));
  ZC_EXPECT(isFunction(SymbolFlags::Constructor));
  ZC_EXPECT(!isFunction(SymbolFlags::Variable));
  ZC_EXPECT(!isFunction(SymbolFlags::Class));

  // Test compiler-generated classification
  ZC_EXPECT(isCompilerGenerated(SymbolFlags::Synthetic));
  ZC_EXPECT(isCompilerGenerated(SymbolFlags::Builtin));
  ZC_EXPECT(isCompilerGenerated(SymbolFlags::Template));
  ZC_EXPECT(!isCompilerGenerated(SymbolFlags::Public));
  ZC_EXPECT(!isCompilerGenerated(SymbolFlags::Function));
}

ZC_TEST("SymbolFlags_FlagConversion") {
  SymbolFlags flags =
      SymbolFlags::Public | SymbolFlags::Function | SymbolFlags::Static | SymbolFlags::Class;

  // Test toTermFlags
  SymbolFlags termFlags = toTermFlags(flags);
  ZC_EXPECT(hasFlag(termFlags, SymbolFlags::TermKind));
  ZC_EXPECT(hasFlag(termFlags, SymbolFlags::Public));
  ZC_EXPECT(hasFlag(termFlags, SymbolFlags::Function));
  ZC_EXPECT(hasFlag(termFlags, SymbolFlags::Static));
  ZC_EXPECT(!hasFlag(termFlags, SymbolFlags::Class));  // Class is type-level

  // Test toTypeFlags
  SymbolFlags typeFlags = toTypeFlags(flags);
  ZC_EXPECT(hasFlag(typeFlags, SymbolFlags::TypeKind));
  ZC_EXPECT(hasFlag(typeFlags, SymbolFlags::Public));
  ZC_EXPECT(hasFlag(typeFlags, SymbolFlags::Class));
  ZC_EXPECT(!hasFlag(typeFlags, SymbolFlags::Function));  // Function is term-level

  // Test toCommonFlags
  SymbolFlags commonFlags = toCommonFlags(flags);
  ZC_EXPECT(hasFlag(commonFlags, SymbolFlags::Public));
  ZC_EXPECT(hasFlag(commonFlags, SymbolFlags::Static));
  ZC_EXPECT(!hasFlag(commonFlags, SymbolFlags::Function));  // Function is kind-specific
  ZC_EXPECT(!hasFlag(commonFlags, SymbolFlags::Class));     // Class is kind-specific
}

ZC_TEST("SymbolFlags_EdgeCases") {
  // Test None flag
  ZC_EXPECT(!hasFlag(SymbolFlags::None, SymbolFlags::Public));
  ZC_EXPECT(!hasAnyFlag(SymbolFlags::None, SymbolFlags::Public | SymbolFlags::Private));

  // Test all flags
  SymbolFlags allFlags = ~SymbolFlags::None;
  ZC_EXPECT(hasFlag(allFlags, SymbolFlags::Public));
  ZC_EXPECT(hasFlag(allFlags, SymbolFlags::Private));
  ZC_EXPECT(hasFlag(allFlags, SymbolFlags::Function));
  ZC_EXPECT(hasFlag(allFlags, SymbolFlags::Class));

  // Test reserved flags
  ZC_EXPECT(hasFlag(allFlags, SymbolFlags::Reserved1));
}

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
