// Copyright (c) 2024-2025 Zode.Z. All rights reserved
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

#include "compiler/ast/schema-verifier.h"

#include <cstdint>

#include "compiler/ast/generated/node-accessors.h"
#include "compiler/ast/generated/node-schema.h"
#include "zc/core/string.h"

namespace zomlang {
namespace compiler {
namespace ast {

namespace {

bool equals(const char* lhs, const char* rhs) {
  if (lhs == nullptr || rhs == nullptr) { return lhs == rhs; }
  while (*lhs != '\0' && *rhs != '\0') {
    if (*lhs != *rhs) { return false; }
    ++lhs;
    ++rhs;
  }
  return *lhs == *rhs;
}

bool isExpressionSchemaKind(SyntaxKind kind) {
  return isLiteralExprKind(kind) || isExprKind(kind) || kind == SyntaxKind::UnsafeBlockExpr;
}

bool isStatementSchemaKind(SyntaxKind kind) {
  return isStatementKind(kind) || kind == SyntaxKind::ExternBlock;
}

bool isIdentifierSchemaKind(SyntaxKind kind) {
  return kind == SyntaxKind::IdentExpr || kind == SyntaxKind::ModulePath ||
         kind == SyntaxKind::AttributePath || kind == SyntaxKind::Identifier;
}

bool isParameterDeclKind(SyntaxKind kind) { return kind == SyntaxKind::FunctionParameterDecl; }

bool isObjectLiteralElementKind(SyntaxKind kind) {
  return kind == SyntaxKind::ObjectProperty || kind == SyntaxKind::ObjectSpread;
}

bool isClassElementKind(SyntaxKind kind) {
  return kind == SyntaxKind::MethodDecl || kind == SyntaxKind::FieldDecl ||
         kind == SyntaxKind::AssociatedTypeDecl || kind == SyntaxKind::ConstructorDecl ||
         kind == SyntaxKind::DestructorDecl || kind == SyntaxKind::ClassConstDecl;
}

bool isTypeParamDeclKind(SyntaxKind kind) { return kind == SyntaxKind::GenericTypeParam; }

bool matchesCastTarget(SyntaxKind kind, const char* target) {
  if (target == nullptr) { return true; }
  if (equals(target, "Expression")) { return isExpressionSchemaKind(kind); }
  if (equals(target, "TypeExpr")) { return isTypeKind(kind); }
  if (equals(target, "Statement")) { return isStatementSchemaKind(kind); }
  if (equals(target, "Declaration")) { return isDeclarationKind(kind); }
  if (equals(target, "Pattern")) { return isPatternKind(kind); }
  if (equals(target, "Identifier")) { return isIdentifierSchemaKind(kind); }
  if (equals(target, "BlockStmt")) { return kind == SyntaxKind::BlockStmt; }
  if (equals(target, "ParameterDecl")) { return isParameterDeclKind(kind); }
  if (equals(target, "ObjectLiteralElement")) { return isObjectLiteralElementKind(kind); }
  if (equals(target, "ClassElement")) { return isClassElementKind(kind); }
  if (equals(target, "TypeParamDecl")) { return isTypeParamDeclKind(kind); }
  return equals(nodeKindName(kind), target);
}

bool enumContains(const NodeSchemaFieldEntry& field, uint32_t value) {
  if (field.enumValueCount == 0) { return true; }
  for (uint32_t index = 0; index < field.enumValueCount; ++index) {
    if (field.enumValues[index].value == value) { return true; }
  }
  return false;
}

zc::Maybe<zc::String> validateNodeIdField(const Tree& tree, const Node& node,
                                          const NodeSchemaFieldEntry& field) {
  const NodeId id(node.payload.words[field.firstWord]);
  if (!id) {
    if (field.optional) { return zc::none; }
    return zc::str("Required NodeId field ", field.name, " is empty on ", nodeKindName(node.kind));
  }
  if (!tree.contains(id)) {
    return zc::str("NodeId field ", field.name, " points outside the AST on ",
                   nodeKindName(node.kind));
  }
  if (!matchesCastTarget(tree.node(id).kind, field.castTarget)) {
    return zc::str("NodeId field ", field.name, " has the wrong child kind on ",
                   nodeKindName(node.kind));
  }
  return zc::none;
}

zc::Maybe<zc::String> validateNodeListField(const Tree& tree, const Node& node,
                                            const NodeSchemaFieldEntry& field) {
  NodeList list;
  list.first = node.payload.words[field.firstWord];
  list.size = node.payload.words[field.secondWord];
  if (!tree.contains(list)) {
    return zc::str("NodeList field ", field.name, " points outside the AST on ",
                   nodeKindName(node.kind));
  }

  for (NodeId id : tree.list(list)) {
    if (!tree.contains(id)) {
      return zc::str("NodeList field ", field.name, " contains an invalid child on ",
                     nodeKindName(node.kind));
    }
    if (!matchesCastTarget(tree.node(id).kind, field.castTarget)) {
      return zc::str("NodeList field ", field.name, " has a child with the wrong kind on ",
                     nodeKindName(node.kind));
    }
  }
  return zc::none;
}

zc::Maybe<zc::String> validateIdentListField(const Tree& tree, const Node& node,
                                             const NodeSchemaFieldEntry& field) {
  IdentList list;
  list.first = node.payload.words[field.firstWord];
  list.size = node.payload.words[field.secondWord];
  if (!tree.contains(list)) {
    return zc::str("IdentList field ", field.name, " points outside the AST on ",
                   nodeKindName(node.kind));
  }
  return zc::none;
}

zc::Maybe<zc::String> validateScalarField(const Node& node, const NodeSchemaFieldEntry& field) {
  const uint32_t value = node.payload.words[field.firstWord];
  switch (field.storage) {
    case NodeSchemaFieldStorage::Bool:
      if (value != 0 && value != 1) {
        return zc::str("Bool field ", field.name, " is outside its domain on ",
                       nodeKindName(node.kind));
      }
      return zc::none;
    case NodeSchemaFieldStorage::UInt8:
      if (value > 0xff) {
        return zc::str("UInt8 field ", field.name, " is outside its domain on ",
                       nodeKindName(node.kind));
      }
      return zc::none;
    case NodeSchemaFieldStorage::UInt16:
      if (value > 0xffff) {
        return zc::str("UInt16 field ", field.name, " is outside its domain on ",
                       nodeKindName(node.kind));
      }
      return zc::none;
    case NodeSchemaFieldStorage::Enum:
      if (!enumContains(field, value)) {
        return zc::str("Enum field ", field.name, " is outside its domain on ",
                       nodeKindName(node.kind));
      }
      return zc::none;
    default:
      return zc::none;
  }
}

zc::Maybe<zc::String> validateInternField(const Tree& tree, const Node& node,
                                          const NodeSchemaFieldEntry& field) {
  const uint32_t value = node.payload.words[field.firstWord];
  // An optional interned handle may be empty (0); a required one may not. A
  // non-empty handle must resolve to a live entry in this tree's own intern
  // table, so a forged out-of-range id is rejected here rather than crashing on
  // later access. This closes the sole AST verifier over interned identities.
  if (value == 0) {
    if (!field.optional) {
      return zc::str("Required scalar field ", field.name, " is empty on ",
                     nodeKindName(node.kind));
    }
    return zc::none;
  }
  bool member = false;
  switch (field.storage) {
    case NodeSchemaFieldStorage::StringId:
      member = tree.contains(StringId(value));
      break;
    case NodeSchemaFieldStorage::IdentId:
      member = tree.contains(IdentId(value));
      break;
    case NodeSchemaFieldStorage::BigIntId:
      member = tree.contains(BigIntId(value));
      break;
    case NodeSchemaFieldStorage::FloatId:
      member = tree.contains(FloatId(value));
      break;
    default:
      return zc::str("Non-interned storage routed through intern validation on ",
                     nodeKindName(node.kind));
  }
  if (!member) {
    return zc::str("Interned field ", field.name, " references an id outside this tree on ",
                   nodeKindName(node.kind));
  }
  return zc::none;
}

zc::Maybe<zc::String> validateField(const Tree& tree, const Node& node,
                                    const NodeSchemaFieldEntry& field) {
  switch (field.storage) {
    case NodeSchemaFieldStorage::NodeId:
      return validateNodeIdField(tree, node, field);
    case NodeSchemaFieldStorage::NodeList:
      return validateNodeListField(tree, node, field);
    case NodeSchemaFieldStorage::IdentList:
      return validateIdentListField(tree, node, field);
    case NodeSchemaFieldStorage::StringId:
    case NodeSchemaFieldStorage::IdentId:
    case NodeSchemaFieldStorage::BigIntId:
    case NodeSchemaFieldStorage::FloatId:
      return validateInternField(tree, node, field);
    case NodeSchemaFieldStorage::Bool:
    case NodeSchemaFieldStorage::UInt8:
    case NodeSchemaFieldStorage::UInt16:
    case NodeSchemaFieldStorage::UInt32:
    case NodeSchemaFieldStorage::UInt64:
    case NodeSchemaFieldStorage::Enum:
      return validateScalarField(node, field);
  }
  return zc::str("Unknown schema field storage on ", nodeKindName(node.kind));
}

}  // namespace

zc::Maybe<zc::String> verifySchemaFailure(const Tree& tree) {
  if (!tree.contains(tree.root())) { return zc::str("AST root is missing"); }
  if (tree.node(tree.root()).kind != SyntaxKind::SourceFile) {
    return zc::str("AST root is not SourceFile");
  }

  for (const Node& node : tree.nodes()) {
    if (!isKnownAstKind(node.kind)) { return zc::str("Unknown AST node kind"); }

    const NodeSchemaEntry* schema = lookupNodeSchema(node.kind);
    if (schema == nullptr) { return zc::str("Missing AST node schema entry"); }

    for (uint32_t fieldIndex = 0; fieldIndex < schema->fieldCount; ++fieldIndex) {
      ZC_IF_SOME(failure, validateField(tree, node, schema->fields[fieldIndex])) {
        return zc::mv(failure);
      }
    }
  }

  return zc::none;
}

bool verifySchema(const Tree& tree) { return verifySchemaFailure(tree) == zc::none; }

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
