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

#include <cstdint>

namespace zomlang {
namespace compiler {
namespace irgen {

struct ScalarLayout final {
  uint64_t size = 0;
  uint64_t align = 1;
};

enum class IntegerScalarWidth : uint8_t {
  W8,
  W16,
  W32,
  W64,
};

enum class FloatScalarWidth : uint8_t {
  W32,
  W64,
};

/// \brief ZOM ABI properties required while lowering target-dependent values.
///
/// Fixed-width scalar values use their natural byte alignment in the current
/// ZOM ABI profile. Pointer size and alignment are explicit because those
/// properties vary between supported target profiles.
class TargetDataLayout final {
public:
  /// \brief Construct the ZOM ILP32 data-layout profile.
  /// \return A data layout with four-byte pointers aligned to four bytes.
  static TargetDataLayout ilp32();

  /// \brief Construct the ZOM LP64 data-layout profile.
  /// \return A data layout with eight-byte pointers aligned to eight bytes.
  static TargetDataLayout lp64();

  /// \brief Return the target pointer storage size in bytes.
  /// \return The pointer storage size.
  uint64_t getPointerSize() const;

  /// \brief Return the target pointer ABI alignment in bytes.
  /// \return The pointer ABI alignment.
  uint64_t getPointerAlignment() const;

  /// \brief Return the selected profile's layout for an integer scalar.
  /// \param width Integer storage width.
  /// \return The scalar storage size and ABI alignment.
  ScalarLayout getIntegerLayout(IntegerScalarWidth width) const;

  /// \brief Return the selected profile's layout for a floating-point scalar.
  /// \param width Floating-point storage width.
  /// \return The scalar storage size and ABI alignment.
  ScalarLayout getFloatLayout(FloatScalarWidth width) const;

  /// \brief Return the selected profile's layout for bool.
  /// \return The bool storage size and ABI alignment.
  ScalarLayout getBoolLayout() const;

  /// \brief Return the selected profile's layout for a Unicode scalar.
  /// \return The char storage size and ABI alignment.
  ScalarLayout getCharLayout() const;

private:
  TargetDataLayout(uint64_t pointerSize, uint64_t pointerAlignment);

  uint64_t pointerSize;
  uint64_t pointerAlignment;
};

}  // namespace irgen
}  // namespace compiler
}  // namespace zomlang
