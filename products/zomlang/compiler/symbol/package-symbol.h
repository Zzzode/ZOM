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

#include "zomlang/compiler/symbol/symbol.h"

namespace zomlang {
namespace compiler {
namespace symbol {

class SymbolVisitor;

/// \brief Represents a package symbol in the symbol table
/// \details Packages are used for organizing related symbols
///          in larger codebases
class PackageSymbol : public Symbol {
public:
  /// \brief Constructs a PackageSymbol
  /// \param id Unique symbol identifier
  /// \param name Package name
  /// \param flags Symbol properties flags
  /// \param location Source code location where this package is defined
  PackageSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                const source::SourceLoc& location) noexcept
      : Symbol(id, name, flags, location) {}

  /// \brief Gets the symbol kind classification for packages
  static SymbolKind getStaticKind() { return SymbolKind::Module; }
  /// \brief Gets the symbol kind for this specific instance
  SymbolKind getKind() const override { return SymbolKind::Package; }

  /// \brief Accepts a symbol visitor
  /// \param visitor Visitor instance to process this symbol
  void accept(SymbolVisitor& visitor);
};

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
