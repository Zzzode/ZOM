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

#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/checker/symbol-table.h"

namespace zomlang {
namespace checker {

// class TypeChecker : public CompilerStage<zc::Own<ast::AST>,
// zc::String> {
//  protected:
//   void process(const zc::Own<ast::AST>& input,
//                zc::Vector<zc::String>& outputs) override;
//
//  private:
//   SymbolTable symbol_table_;
//
//  public:
//   TypeChecker() = default;
//   ~TypeChecker() noexcept override = default;
// };

}  // namespace checker
}  // namespace zomlang
