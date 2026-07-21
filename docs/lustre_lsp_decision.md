# lustre-lsp — architecture decision

## 0. Scope

An LSP server for `.lustre` files (`tools/lustre-lsp/`), built directly against the
`lustre` static library rather than reimplementing any part of the language. Driven by an
explicit ask for Neovim support: diagnostics, completion, goto-definition (for
`var(--name)` references today — see §4 for what's deliberately deferred), and syntax
highlighting.

## 1. The one thing that makes this different from iris-lsp

`iris`'s own LSP (`iris/tools/iris-lsp/`) exists mainly to solve one hard problem: a
`.iris` file mixes Iris's own `render { }` grammar with ordinary host-language (C++) code,
so most of that tool is a virtual-document/position-mapping layer in front of a real proxy
to `clangd`.

Lustre has no equivalent problem. A `.lustre` file is one language, in full, parsed by a
real recursive-descent parser (`Lustre::Parser`) that already exists in this repo for the
runtime resolver's own sake (`docs/lustre_core_spec.md` §5). `lustre-lsp` is therefore
just a thin LSP-protocol shell around that parser and tokenizer — no proxy, no generated
buffer, no position translation, no background thread. Every open document is reparsed in
full on each `didChange` (Lustre files are small stylesheets; this repo's own test suite's
timing precedent for cheap re-parsing made debouncing not worth the complexity for v1,
same call `iris-lsp` made for `Iris::CompileFile`).

## 2. Syntax highlighting — reusing tree-sitter-css, not a bespoke grammar

`iris-lsp` reuses tree-sitter's `cpp` grammar for `.iris` files, since most of a `.iris`
file already *is* valid C++ outside `render{}`. Lustre's own grammar (§1.1/§1.2 of the
core spec) is, by design, a strict subset of CSS: class/pseudo-class selectors, real
nested/descendant rules, `--variable` declarations, `var(--name)` references (single-arg,
no fallback — still a valid call expression to a CSS parser), hex colors, numbers with
unit suffixes, and `scale(1.5)`-shaped single-argument function calls. Primitive-element
selectors (`frame { }`) parse as ordinary CSS type selectors, the same way `div { }` would
in real CSS.

Verified directly: every construct in `lustre_core_spec.md`'s own worked examples,
tokenized through `tree-sitter-css`, produces **zero ERROR nodes**. So — mirroring the
iris decision exactly, just with an even cleaner fit — `editors/nvim/lustre-treesitter.lua`
just registers the `css` parser against the `lustre` filetype. No grammar to write or
maintain, and nvim-treesitter's own CSS highlight queries already color selectors,
properties, strings, numbers, and colors correctly.

## 3. What lustre-lsp actually implements

- **Diagnostics** — `Lustre::ParseResult::Errors` published as-is. This already covers
  every compile-time-diagnosable error in the core spec's error catalogue (§6): a
  compound tag+class selector (`Frame.card { }`) and a duplicate selector within one
  file. Undefined-variable and component-boundary-crossing errors need whole-project
  `Resolver` context a single-file parse doesn't have — out of scope here, the same limit
  the parser's own header comment already documents.
- **Completion** — `tools/lustre-lsp/CompletionContext.h/.cpp` classifies a cursor
  position by tokenizing the whole buffer and walking the token stream (brace depth,
  the nearest unresolved `:`, an unclosed `var(`) into one of three contexts:
  - `Statement` — offers primitive-element selector names, pseudo-classes, and (inside a
    block) property names from `lustre_core_spec.md` §2. Lustre's own grammar makes a
    property name and a nested primitive selector genuinely ambiguous until the *next*
    token (`ident:` vs `ident{`), so this offers both rather than guessing.
  - `Value` — offers that property's keyword values, for the handful of properties with a
    closed enum (`display`, `flex-direction`, `align-items`, `text-overflow`).
  - `VarRef` — offers every `--name` in scope: this file's own `:root`/component-scoped
    variables, plus a sibling `global.lustre`'s, mirroring §1.3's two-layer cascade.
- **Goto declaration** — a `var(--name)` reference resolves to its `VariableDeclaration`'s
  `SourceLocation`, searched in this file's own scope first, then a sibling
  `global.lustre`. Trivial once tokens are located, since every AST node already carries a
  `SourceLocation`.

## 4. Deliberately deferred

- **Resolver-backed diagnostics** (undefined variable, component-boundary crossing) need
  a whole-project view (every `.iris` render tree, or at least the full `Stylesheet` set)
  that a single open buffer doesn't have. Revisit once there's a project-wide index, the
  same way `iris-lsp` only resolves imports it already has a project root/config for.
- **Jumping from a `class="card"` string in a `.iris` file straight into `.lustre`'s
  `.card { }`** — the natural editor workflow this was asked for, but the handler for it
  lives in `iris-lsp`'s own `HandleDefinition` (it fires while a `.iris` buffer, not a
  `.lustre` one, is open), not here. Separate follow-up in the `iris` repo.

## 5. Dependencies

Vendored `libs/amanuensis` (this ecosystem's own JSON library) as a git submodule, the
same way `iris` does, purely for `JsonRpc.h`'s stdio `Content-Length` framing — the
`lustre` core library itself has no JSON dependency and doesn't link against it.
