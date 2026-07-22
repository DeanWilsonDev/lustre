---
description: Read and write Lustre (.lustre) stylesheets — a declarative, CSS-like styling language for Iris UI components. Use whenever a project has .lustre files, a global.lustre, or styles Iris components via a `class` prop.
---

Lustre is a declarative, CSS-like DSL — deliberately **not**
Turing-complete (no logic, conditionals, or data binding) — that is the
sole styling and animation layer for Iris UI components. `.iris` files
define structure/behavior; `.lustre` files define appearance, joined to
Iris elements purely through a `class` string prop. It's syntactically a
strict CSS subset, **runtime-loaded/interpreted** (not compiled ahead of
time, unlike Iris's own `iris_cc`), specifically to support hot-reloading
styles without a rebuild. The authoritative reference is
`docs/lustre_core_spec.md` in this repo; this skill is a summary of it.

## Build & consume

```sh
git submodule update --init --recursive
cmake -S . -B build
cmake --build build -j"$(nproc)"

./build/tests/test_lustre
./build/tools/lustre-lsp/tests/test_lustre_lsp
```

There is **no standalone `lustre` CLI compiler** — Lustre is consumed by
linking `liblustre.a` and driving `Lustre::Parser`/`Lustre::Resolver`
programmatically (a host implements `Lustre::IStyleTarget` over its own
widget tree and calls `Resolver::Resolve(...)`). Only the library and an
LSP server (`lustre_lsp`) exist as artifacts.

## File model

- `Name.lustre` — component-scoped stylesheet, paired 1:1 with
  `Name.iris`/`Name.irisx`.
- `global.lustre` — project-wide baseline styles/variables/cross-component
  rules; the only file that can select across component boundaries.

## Selectors — three kinds, never combined

```css
.card { }              /* class selector */
frame { }               /* primitive-element selector, lowercase */
.card:hover { }          /* pseudo-class, suffixed onto a class or primitive */
```

`Frame.card { }` (compound tag+class) is **invalid syntax** — a parse
error. Primitive selectors map to Iris's PascalCase tags via a closed
table: `frame→Frame`, `inline→Inline`, `grid→Grid`, `image→Image`,
`text→Text`, `scroll→Scroll`, `input→Input`. **`icon`/`Icon` is not in this
table yet** — a known, documented gap; you cannot select `<Icon>` by
primitive selector.

## Nesting = real descendant selectors

```css
.card {
    padding: 16px;

    .card-title {
        font-size: 20px;
    }
}

.card:hover {
    .card-title {
        color: var(--color-text-emphasis);
    }
}
```

`.card-title` here only matches an element with `class="card-title"` that
is an actual descendant (any depth) of a `.card`-classed element — this is
real specificity, not file-organization sugar.

**Component-boundary rule (resolve-time, not a compile error):** a
component's own `.lustre` file cannot select into a nested child
component's internals. `Card.lustre`'s `.card .health-bar-fill` never
matches inside a mounted `<HealthBar/>`, even if `HealthBar.lustre` itself
defines `.health-bar-fill`. This is **silent** — a selector that crosses a
component boundary simply matches nothing, no diagnostic. Only
`global.lustre` is unbounded. This is the first thing to check when a style
"isn't applying."

## Cascade

Exactly two layers, no specificity system beyond nesting depth:

1. `global.lustre` — baseline
2. Component's own `Name.lustre` — overrides global for anything both define

No `!important`, no third tier.

## Variables

```css
:root {
    --color-primary: #E8593C;
    --spacing-md: 16px;
}
```

Declared with a `--` prefix, either inside `:root { }` (global.lustre) or
directly inside a component's top-level class block (component-scoped —
shadows a same-named global variable within that file only). Referenced via
`var(--name)`. **No fallback argument** — `var(--x, fallback)` is invalid
syntax. An unresolved reference is a **compile-time error**, never a silent
fallback.

## Units

| Unit | Resolves against | Status |
|---|---|---|
| `px` | Direct logical-pixel value | real |
| `%` | Parent's computed size | real |
| `vw`/`vh` | Application window's current logical size | real |
| `rem` / `em` | — | **stubbed**, no root-font-size convention exists yet |

## Property reference (selected — full real-vs-stubbed table in `docs/lustre_core_spec.md` §2)

**Real (applied by the Penumbra backend today):** `background-color`,
`background-gradient-start`/`-end`, `border-color`, `border-width`,
`border-radius` (single uniform value, no per-corner shorthand),
`padding`/`margin` (1–4 value CSS shorthand), `color` (text only),
`font-family`/`font-size`, `display: stack|inline`, `flex-direction:
row|column`, `gap`, `align-items: start|center|end|stretch`, `max-width`,
`text-overflow: clip|ellipsis` (needs `max-width` set to take effect),
`transition` (color-only, e.g. `background-color 0.2s` — no easing
keyword), `box-shadow` (color + blur radius only, e.g. `box-shadow:
#000000AA 12px;` — no multi-layer list).

**Stubbed (parsed into the IR, not applied by any backend yet):** `width`,
`height`, `transform: scale(<number>)`, most non-`background-color`
pseudo-class-scoped properties on non-Button widgets.

**Container-only property guard:** `display`, `flex-direction`, `gap`,
`align-items` are **rejected with a diagnostic** (not silently resolved)
when applied to a leaf primitive (`Image`/`Text`/`Input`) rather than a
container (`Frame`/`Grid`/`Scroll`/`Inline`). This was a real bug hit in a
consumer project — check container vs. leaf before reaching for a layout
property.

## Worked example

```cpp
// HealthBar.iris
struct HealthBarProps { float current; };

component HealthBar(props: HealthBarProps) {
    std::string barClass = props.current < 25.0f ? "bar-critical" : "bar-normal";
    render {
        <Frame class="health-bar">
            <Frame class={barClass} />
        </Frame>
    }
}
```

```css
/* HealthBar.lustre */
.health-bar {
    --bar-background: #333333;
    background-color: var(--bar-background);
    border-radius: 8px;
}

.bar-normal    { background-color: #4CAF50; }
.bar-critical  { background-color: #E8593C; }
```

## Errors to expect

| Error | Trigger |
|---|---|
| Undefined variable | `var(--x)` not declared globally or in current scope |
| Duplicate selector | Two rule blocks with an identical selector in one file |
| Compound tag+class selector | `Frame.card { }` |
| Container-only property on a leaf | `display`/`flex-direction`/`gap`/`align-items` on `Image`/`Text`/`Input` |

**Deliberately not diagnosed:** component-boundary crossing (matches
nothing, silently), general property-applicability (e.g. `frame {
font-family: ...; }` is accepted without complaint in v1).

## Tooling

- **`lustre_lsp`** — stdio LSP server. Diagnostics are parse-only (compound
  selector, duplicate selector) — it does **not** do resolver-backed
  diagnostics (undefined variable, component-boundary), since those need
  whole-project context a single buffer doesn't have. Completion covers
  property names/pseudo-classes, keyword values for closed-enum properties,
  and `var(--name)` names in scope (this file + sibling `global.lustre`).
  Goto-declaration works for `var(--name)`. Reparses the whole buffer on
  every change — no debouncing.
- **No formatter, no linter, no CLI compiler.**
- Syntax highlighting reuses tree-sitter's `css` grammar directly (no
  bespoke Lustre grammar) — `:TSInstall css`.
- Neovim setup: `editors/nvim/lustre-lsp.lua` +
  `editors/nvim/lustre-treesitter.lua`.

## Things that will bite an agent writing Lustre

- Runtime hot-reload wiring into Iris is still design-phase (core
  parser/resolver + LSP exist; the live-reload path doesn't yet).
- `rem`/`em`, `width`/`height`, and `transform: scale(...)` are parsed but
  **do nothing** in the current backend — don't rely on them for layout.
- `transition` only animates color, and only with a fixed curve — no easing
  keyword, no `@keyframes`.
- Pseudo-class styles (`:hover`/`:active`/`:disabled`) only have a real
  backend field for `background-color` on Button-mapped widgets today;
  targeting other widgets resolves into the IR but has nowhere to land.
- No universal (`*`), attribute, or sibling (`+`/`~`) selectors exist.
- Only single-layer `box-shadow` is supported — a multi-layer list was
  explicitly declined, not just unimplemented.
- If a style silently isn't applying, check the component-boundary rule
  first — it's the most common "why isn't this working" cause and produces
  no diagnostic.

## Project structure reference

```
lustre/
├── README.md
├── include/Lustre/         — Ast.h, Parser.h, Resolver.h (IStyleTarget), ResolvedStyle.h, Token.h
├── src/Lustre/              — Tokenizer.cpp, Parser.cpp, Resolver.cpp
├── tools/lustre-lsp/         — stdio LSP server (Server, CompletionContext, JsonRpc)
├── tests/                     — test_lustre (TokenizerTests, ParserTests, ResolverTests)
├── docs/lustre_core_spec.md    — authoritative v1 spec; read this for anything not covered here
└── editors/nvim/                 — lustre-lsp.lua, lustre-treesitter.lua
```
