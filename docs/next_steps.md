# Lustre — Next Steps

> Living backlog, not a session-changelog: add an entry for anything new
> and open; remove or mark an entry done the moment it's actually
> implemented, rather than letting finished work linger. First file of
> this kind in this repo (previously tracked via `docs/lustre_core_spec.md`
> plus separate decision/handoff docs — those stay as-is, not migrated
> into here retroactively).
> Last updated: 2026-07-22.

## Open items

_None open right now._

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
