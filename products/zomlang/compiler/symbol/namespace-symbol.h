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

/// \brief Represents a namespace symbol in the symbol table
/// \details Namespaces provide hierarchical organization of symbols
///          and help prevent name collisions
class NamespaceSymbol : public Symbol {
public:
  /// \brief Constructs a NamespaceSymbol
  /// \param id Unique symbol identifier
  /// \param name Namespace name
  /// \param flags Symbol properties flags
  /// \param location Source code location where this namespace is defined
  NamespaceSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                  const source::SourceLoc& location) noexcept
      : Symbol(id, name, flags, location) {}

  /// \brief Gets the symbol kind classification for namespaces
  static SymbolKind getKind() { return SymbolKind::Module; }
  /// \brief Gets the symbol kind for this specific instance
  SymbolKind getSymbolKind() const override { return getKind(); }

  /// \brief Accepts a symbol visitor
  /// \param visitor Visitor instance to process this symbol
  void accept(SymbolVisitor& visitor) override;
};

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang