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
#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {
namespace type {

/// \brief ErrorType - Placeholder type used for error recovery.
///
/// When the type checker encounters a type error (e.g., undefined variable,
/// type mismatch), it produces an ErrorType to allow compilation to continue
/// and report additional errors rather than aborting on the first error.
///
/// ErrorType has special behavior:
/// - It is compatible with all types (both as subtype and supertype)
/// - Operations on ErrorType produce ErrorType
/// - It should never appear in successfully type-checked code
class ErrorType final : public Type {
public:
  /// \brief Construct an error type with an optional diagnostic message.
  ErrorType();
  explicit ErrorType(zc::StringPtr message);

  ~ErrorType() noexcept(false);

  ZC_DISALLOW_COPY(ErrorType);

  // Move semantics
  ErrorType(ErrorType&& other) noexcept;
  ErrorType& operator=(ErrorType&& other) noexcept;

  /// \brief Get the diagnostic message associated with this error.
  zc::StringPtr getMessage() const;

  // Type overrides
  TypeKind getKind() const override { return TypeKind::Error; }
  zc::String toString() const override;
  bool equals(const Type& other) const override;
  bool isSubtypeOf(const Type& other) const override;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
