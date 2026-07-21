# Lustre — Next Steps

> Living backlog, not a session-changelog: add an entry for anything new
> and open; remove or mark an entry done the moment it's actually
> implemented, rather than letting finished work linger. First file of
> this kind in this repo (previously tracked via `docs/lustre_core_spec.md`
> plus separate decision/handoff docs — those stay as-is, not migrated
> into here retroactively).
> Last updated: 2026-07-21.

## Open items

None currently. `box-shadow` (color + blur radius, parsed into
`ResolvedStyle::ShadowColor`/`ShadowBlurRadiusLogical`) shipped 2026-07-21 —
see `Resolver.cpp`'s `box-shadow` case and `tests/ResolverTests.cpp`.

### Explicitly not requested

- **A multi-layer shadow list** (CSS's `box-shadow` accepts a comma-separated
  list of shadows). No known consumer needs more than one layer; the
  single color+blur pair Lustre implements matches Penumbra's
  `DrawDropShadow` signature exactly. Revisit only if a real consumer needs
  stacked shadows.
