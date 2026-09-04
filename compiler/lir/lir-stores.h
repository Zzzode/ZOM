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

#include "compiler/lir/lir-store.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::lir {

// Closed interning stores for the RFC 0021 LIR module. Each store owns a dense,
// one-based, deterministically ordered sequence of immutable records and issues
// the matching store-local branded handle. Distinct handle types keep the
// stores non-interchangeable at compile time. These are pure data structures;
// no live MIR consumer or lowering path constructs them in this slice.

/// \brief Closed store of SSA carrier type records, keyed by `ValueTypeId`.
class ValueTypeStore final {
public:
  ValueTypeStore() = default;
  ZC_DISALLOW_COPY(ValueTypeStore);
  ValueTypeStore(ValueTypeStore&&) = default;
  ValueTypeStore& operator=(ValueTypeStore&&) = default;

  /// \brief Interns a carrier type, returning the existing handle when equal.
  ZC_NODISCARD ValueTypeId intern(const ValueType& type);

  ZC_NODISCARD size_t size() const noexcept { return records.size(); }
  ZC_NODISCARD zc::Maybe<const ValueType&> lookup(ValueTypeId id) const noexcept;

private:
  zc::Vector<ValueType> records;
};

/// \brief Closed store of storage-layout records, keyed by `LayoutId`.
class LayoutStore final {
public:
  LayoutStore() = default;
  ZC_DISALLOW_COPY(LayoutStore);
  LayoutStore(LayoutStore&&) = default;
  LayoutStore& operator=(LayoutStore&&) = default;

  /// \brief Interns a layout, returning the existing handle when equal.
  ZC_NODISCARD LayoutId intern(const StorageLayout& layout);

  ZC_NODISCARD size_t size() const noexcept { return records.size(); }
  ZC_NODISCARD zc::Maybe<const StorageLayout&> lookup(LayoutId id) const noexcept;

private:
  zc::Vector<StorageLayout> records;
};

/// \brief Closed store of function-ABI records, keyed by `FnAbiId`.
class FnAbiStore final {
public:
  FnAbiStore() = default;
  ZC_DISALLOW_COPY(FnAbiStore);
  FnAbiStore(FnAbiStore&&) = default;
  FnAbiStore& operator=(FnAbiStore&&) = default;

  /// \brief Interns a function ABI, returning the existing handle when equal.
  ZC_NODISCARD FnAbiId intern(const FnAbi& abi);

  ZC_NODISCARD size_t size() const noexcept { return records.size(); }
  ZC_NODISCARD zc::Maybe<const FnAbi&> lookup(FnAbiId id) const noexcept;

private:
  zc::Vector<FnAbi> records;
};

/// \brief Closed store of imported runtime-symbol records, keyed by `RuntimeSymbolId`.
class RuntimeSymbolStore final {
public:
  RuntimeSymbolStore() = default;
  ZC_DISALLOW_COPY(RuntimeSymbolStore);
  RuntimeSymbolStore(RuntimeSymbolStore&&) = default;
  RuntimeSymbolStore& operator=(RuntimeSymbolStore&&) = default;

  /// \brief Interns a runtime symbol, returning the existing handle when equal.
  ZC_NODISCARD RuntimeSymbolId intern(const RuntimeSymbol& symbol);

  ZC_NODISCARD size_t size() const noexcept { return records.size(); }
  ZC_NODISCARD zc::Maybe<const RuntimeSymbol&> lookup(RuntimeSymbolId id) const noexcept;

private:
  zc::Vector<RuntimeSymbol> records;
};

/// \brief Closed store of source-location records, keyed by `SourceLocationId`.
class SourceLocationStore final {
public:
  SourceLocationStore() = default;
  ZC_DISALLOW_COPY(SourceLocationStore);
  SourceLocationStore(SourceLocationStore&&) = default;
  SourceLocationStore& operator=(SourceLocationStore&&) = default;

  /// \brief Interns a source location, returning the existing handle when equal.
  ZC_NODISCARD SourceLocationId intern(const SourceLocation& location);

  ZC_NODISCARD size_t size() const noexcept { return records.size(); }
  ZC_NODISCARD zc::Maybe<const SourceLocation&> lookup(SourceLocationId id) const noexcept;

private:
  zc::Vector<SourceLocation> records;
};

}  // namespace zomlang::compiler::lir
