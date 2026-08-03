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
