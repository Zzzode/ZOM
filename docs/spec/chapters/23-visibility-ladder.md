# Chapter 23 - Visibility Syntax and Retained Facts

## 23.1 Scope

This chapter defines the current source syntax and compiler facts for
module-level exports and type-member visibility. The compiler retains these
facts, but it does not yet enforce cross-module or member-access permissions.

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
explicitly. The binder records `Export` and `Public` flags for successfully
resolved exported bindings and aliases.

`public`, `private`, and `protected` are not module-level declaration
modifiers. Their use before a module-level or local declaration emits
`ZOM2088 VisibilityModifierRequiresMemberContext`.

The current compiler has no `CompilerSession` or published cross-file module
interface. Export flags therefore do not constitute a complete cross-module
access-control implementation.

## 23.3 Member Visibility Syntax

Callable members, fields, and class constants may carry one member-visibility
modifier:

```ebnf
MemberVisibility ::= 'public' | 'private' | 'protected'
```

```zom
class Session {
    private let token: str;
    public fun user_id() -> i32 { 0 }
    protected fun refresh_hook() -> unit {}
}
```

The AST stores the spelling as `Public`, `Private`, `Protected`, or `Default`.
The binder converts it to the corresponding symbol flag. Unqualified members
in an interface receive a public binder fact; unqualified members in other
supported type bodies receive a private binder fact.

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
