// Copyright (c) 2026 Zode.Z. All rights reserved.
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

#include "compiler/type/type-substitution.h"

#include "compiler/type/semantic-type-data.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::type::semantic {
namespace {

/// \brief Stack-local recursive substitution walker for one type tree.
class SubstitutionWalker final {
public:
  SubstitutionWalker(SemanticTypeStore& store,
                     zc::ArrayPtr<const TypeSubstitution> substitutions)
      : storeValue(store), substitutionsValue(substitutions) {}

  SubstituteTypeResult walk(identity::SemanticTypeId id) {
    auto lookup = storeValue.get(id);
    if (lookup.is<identity::IdentityInvariant>()) {
      return zc::mv(lookup.get<identity::IdentityInvariant>());
    }
    const auto& data = lookup.get<SemanticTypeLookup>().data();
    switch (data.tag()) {
      case TypeDataTag::Primitive:
      case TypeDataTag::InterfaceSelf:
        return id;
      case TypeDataTag::TypeParameter: {
        const auto& parameter = data.get<TypeParameterTypeData>().parameter;
        ZC_IF_SOME(argument, mapped(parameter, substitutionsValue)) {
          // The argument may itself mention mapped parameters, so recurse.
          return walk(argument);
        }
        return id;
      }
      case TypeDataTag::Tuple: {
        zc::Vector<identity::SemanticTypeId> elements;
        ZC_IF_SOME(failure, walkAll(data.get<TupleTypeData>().elements, elements)) {
          return zc::mv(failure);
        }
        return rebuild(TypeData(TupleTypeData{zc::mv(elements)}));
      }
      case TypeDataTag::Object: {
        zc::Vector<ObjectFieldData> fields;
        for (const auto& field : data.get<ObjectTypeData>().fields) {
          auto walked = walk(field.type);
          if (walked.is<identity::IdentityInvariant>()) {
            return zc::mv(walked.get<identity::IdentityInvariant>());
          }
          fields.add(ObjectFieldData{field.name.clone(),
                                     walked.get<identity::SemanticTypeId>(), field.mutability,
                                     field.presence});
        }
        return rebuild(TypeData(ObjectTypeData{zc::mv(fields)}));
      }
      case TypeDataTag::DynamicArray: {
        auto element = walkOne(data.get<DynamicArrayTypeData>().element);
        if (element.is<identity::IdentityInvariant>()) {
          return zc::mv(element.get<identity::IdentityInvariant>());
        }
        return rebuild(TypeData(DynamicArrayTypeData{element.get<identity::SemanticTypeId>()}));
      }
      case TypeDataTag::Slice: {
        auto element = walkOne(data.get<SliceTypeData>().element);
        if (element.is<identity::IdentityInvariant>()) {
          return zc::mv(element.get<identity::IdentityInvariant>());
        }
        return rebuild(TypeData(SliceTypeData{element.get<identity::SemanticTypeId>()}));
      }
      case TypeDataTag::FixedArray: {
        const auto& fixed = data.get<FixedArrayTypeData>();
        auto element = walkOne(fixed.element);
        if (element.is<identity::IdentityInvariant>()) {
          return zc::mv(element.get<identity::IdentityInvariant>());
        }
        return rebuild(TypeData(FixedArrayTypeData{element.get<identity::SemanticTypeId>(),
                                                   fixed.length}));
      }
      case TypeDataTag::Function: {
        const auto& function = data.get<FunctionTypeData>();
        zc::Vector<identity::SemanticTypeId> parameters;
        ZC_IF_SOME(failure, walkAll(function.parameters, parameters)) {
          return zc::mv(failure);
        }
        auto success = walkOne(function.success);
        if (success.is<identity::IdentityInvariant>()) {
          return zc::mv(success.get<identity::IdentityInvariant>());
        }
        zc::Maybe<identity::SemanticTypeId> raises;
        ZC_IF_SOME(raisesType, function.raises) {
          auto walked = walkOne(raisesType);
          if (walked.is<identity::IdentityInvariant>()) {
            return zc::mv(walked.get<identity::IdentityInvariant>());
          }
          raises = walked.get<identity::SemanticTypeId>();
        }
        return rebuild(TypeData(FunctionTypeData{zc::mv(parameters),
                                                success.get<identity::SemanticTypeId>(),
                                                zc::mv(raises)}));
      }
      case TypeDataTag::Nominal: {
        const auto& nominal = data.get<NominalTypeData>();
        zc::Vector<identity::SemanticTypeId> arguments;
        ZC_IF_SOME(failure, walkAll(nominal.arguments, arguments)) {
          return zc::mv(failure);
        }
        return rebuild(
            TypeData(NominalTypeData{nominal.definition, zc::mv(arguments)}));
      }
      case TypeDataTag::Union: {
        zc::Vector<identity::SemanticTypeId> alternatives;
        ZC_IF_SOME(failure, walkAll(data.get<UnionTypeData>().alternatives, alternatives)) {
          return zc::mv(failure);
        }
        return rebuild(TypeData(UnionTypeData{zc::mv(alternatives)}));
      }
      case TypeDataTag::Intersection: {
        zc::Vector<identity::SemanticTypeId> conjuncts;
        ZC_IF_SOME(failure, walkAll(data.get<IntersectionTypeData>().conjuncts, conjuncts)) {
          return zc::mv(failure);
        }
        return rebuild(TypeData(IntersectionTypeData{zc::mv(conjuncts)}));
      }
      case TypeDataTag::Reference: {
        const auto& reference = data.get<ReferenceTypeData>();
        auto referent = walkOne(reference.referent);
        if (referent.is<identity::IdentityInvariant>()) {
          return zc::mv(referent.get<identity::IdentityInvariant>());
        }
        return rebuild(TypeData(ReferenceTypeData{
            reference.mutability, referent.get<identity::SemanticTypeId>()}));
      }
      case TypeDataTag::RawPointer: {
        const auto& pointer = data.get<RawPointerTypeData>();
        auto pointee = walkOne(pointer.pointee);
        if (pointee.is<identity::IdentityInvariant>()) {
          return zc::mv(pointee.get<identity::IdentityInvariant>());
        }
        return rebuild(TypeData(RawPointerTypeData{
            pointer.mutability, pointee.get<identity::SemanticTypeId>()}));
      }
      case TypeDataTag::Existential: {
        const auto& existential = data.get<ExistentialTypeData>();
        zc::Vector<identity::SemanticTypeId> principalArguments;
        ZC_IF_SOME(failure, walkAll(existential.principal.arguments, principalArguments)) {
          return zc::mv(failure);
        }
        ExistentialInterfaceData principal{existential.principal.definition,
                                          zc::mv(principalArguments)};
        zc::Vector<ExistentialInterfaceData> additional;
        for (const auto& interface : existential.additionalInterfaces) {
          zc::Vector<identity::SemanticTypeId> arguments;
          ZC_IF_SOME(failure, walkAll(interface.arguments, arguments)) {
            return zc::mv(failure);
          }
          additional.add(ExistentialInterfaceData{interface.definition, zc::mv(arguments)});
        }
        zc::Vector<AssociatedTypeBindingData> bindings;
        for (const auto& binding : existential.associatedBindings) {
          auto walked = walkOne(binding.type);
          if (walked.is<identity::IdentityInvariant>()) {
            return zc::mv(walked.get<identity::IdentityInvariant>());
          }
          bindings.add(AssociatedTypeBindingData{binding.associated,
                                                 walked.get<identity::SemanticTypeId>()});
        }
        zc::Vector<identity::DefId> markers;
        for (const auto marker : existential.markers) { markers.add(marker); }
        return rebuild(TypeData(ExistentialTypeData{zc::mv(principal), zc::mv(additional),
                                                    zc::mv(markers), zc::mv(bindings)}));
      }
      case TypeDataTag::InterfaceBound: {
        const auto& bound = data.get<InterfaceBoundTypeData>().interface;
        zc::Vector<identity::SemanticTypeId> arguments;
        ZC_IF_SOME(failure, walkAll(bound.arguments, arguments)) {
          return zc::mv(failure);
        }
        return rebuild(TypeData(InterfaceBoundTypeData{
            InterfaceInstantiation{bound.interface, zc::mv(arguments)}}));
      }
    }
    ZC_UNREACHABLE
  }

private:
  static zc::Maybe<identity::SemanticTypeId> mapped(
      const identity::GenericParameterKey& parameter,
      zc::ArrayPtr<const TypeSubstitution> substitutions) {
    for (const auto& candidate : substitutions) {
      if (candidate.parameter == parameter) { return candidate.argument; }
    }
    return zc::none;
  }

  SubstituteTypeResult walkOne(identity::SemanticTypeId id) { return walk(id); }

  zc::Maybe<identity::IdentityInvariant> walkAll(
      zc::ArrayPtr<const identity::SemanticTypeId> inputs,
      zc::Vector<identity::SemanticTypeId>& outputs) {
    for (const auto input : inputs) {
      auto walked = walk(input);
      if (walked.is<identity::IdentityInvariant>()) {
        return zc::mv(walked.get<identity::IdentityInvariant>());
      }
      outputs.add(walked.get<identity::SemanticTypeId>());
    }
    return zc::none;
  }

  SubstituteTypeResult rebuild(TypeData&& data) {
    auto canonical = storeValue.canonicalizeClosed(zc::mv(data));
    if (canonical.is<identity::IdentityInvariant>()) {
      return zc::mv(canonical.get<identity::IdentityInvariant>());
    }
    auto interned = storeValue.intern(zc::mv(canonical.get<CanonicalTypeData>()));
    if (interned.is<identity::IdentityInvariant>()) {
      return zc::mv(interned.get<identity::IdentityInvariant>());
    }
    return interned.get<SemanticTypeInterned>().id;
  }

  SemanticTypeStore& storeValue;
  zc::ArrayPtr<const TypeSubstitution> substitutionsValue;
};

}  // namespace

SubstituteTypeResult substituteType(SemanticTypeStore& store,
                                    identity::SemanticTypeId type,
                                    zc::ArrayPtr<const TypeSubstitution> substitutions) {
  if (substitutions.size() == 0) { return type; }
  SubstitutionWalker walker(store, substitutions);
  return walker.walk(type);
}

}  // namespace zomlang::compiler::type::semantic
