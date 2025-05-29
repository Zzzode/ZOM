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

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zomlang/compiler/ast/ast.h"  // For Node

namespace zomlang {
namespace compiler {
namespace ast {

class SourceFile : public Node {
public:
  // TODO: Define constructor and methods for SourceFile
  SourceFile() noexcept;
  ~SourceFile() noexcept(false) override;

  ZC_DISALLOW_COPY_AND_MOVE(SourceFile);

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ImplementationModule : public Node {
public:
  // TODO: Define constructor and methods for ImplementationModule
  ImplementationModule() noexcept;
  ~ImplementationModule() noexcept(false) override;

  ZC_DISALLOW_COPY_AND_MOVE(ImplementationModule);

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ImplementationModuleElement : public Node {
public:
  ImplementationModuleElement() noexcept;
  // TODO: Define constructor and methods for ImplementationModuleElement
  ~ImplementationModuleElement() noexcept(false) override;

  ZC_DISALLOW_COPY_AND_MOVE(ImplementationModuleElement);
};

class ImportDeclaration : public ImplementationModuleElement {
public:
  ImportDeclaration() noexcept;
  // TODO: Define constructor and methods for ImportDeclaration
  ~ImportDeclaration() noexcept(false) override;

  ZC_DISALLOW_COPY_AND_MOVE(ImportDeclaration);
};

class ExportDeclaration : public ImplementationModuleElement {
public:
  ExportDeclaration() noexcept;
  // TODO: Define constructor and methods for ExportDeclaration
  ~ExportDeclaration() noexcept(false) override;

  ZC_DISALLOW_COPY_AND_MOVE(ExportDeclaration);
};

class ModulePath
    : public Node {  // Or potentially not an Node node itself, but part of Import/Export
public:
  // TODO: Define constructor and methods for ModulePath
  ModulePath() noexcept;
  ~ModulePath() noexcept(false) override;

  ZC_DISALLOW_COPY_AND_MOVE(ModulePath);
};

class ImplementationElement : public ImplementationModuleElement {
public:
  // TODO: Define constructor and methods for ImplementationElement
  ImplementationElement() noexcept;
  ~ImplementationElement() noexcept(false) override;

  ZC_DISALLOW_COPY_AND_MOVE(ImplementationElement);
};

// TODO: Add ExportImplementationElement if it's a distinct Node node
// class ExportImplementationElement : public ImplementationModuleElement {
// public:
//   // TODO: Define constructor and methods for ExportImplementationElement
//   ~ExportImplementationElement() noexcept(false) ;
// };

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang