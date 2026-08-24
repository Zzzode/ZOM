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
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zomlang/compiler/ir/error-union-layout-revision.h"
#include "zomlang/compiler/ir/error-union-layout.h"

namespace zomlang::compiler::ir {

/// \brief Canonical codec for RFC 0006 error-union layout descriptors and manifests.
///
/// `encode` produces the exact `zom.error-union-layout` framed byte stream for one
/// descriptor; `compute` returns its SHA-256 revision. `encodeManifest` produces
/// the `zom.target-artifact-abi` framed stream; `computeManifestRevision` returns
/// its SHA-256 revision. Both mirror the OwnershipFactsCodec encoder discipline:
/// domain characters as individual bytes followed by a NUL, 8-byte big-endian
/// integers, 8-byte big-endian length-prefixed byte strings, and 32 raw digest
/// bytes. `SemanticTypeKey` values are opaque byte strings; the codec never
/// interprets their interior.
class ErrorUnionLayoutCodec final {
public:
  /// \brief Encodes one descriptor to its canonical `zom.error-union-layout` stream.
  ZC_NODISCARD static zc::Array<uint8_t> encode(const ErrorUnionLayoutDescriptor& descriptor);

  /// \brief Computes the descriptor revision (SHA-256 over `encode`).
  ZC_NODISCARD static ErrorUnionLayoutRevision compute(
      const ErrorUnionLayoutDescriptor& descriptor);

  /// \brief Encodes one target-artifact manifest to its canonical stream.
  ///
  /// Layouts are emitted in the order supplied; the caller is responsible for the
  /// canonical role-key ordering the RFC requires.
  ZC_NODISCARD static zc::Array<uint8_t> encodeManifest(const TargetArtifactAbiManifest& manifest);

  /// \brief Computes the manifest revision (SHA-256 over `encodeManifest`).
  ZC_NODISCARD static TargetArtifactAbiRevision computeManifestRevision(
      const TargetArtifactAbiManifest& manifest);
};

}  // namespace zomlang::compiler::ir
