# Chapter 23 - Visibility Syntax and Retained Facts

## 23.1 Scope

This chapter defines the current source syntax and compiler facts for
module-level exports and type-member visibility. The compiler retains these
facts and enforces module export visibility during verified Binder projection.
It does not yet enforce typed member-access permissions.

## 23.2 Module-Level Export Syntax

A module item is published syntactically with `export`:

```ebnf
ModuleExport ::= 'export' Declaration
               | 'export' ExportClause ';'
```

Examples:

```zom
export fun run() -> i32 { 0 }
export { run as execute };
```

The parser represents declaration-site export and export-clause forms
explicitly. Verified binding metadata retains local and re-export identities,
while verified export surfaces retain their visibility envelopes and complete
provenance.

`public`, `private`, and `protected` are not module-level declaration
modifiers. Their use before a module-level or local declaration emits
`ZOM2088 VisibilityModifierRequiresMemberContext`.

`CompilerSession` builds a verified module graph and projects imports and
re-exports only from dependency `VerifiedExportSurface` values. Private module
bindings cannot be selected as external imports. After signature verification,
the session publishes one `VerifiedModuleInterface` per module and one
`ImportedSignatureView` per consumer module. Checked-fact verification is bound
to those immutable interface revisions. Typed member-access authorization is
the remaining visibility boundary that is not yet enforced.

## 23.3 Member Visibility Syntax

Callable members, fields, and class constants may carry one member-visibility
modifier:

```ebnf
MemberVisibility ::= 'public' | 'private' | 'protected'
```

```zom
class Session {
    private let token: str;
    public fun user_id(this) -> i32 { 0 }
    protected fun refresh_hook(this) -> unit {}
}
```

The AST stores the spelling as `Public`, `Private`, `Protected`, or `Default`.
Verified `DefinitionFact` records retain the closed semantic value `Public`,
`Private`, or `Protected`. Unqualified members in an interface receive a public
fact; unqualified members in other supported type bodies receive a private
fact.

These are retained semantic facts only. Member lookup currently does not
reject a reference because it crosses a private or protected boundary, and the
compiler does not compute subclass access contexts. No source program may rely
on those access restrictions being enforced.

## 23.4 Type Extensibility

There is no source-level type-extensibility modifier. `open`, `sealed`, and
`final` are not accepted before class, struct, interface, enum, or error
declarations. The language does not currently define cross-module subclassing
or implementation permission from such spellings.

Class and interface relationship syntax is defined by the declaration and
interface chapters. It does not imply an extensibility state.

## 23.5 Diagnostics and Conformance

The registered parser diagnostic `ZOM2088` owns member-visibility modifiers in
module or local declaration context. Malformed member modifier groups use the
ordinary registered parser diagnostics.

Conformance covers:

- declaration-site exports and export aliases;
- member visibility retention in AST dumps;
- public-interface and private-type-body binder defaults;
- rejection of module-level `public`, `private`, and `protected`; and
- rejection of source-level `open`, `sealed`, and `final` declaration forms.

Access-control diagnostics require a real enforcement pass and executable
negative conformance before they become part of this chapter.
