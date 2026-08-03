# Lustre — Next Steps

> Living backlog, not a session-changelog: add an entry for anything new
> and open; remove or mark an entry done the moment it's actually
> implemented, rather than letting finished work linger. First file of
> this kind in this repo (previously tracked via `docs/lustre_core_spec.md`
> plus separate decision/handoff docs — those stay as-is, not migrated
> into here retroactively).
> Last updated: 2026-08-03.

## Open items

_None open right now._

**BUG, fixed 2026-08-03: `ResolveCascadedLayers` built each layer's variable scope from
only that layer's own sheet**, so a component-layer `var(--x)` referencing a Global-only
`--x` never resolved. Found while `pharos-proto` updated its dependency pins the same day
(bumping to pick up `justify-content` and the `color`/`font` inheritance work below) — not
a new gap introduced by either of those: this composition shape
(`ResolveCascadedLayers` calling `Resolver::Resolve()` twice, once per layer, each with the
other sheet passed as `nullptr`) is exactly what `penumbra-ui-backend`'s old hand-rolled
`StyleResolution.cpp::ResolveStyle`/`MergeInto` already did before the "Add CSS-style
color/font inheritance (§1.7)" commit (`7779d94`) moved that composition into this repo —
this bug predates that commit entirely and was latent in `penumbra-ui-backend` the whole
time the two-layer cascade existed.

**Symptom:** any declaration in a *component*-layer `.lustre` file that references a
variable declared only in the *global* layer's `:root` block silently failed to resolve —
the property was dropped entirely (as if never written), not applied with some wrong
value.

**Root cause**, confirmed by direct instrumentation, not guessed: `ResolveCascadedLayers`
called `Resolver::Resolve()` twice, each time handing it a `StylesheetSet` with only *one*
of `{Global, Component}` populated. `Resolve()` itself builds
`const VariableScope Scope = BuildVariableScope(Sheets.Global, Sheets.Component);` from
the *isolated* `Sheets` it was just handed for that one call, not the original caller's
full pair. So while resolving the Component layer, `BuildVariableScope(Global=nullptr,
Component=Sheets.Component)` built a scope containing only the component file's own
`--variables`; a Global-only variable was invisible to that pass entirely.
`ResolveVariableRef`'s lookup missed, a `ResolveDiagnostic` ("Undefined variable...") was
recorded, and `ApplyDeclaration` received an empty `Resolved` vector for that value and
returned before setting anything on `Out`.

**Confirmed empirically** before fixing: instrumented `pharos-proto`'s vendored build to
print the `ResolvedStyle` obtained for a `Frame class="toolbar"` / `Text class=
"toolbar-label"` pair, where `Toolbar.lustre` references `var(--color-panel-background)`/
`var(--color-text-primary)`, both declared only in a separate `global.lustre`'s `:root`
block. Every property came back unset (`BackgroundColor`/`BorderColor`/`TextColor`/
`Padding` all `nullopt`) even though both `Global`/`Component` `Stylesheet*` pointers were
valid and non-null.

**Fix** (`src/Lustre/Resolver.cpp`): `ResolveCascadedLayers` no longer routes through
`Resolver::Resolve()`/`MergeCascadeInto` at all. It now builds the `VariableScope` *once*
from the full, un-isolated `Sheets` (both members exactly as the caller passed them in),
then calls `ApplyLayer` directly for each layer with that shared scope and each layer's
own `Unbounded` value (`true` for Global, `false` for Component), writing both layers'
declarations onto one shared `ResolvedStyle` in cascade order — matching how
`Resolver::Resolve()`'s own body already behaved when given both sheets in a single call.
`MergeCascadeInto` (the old separate-then-merge step, only reachable from this one call
site) is gone entirely; the gradient/box-shadow pair-completion check it used to help
enforce was factored into a small shared `FinalizePairedProperties` helper, now called by
both `Resolver::Resolve()` and `ResolveCascadedLayers` so the two paths can't drift.
`Resolver::Resolve()`'s own public single-call behavior/signature is unchanged (it was
never buggy — every existing test calling it directly already passed both sheets in one
call, which is why this slipped past `tests/ResolverTests.cpp`'s existing coverage
entirely).

New regression coverage: `tests/InheritanceTests.cpp`'s "a component-layer declaration can
reference a variable declared only in the global layer" (through `ResolveStyle()`, the
public entry point that calls `ResolveCascadedLayers`) — verified it fails without the fix
(reverted the fix locally, confirmed red) and passes with it. Full suite: `test_lustre`
42/42, `test_lustre_lsp` 15/15.

**Practical impact this fixes**: `pharos-proto`'s `global.lustre --variables` bridge (all
12 of its component `.lustre` files using `var(--name)` referencing a shared
`global.lustre`) was non-functional across that whole app until this landed — every
migrated file's colors/spacing silently resolved to nothing (`Label`s rendered fully
transparent `{0,0,0,0}` text, `Box`es got no background/border) instead of the intended
theme values. Went unnoticed in that repo's own prior screenshot verification because a
fully-transparent panel over a dark window background still looks plausibly
"dark-themed" at a glance, and `penumbra-ui-backend`'s debug-mode
`ClassedNodes>0 && ResolvedNodes==0` diagnostic (meant to catch exactly this class of
"resolved nothing" bug) doesn't fire because *some* node in a typical tree usually
resolves at least one non-`var()`-dependent property, keeping the tree-wide count above
zero even when individual nodes resolve nothing at all. `pharos-proto` needs no code
change of its own for this — just picking up the new `lustre` commit through its existing
`main`-tracking `FetchContent` pin.

`justify-content` (main-axis distribution — `start`/`center`/`end`/`space-between`,
parallel to `align-items`'s existing cross-axis `Align`) shipped 2026-08-03, closing the
gap `pharos-proto`'s hand-rolled `ThreeZoneRow` ("left/center/right justify in one row")
existed to work around: `Frame`'s `display: stack; flex-direction: row` could only pack
children sequentially from the start, with no way to spread them across the container the
way CSS's `justify-content` does. New `enum class Justify { Start, Center, End,
SpaceBetween }` (`ResolvedStyle.h`, alongside `Align`) and `ResolvedStyle::JustifyContent`;
a `justify-content` case in `Resolver.cpp`'s `ApplyDeclaration`, same shape as the existing
`align-items` case; `IsContainerOnlyProperty` gained `"justify-content"` alongside
`display`/`flex-direction`/`gap`/`align-items` — a leaf has no children to distribute along
a main axis either, so it gets the same `ResolveDiagnostic` guard. See
`tests/ResolverTests.cpp`'s two new cases (resolves on a container, diagnosed on a leaf).
**Not yet done**: `penumbra-ui-backend`'s `StyleApplier.cpp` doesn't apply
`ResolvedStyle::JustifyContent` yet — `Box` (Penumbra's own widget) has no main-axis-
distribution concept today (`Box::Measure`/`Box::Arrange` pack children sequentially), so
that's a real `Box`-layout-algorithm change in `penumbra-proto`, then a `StyleApplier`
mapping in `penumbra-ui-backend` — cross-referenced here, not this repo's to close.
`ThreeZoneRow` itself can't actually switch over to `.three-zone-row { justify-content:
space-between; }` until that lands upstream. No `SpaceAround`/`SpaceEvenly`: not requested
by anything; a general `flex-grow`/`flex-shrink`/`flex-basis` system was explicitly not
requested either — `justify-content`'s four keyword values are the concrete ask `pharos-
proto` has, not a full flexbox-equivalent sizing model.

A container-only-property guard (`display`, `flex-direction`, `gap`,
`align-items` reported as a `ResolveDiagnostic` rather than silently resolved
when applied to a leaf tag) shipped 2026-07-22, fixing `display: stack`
silently corrupting leaf widgets (hit in `pharos-proto` 2026-07-22
componentizing its toolbar's `<Input>` field) — see `Resolver.cpp`'s
`IsContainerTag`/`IsContainerOnlyProperty` and the two new
`tests/ResolverTests.cpp` cases. The original write-up assumed `Resolver.cpp`
had no idea which Iris tag consumed a class, but `IStyleTarget::PrimitiveTag()`
already carried that (Resolver.cpp's `PrimitiveTagForSelector` mapping
includes `input` → `Input` and `scroll` → `Scroll`, beyond the 5 primitives
`lustre_core_spec.md` §1.1 documents) — the guard belonged here after all.
Container/leaf classification (`Frame`/`Grid`/`Scroll`/`Inline` = container,
`Image`/`Text`/`Input` = leaf) isn't written down in the spec itself yet;
worth folding into `lustre_core_spec.md` §2 if another leaf/container
question comes up.

`box-shadow` (color + blur radius, parsed into
`ResolvedStyle::ShadowColor`/`ShadowBlurRadiusLogical`) shipped 2026-07-21 —
see `Resolver.cpp`'s `box-shadow` case and `tests/ResolverTests.cpp`.

### Explicitly not requested

- **A multi-layer shadow list** (CSS's `box-shadow` accepts a comma-separated
  list of shadows). No known consumer needs more than one layer; the
  single color+blur pair Lustre implements matches Penumbra's
  `DrawDropShadow` signature exactly. Revisit only if a real consumer needs
  stacked shadows.
