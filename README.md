# lustre

> **Status:** Design handoff — no code exists yet.

Lustre is the styling and animation layer for [Iris](https://github.com/DeanWilsonDev/iris).
Where `.iris`/`.irisx` files define structure (component composition, the `render { }` element
tree), `.lustre` files are the sole authoring surface for appearance — colors, layout-adjacent
visual properties, transitions — resolved down to concrete values per rendering backend.

## Why this exists

Iris Core deliberately does not define Lustre's syntax, cascade rules, or how its properties
map onto backend-specific style structs (Penumbra's `BoxStyle`/`ButtonStyle`/etc., or a future
Umbra Engine backend's own equivalents) — see `iris`'s `docs/iris_core_spec.md` §4/§8 and
`docs/iris_handoff.md` §4. That is Lustre's own design problem, scoped here instead.

What Iris Core already fixes, and Lustre must live within:

- Style is never inline in `.iris` files — no `style="..."` prop, ever.
- The `class` prop is valid on any element and accepts a string class name; it is the sole
  join between an Iris element and its Lustre declarations.
- Iris ships no default styling opinions — Lustre resolution is what actually reads a
  `class` value and produces concrete style values.

## Relationship to Iris's roadmap

This is Iris's Stage 4 (`iris`'s `docs/iris_handoff.md` §6 / `docs/iris_next_steps.md`):
Stages 0–3 (language spec, preprocessor front end, Penumbra backend, reactive runtime) are
done. Stage 4 — Lustre-lite style resolution — has not been designed yet; this repo is where
that design happens, and where the resulting compiler/resolver will eventually live.

## What's here

- `docs/` — design docs, decision records, and open questions, following the same pattern
  `iris`'s own `docs/` directory used during its Stage 0 scoping (a handoff doc first, decision
  docs as design questions get resolved).

## Build

Nothing yet — pure design phase.
