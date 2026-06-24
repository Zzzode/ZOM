---
paths:
  - "**"
---

# Design Principles (Non-Negotiable)

> Applies globally across the entire repository. Nothing overrides these.
> See [Section 1: Attention in `/AGENTS.md`](/AGENTS.md) for the short version.
> See `/docs/reports/zom-design-audit-2026-06-23.md` for the audit that produced
> many of the concrete examples below.

---

## 1. Best Practices First

Before writing any new mechanism — **survey the industry.** Pick the mature
winner. Do not design a novel solution to a 20-year-old problem.

### Required Checklist (at least one of)

- [ ] Cite a Rust/Zig/Swift 6/Go 1.22+/C++23 feature that is widely accepted as
      the correct answer for this problem space.
- [ ] If ZOM picks a *different* answer, the doc must list the exact languages it
      will be a minority against, justify why the common answer is unsuitable for
      ZOM's goals, and describe the migration cost if the choice is wrong later.
- [ ] List the three most common bugs/pitfalls caused by this choice in other
      languages and how ZOM avoids them.

### Hard No's

- ❌ Do not invent a new string type, new error union, new async primitive,
  new trait system, etc. unless you can cite two other languages where *the
  same construct with the same semantics* has been production-proven.
- ❌ "It's more elegant" is not a justification when the less elegant approach has
  a 10-year track record of correctness.

---

## 2. Radical Refactoring

Rewrites are the default. Technical debt is paid immediately, not scheduled.

### Rules

1. If a module feels "this is getting messy," **rewrite it** this PR. Do not open
   a follow-up issue for it.
2. A single refactor that touches 40 call sites is better than 40 call sites
   each carrying a one-line compatibility shim. Fix all 40.
3. No `// TODO: refactor this later`. Either refactor it now, or delete the
   code path. TODO comments without an assigned ticket number are rejected on review.
4. Keep no sacred cows. `lexer/`, `parser/`, `binder/`, even `zc/` types can be
   deleted and replaced. The only immutable thing is the project's core goals.
5. Never introduce an intermediate `V2` type or `FooCompat` shim alongside the
   old one. Replace, delete the old, move on.

---

## 3. No Forward Compatibility

We are pre-1.0. Breaking changes are the default, not an exception.
**Nothing in this repo is version-stable except what is explicitly listed in
`docs/stability-manifest.md` (which does not exist yet — so, nothing).**

### Explicit Bans

| Pattern | Why | Alternative |
|---|---|---|
| `ZC_DEPRECATED("Use X instead")` or any deprecation marker | Carries the old API. Users never migrate. | Delete the old API. Fix every call site in the same commit. |
| `#ifdef ZOM_V1_COMPAT`, version-gated grammar, feature flags for old behavior | Dual code paths rot silently and double test matrix. | Delete the old code path. |
| Renaming but keeping the old name as a `using` alias | Aliases accumulate. | Direct rename, fix all references. |
| `// Kept for forward compatibility with future module system` | "Future" never arrives. Build what we need today. | Delete the unused path, write it when the feature actually ships. |
| Old AST kind preserved with a comment `// for 0.1.x compat` | AST kinds are internal. | Remove from `kinds.h`, regenerate all builders. |

### Acceptable Change Pattern

```
1. Change the API / grammar / type.
2. Delete the old surface entirely (macros, overloads, aliases, flags).
3. Fix every call site in the repository in the same commit.
4. Update spec chapters + tests + lit expectations in the same commit.
5. Update AGENTS / .agents/rules docs if this changes a documented default.
```

One commit. No migration epoch.

---

## 4. Remove Useless Things Immediately

"Useless" = any of the following:

1. A type, function, field, enum variant, flag, keyword, grammar production,
   spec chapter, or source file that has **zero call sites or zero references**.
2. A reserved keyword with **no grammar rule** in the current parser.
   - Exception: the keyword has a dedicated spec subsection that explicitly says
     "reserved for v2" and its semantic direction is fully written. Without that,
     the reservation creates ambiguity for users and audit tools.
3. A spec section describing a construct the parser does not accept and for which
   no active implementation exists. Spec drift = spec bug.
4. Any flag defined in an enum but never written (grep the repository for the
   setter or `addFlag` name; zero hits → delete the flag).
5. Placeholder AST nodes with empty bodies, unimplemented visitor branches,
   stubs that abort or panic without an explicit roadmap.
6. "We might need this later" without a concrete, scheduled PR.

### Workflow for Suspected Dead Code

1. Grep the whole repo for the identifier.
2. If zero results (or only a definition with no use), **delete it in the same PR**.
3. If the build passes and tests pass, the deletion was correct.
4. If something breaks downstream, revert + restore in the same PR; the failure
   itself proves it was *not* useless, and we now have a concrete reason documented.

### Reverting Deletions is Cheap

A deleted file or type is 1 `git revert` away from being restored.
Carrying dead code, by contrast, silently:

- inflates compile times,
- bloats audit surface,
- misleads downstream implementers (e.g. "oh the grammar says X, so users can rely on X"),
- makes module / symbol boundaries fuzzy,
- guarantees a larger, harder breaking change when we finally remove it after
  users have already started depending on it by accident.

**Delete first. Argue later.** The git history never forgets.
