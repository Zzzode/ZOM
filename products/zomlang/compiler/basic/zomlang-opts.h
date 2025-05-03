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

namespace zomlang {
namespace compiler {
namespace basic {

struct LangOptions {
  bool useUnicode;
  bool allowDollarIdentifiers;
  bool supportRegexLiterals;
  // more...

  LangOptions() : useUnicode(true), allowDollarIdentifiers(false), supportRegexLiterals(true) {}
};

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang
