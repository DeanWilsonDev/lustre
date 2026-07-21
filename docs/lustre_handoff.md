# Lustre — Design Handoff

> **Status:** Historical design-handoff record — written when no Lustre code existed yet.
> **Superseded as the language reference** by `docs/lustre_core_spec.md` (v1), written once
> every decision below was resolved — where the two disagree, the spec wins. This document
> remains the record of *why* each decision was made; the spec is *what* was decided. Code now
> exists (`src/Lustre/`, `tools/lustre-lsp/` — see the top-level `README.md`); this document is
> not updated to track that, by design.
> **Scope:** Scoping decisions for Lustre, the styling/animation layer for Iris
> (`../iris`), reconciled against the current, post-pivot state of Iris and the real
> capabilities of Penumbra (`../penumbra-proto`) as its first backend.

## 1. Why this document exists

An earlier design pass produced `../fearless-hq/Projects/lustre-stylesheet-language/lustre-design-document.md`
while Iris was still scoped against Umbra Engine as its only backend, and before Iris's
Stage 1 preprocessor pivot (component/props/state/event grammar moved to the host language).
That document is a good starting point for Lustre's *syntax* (selectors, variables,
transitions) but is stale on two counts: it targets an engine that doesn't exist yet instead
of Penumbra (real, available now), and its one worked example predates the pivot. This
document reconciles that draft against `iris`'s current `docs/iris_core_spec.md` and
`docs/iris_handoff.md`, and against Penumbra's actual style/animation code
(`penumbra-proto`'s `include/Penumbra/Widgets/Styles.h`, `include/Penumbra/Anim/Animation.h`),
not just its aspirations.

**Guiding principle for every gap found below:** Iris, Lustre, and Penumbra are growing
together, the same way Iris and Penumbra already have (see `iris_handoff.md`'s history of
the `<Image>` decode-from-path gap and the `IWidgetLifecycle` gap — both flagged, then
landed). Lustre specs the full language it should eventually be, not just what today's
backend happens to support. Where a backend can't yet back a Lustre feature, that feature is
**stubbed, not silently dropped and not a compile error** — and a feature-request doc is
filed in the relevant repo. Two are filed alongside this one:
`iris/docs/next-steps.md`'s "Live-widget root registry, for Lustre's hot-reload" entry (the
requirements doc it was originally filed as has since been coalesced into that file),
`penumbra-proto/docs/lustre_style_gaps_requirements.md`.

## 2. What Iris Core already fixes (Lustre must live within these)

Per `iris_core_spec.md` §4/§8:

- Style is never inline in `.iris`/`.irisx` files — no `style="..."` prop, ever.
- The `class` prop is valid on any element, accepts one string class name, and is the sole
  join between an Iris element and its Lustre declarations.
- Iris ships zero default styling opinions — Lustre resolution is what actually reads a
  `class` value and produces concrete style values.
- `class` already flows all the way to a real Penumbra widget today: `Walker.cpp` calls
  `Builder.className(...)` at build time, and `WidgetBase::ClassName` is a live runtime
  `std::string` field, updated via `IrisPropDiff::ClassName` whenever a reconcile changes
  which class is active (e.g. `class={barClass}` switching between `"bar-critical"`/
  `"bar-normal"`). This is currently completely inert — nothing reads `ClassName` to apply an
  actual style — and is Lustre's natural hook point.

## 3. Decisions resolved during this scoping pass

### Cascade

Two layers only, no specificity system beyond nesting (below): a component's own
`Name.lustre` overrides `global.lustre` for anything both define. This was already the shape
of the earlier draft's `global.lustre` + per-component-file model; this pass confirms it as
the whole cascade, not a starting point for something richer.

### Primitive selectors stay lowercase; a closed mapping table targets Iris's PascalCase tags

`iris_core_spec.md`'s PascalCase rule (`<Frame>`, `<Inline>`) exists to visually distinguish
primitives from components inside JSX-like angle brackets — a problem that doesn't exist in a
CSS-like selector file. Lustre's primitive-element selectors stay lowercase (`frame { }`,
`inline { }`), matching CSS convention and the earlier draft's own style. A small, closed
mapping table (`frame→Frame`, `inline→Inline`, `grid→Grid`, `image→Image`, `text→Text`) in the
`lustre` repo translates them to Iris's real tag names at resolution time. This only covers
the five Core primitives — components were never selectable by tag name (only by `class`), so
there's no lowercase/PascalCase ambiguity for them to begin with.

### One class per element

`class` holds exactly one identifier, never a space-separated list. Every example in the
earlier draft already assumed this; it's now a fixed rule, not just a convention. Composing
looks happens by defining a combined class or by nesting elements — not by combining classes
on one element.

### Nested selectors are real descendant selectors, with real specificity

`.card { .card-title { ... } }` is not file-organization sugar — it only matches a
`.card-title`-classed element that is an actual descendant (any depth, not just a direct
child) of a `.card`-classed one, and it outranks a same-named top-level rule the way real CSS
specificity would. A pseudo-class block (`:hover`) nests exactly the same way — it can itself
contain further descendant selectors (`.card:hover { .card-title { color: red; } }`,
restyling a child when the parent is hovered).

### Component-scoped `.lustre` cannot cross a child-component boundary; `global.lustre` can

A component's own stylesheet can only select elements it directly authored — a nested
selector cannot reach into a child component's internals (e.g. `Card.lustre`'s `.card
.health-bar-fill` cannot match inside a `<HealthBar/>` used within `Card`, even if
`HealthBar.lustre` happens to define a `.health-bar-fill` class of its own). This is the
precise, nesting-specific version of the earlier draft's "styles do not bleed into child
components." If cross-component styling is genuinely needed, it belongs in `global.lustre`
instead — which has no "own subtree" to be bounded by in the first place, so a selector
written there reaches however far its class names naturally imply.

### No compound tag+class selectors

`Frame.card { }` is not valid — primitive-element selectors (`Frame { }`) and class selectors
(`.card { }`) are always separate rule kinds, never combined into one selector. Since every
styled element already has at most one class, a class selector alone already identifies it
narrowly enough; combining with a tag adds a redundant axis.

### Variables: component-scoped shadowing, no `var()` fallback

A component redeclaring a `--variable` name already used in `global.lustre` shadows it, but
only within that component's own file — everyone else still sees `global.lustre`'s value.
This is the same override rule the cascade already uses, applied to variables rather than
treating them as a separate mechanism. `var()` has no CSS-style fallback argument — every
reference must resolve to a real declared variable (global or component-scoped) or the build
fails. Lustre already leans compile-time-strict elsewhere (backend-gated primitives,
unresolved imports); a typo'd variable name should be a build error, not a silent fallback.

### Units: the full CSS set, with `rem`/`em` stubbed pending a root-font-size concept

`px`, `%`, `vw`, `vh`, `rem`, `em` are all in the v1 property grammar. `px` maps directly to
Penumbra's logical-pixel float fields; `%` resolves against the parent's computed size
(already meaningful given Penumbra's own layout/`Arrange` pass); `vw`/`vh` resolve against the
actual window size — meaningful even though Penumbra's a resizable desktop tool window, not a
fixed fullscreen surface, it's just "the window" instead of "the screen." `rem`/`em` need a
root/current font-size concept that doesn't exist in Iris or Penumbra yet — accepted in the
grammar, stubbed at resolution time, tracked as an open question below rather than a backend
feature request (this one's resolvable entirely inside Lustre's own resolver once a
`:root { font-size: ... }`-style convention is designed — no Penumbra or Iris change implied).

### `border-radius`: single uniform value only

Matches `BoxStyle::BorderRadius` (one float) exactly — no CSS per-corner shorthand. Per-corner
radius was explicitly "not requested" by any real consumer yet
(`penumbra_theming_requirements.md` §6); no need to spec, let alone stub, a shape nobody's
asked for.

### Fonts: plain path via variables, no separate `@font-face`-style construct

`--font-body: "assets/fonts/body.ttf"` (already the earlier draft's convention), referenced
via `font-family: var(--font-body)`. No dedicated top-level font-declaration block — Label's
actual field is a single `FontHandle`, obtained from `Render::IFontBackend` at a specific path
+ point size (`penumbra_dpi_requirements.md`'s `TTF_OpenFont(Path, PointSizeLogical *
DpiScaleFactor)`); there's no weight/style-variant concept for a dedicated construct to manage.
One resolution nuance worth recording: `font-family` and `font-size` together form the actual
font-request key (path + point size), not two independently-mutable style knobs — changing
either one means requesting a different `FontHandle` from the backend, not adjusting a live
field on an existing one. The resolver should cache by (path, size) to avoid re-requesting an
identical handle every reconcile.

### Layout (stack direction, gap, cross-align) is Lustre's, CSS-style

`display`/`flex-direction`/`gap`/`align-items`-shaped properties resolve to Penumbra's
`Box::Layout`/`ChildGap`/`CrossAlignment` — currently **exposed through no Iris prop at all**;
the only thing that touches them today is a hardcoded stub (`<Grid>` always maps to
`LayoutMode::HorizontalStack`, ignoring whatever the author actually wrote — a known gap,
`iris_core_spec.md` §9). Putting layout in Lustre rather than as new Iris props matches CSS's
own convention (flexbox is styling, not markup) and Lustre's explicit CSS-familiarity design
goal — and gives the `<Grid>` stub a real replacement path once Lustre can express direction/
gap/align, rather than requiring a separate round of new Iris-level prop design.

### `width`/`height`: documented, unimplemented

Same treatment as `transform`: real, named properties in the v1 spec, not omitted, even though
nothing in Penumbra can back them yet. Confirmed by reading `WidgetBase`/`Box`/`Label`
directly: sizing is entirely intrinsic (`Measure`/`Arrange` from content + `Padding`), no
fixed-size override concept exists anywhere. Omitting two of the most basic CSS properties
from a language whose explicit goal is CSS familiarity would be more surprising than stubbing
them. Tracked in `penumbra-proto/docs/lustre_style_gaps_requirements.md`.

### Pseudo-classes: full language surface now, stub what the backend can't do yet, file it

`:hover`, `:active`, `:disabled` are valid on any element `class` can be attached to, in the
full generality the language should eventually have — not limited to what Penumbra's style
structs happen to support today. Concretely, today only `ButtonStyle` has
`ColorBackgroundHovered`/`ColorBackgroundPressed`/`ColorBackgroundDisabled`; `CheckboxStyle`
and plain `BoxStyle` (used by `Frame`) have no interaction-state fields at all. A `:hover`
rule targeting a `Frame`- or `Checkbox`-classed element compiles and is recorded in the
resolved style IR; applying it to those widget types is a stub until Penumbra grows the
equivalent fields — see `penumbra-proto/docs/lustre_style_gaps_requirements.md`. Confirmed
separately: Penumbra's `Button` already detects hover/press/disabled state itself, per frame,
from `InputState` (`Button::UpdateInteractionState`) — Lustre/Iris never need to detect
interaction state themselves, only supply a complete style struct once per class change for
Penumbra's own internals to pick from.

### `transform`: documented, unimplemented

`transform: scale(...)` (and, by extension, translate/rotate) is in the v1 spec as a real,
named property — not omitted — but nothing in Penumbra can execute it yet (no transform
primitive of any kind exists). It's accepted at parse/resolve time and stubbed at the
application boundary, per the growing-together principle above. Tracked in
`penumbra-proto/docs/lustre_style_gaps_requirements.md`.

### `transition`: v1 is color-only, mapped to Penumbra's actual animation primitive

Penumbra has no duration+easing-curve tween wired to anything live — only
`Anim::AnimatedColor::Animate(Target, DeltaSeconds, TimeConstantSeconds)`, a framerate-
independent exponential *approach* toward a possibly-still-moving target, and a separate
`Anim::Tween` (0→1 timeline, real easing curves) that's "designed for, not yet wired up."
`@keyframes` and non-color transitions need that wiring to exist first and are deferred
entirely. v1's `transition` property is narrower than CSS's: it accepts only color-valued
properties, and no easing-curve keyword (`ease`/`linear`) — `AnimatedColor` has no curve
concept, just one time constant — e.g. `transition: background 0.2s` compiles to a real
per-instance `AnimatedColor` with that value as its time constant.

### Runtime-loaded, not compiled ahead of time

Unlike `iris_cc` (an offline preprocessor baked into the C++ build), `.lustre` files are
parsed by a real parser shipped inside the Lustre runtime, loaded at application startup —
because hot-reloading styles without a rebuild is close to Lustre's whole reason for existing
as an authoring surface separate from hardcoded C++. This is a deliberate divergence from
Iris's own build-time-preprocessor philosophy, made with awareness of the larger context: Iris
is designed to eventually host more than one host language (C++23 now, a first-party
scripting language later), and that future host will need real hot-reload of component code
itself, not just styling data. Lustre's hot-reload is scoped much narrower than that (styling
data only, not component logic) but the underlying need — something in the running
application able to react to a changed file without a full rebuild — is shared. Flagged in
`iris/docs/next-steps.md`'s "Live-widget root registry, for Lustre's hot-reload" entry rather
than solved here.

### Iris gains root-widget registration, for hot-reload's restyle pass

Applying a resolved style at mount and re-applying one when `IrisPropDiff::ClassName` changes
both live in `iris-penumbra-backend` (it already touches `Walker.cpp`'s build step and
`PenumbraWidgetAdapter::ApplyPropDiff`) — no new decision needed there. But a hot-reloaded
`.lustre` file needs to restyle *every* mounted widget, not just the one whose class just
changed, which means something needs a reference to the root to walk from. Iris currently has
none — every runtime "Root" in the codebase is a compile-time AST root, not a mounted widget.

This is being added to Iris itself, not scoped to Lustre or to `iris-penumbra-backend`:
`Umbra::IWidget` is already the backend-agnostic interface the reconciler walks/mutates
through, so holding a `Umbra::IWidget*` and handing it back out has no backend-specific content
— building it per-backend would mean every backend reimplementing identical
store-a-pointer/expose-a-getter logic. `IrisRuntime` already holds/tracks live widget state the
same way. Proposed shape: `iris::RegisterRoot(Umbra::IWidget*)`/`iris::GetRoot()`, likely folded
into `IrisRuntime`, callable by any consuming app right after it builds its tree — see
`iris/docs/next-steps.md`'s "Live-widget root registry, for Lustre's hot-reload" entry for the
full reasoning. Benefit beyond
Lustre: the next cross-cutting concern needing "the whole mounted tree" gets this for free.

### Lustre stays backend-agnostic; Penumbra mapping lives in `iris-penumbra-backend`

Mirrors the `iris`/`iris-penumbra-backend` split exactly, for the same reason: Lustre is
meant to support more than one backend over time (Penumbra now, an Umbra Engine backend
later, deferred), so it shouldn't pull in Penumbra's build just to parse a stylesheet. This
repo (`lustre`) owns parsing, cascade/selector resolution, and variable resolution, producing
a generic resolved-style IR — backend-agnostic named properties (`backgroundColor`,
`borderRadius`, `hoverBackgroundColor`, etc.), not a concrete Penumbra struct.
`iris-penumbra-backend` gains the code that reads that IR and calls `ApplyStyle()` with a
concrete `BoxStyle`/`ButtonStyle`/`CheckboxStyle`, the same division of labor it already has
for `IrisComponent` → real Penumbra widgets.

## 4. Error catalogue (starter)

- **Undefined `var()` reference** — compile-time error, no fallback (§3 above).
- **Duplicate selector, exact match** — two rule blocks with the literally identical selector
  in the same file is a compile-time error (almost always a copy-paste mistake), consistent
  with Lustre's existing compile-time strictness elsewhere. This only catches the *same*
  selector written twice — two genuinely different selectors that happen to tie in computed
  specificity still fall back to source order, unflagged, the same as real CSS.
- **Component-boundary crossing is *not* a compile error, deliberately.** A standalone
  `.lustre` file has no way to know which descendant elements belong to which nested component
  instance — only the real render tree at resolve time knows that. So this rule is pure
  resolve-time behavior: the resolver's descendant-matching walk treats a child component's own
  root as opaque and doesn't search past it. A selector that "reaches too far" simply never
  matches anything past that boundary — silent, not diagnosed, the same as a CSS selector that
  matches zero elements today.
- **No property-applicability validation in v1**, not even for primitive selectors where it
  would be checkable (e.g. `frame { font-family: ...; }` — Lustre already knows a primitive
  selector's target type via the lowercase→PascalCase table, the same information
  `iris_core_spec.md` uses to say `<Text font=...>` is invalid outside Lustre). Skipped for
  now for consistency: class selectors can't be checked the same way (Lustre doesn't parse
  `.iris` files to know which primitive a class ends up on), so validating only the primitive
  case would be partial coverage for a deferred amount of value — revisit once the resolver
  exists and it's clear how much this matters in practice.

## 5. Deliberately deferred, not designed here

- Universal (`*`), attribute, and sibling (`+`/`~`) selectors — not in the earlier draft, no
  clear need yet.
- The exact mechanism by which hot-reload detects a changed `.lustre` file (fs watcher vs. an
  explicit reload call) — tangled up with the broader Iris hot-reload question, belongs in
  that feature request rather than resolved here.
- The precise runtime hook by which a `ClassName` change (mount, or a reconcile that swaps
  classes) triggers Lustre to re-resolve and re-apply a widget's style — a real integration
  point, but an implementation-stage question once the resolver itself exists, not a
  scoping-stage one.
- A `:root { font-size: ... }`-style convention for `rem`/`em` — noted above as Lustre's own
  problem to solve, not blocking this handoff.
- The `umbra-engine` backend's own style-struct mapping — same "deferred, Stage 6" status
  Iris itself gives it.

## 6. What the next design pass should produce

**Done — see `docs/lustre_core_spec.md` (v1).** Grammar (selectors, cascade, variables,
units), the v1 property reference (one entry per property, real vs. stubbed), a sketch of the
resolved-style IR `iris-penumbra-backend` will consume, a worked example updated for current
Iris syntax and Lustre's lowercase primitive selectors, the error catalogue from §4 above, and
an open-questions section carrying forward §5.
