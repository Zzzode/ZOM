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

#include "zc/core/common.h"
#include "zc/core/string.h"

namespace zomlang::compiler::identity {

/// \brief Normalizes valid UTF-8 text to Unicode NFC using the pinned UCD release.
/// \return NFC text, or none when the input is not valid Unicode UTF-8.
ZC_NODISCARD zc::Maybe<zc::String> normalizeNfc(zc::StringPtr input);

/// \brief Checks whether valid UTF-8 text is already in Unicode NFC.
/// \return The normalization state, or none when the input is not valid Unicode UTF-8.
ZC_NODISCARD zc::Maybe<bool> isNfc(zc::StringPtr input);

/// \brief Applies default Unicode full case folding using the pinned UCD release.
/// \return Folded UTF-8 text, or none when the input is not valid Unicode UTF-8.
ZC_NODISCARD zc::Maybe<zc::String> fullCaseFold(zc::StringPtr input);

}  // namespace zomlang::compiler::identity
