# Lustre Core — Language Specification (v1)

> **Status:** Design spec, no implementation yet. Authoritative over
> `docs/lustre_handoff.md` and the earlier
> `../fearless-hq/Projects/lustre-stylesheet-language/lustre-design-document.md` draft where
> they disagree — this document is what a Lustre parser/resolver implementer should build
> against. `lustre_handoff.md` remains the record of *why* each decision below was made; this
> document is *what* was decided, without the reasoning trail.

## 0. Overview

Lustre is the styling and animation layer for [Iris](../../iris). Where `.iris`/`.irisx`
files define structure — component composition, the `render { }` element tree — `.lustre`
files are the sole authoring surface for appearance: colors, box model, layout, interaction
states, and a narrow set of animations. Style is never inline in `.iris` files; the `class`
prop is the only bridge between an Iris element and its Lustre declarations.

### Design goals

1. **CSS familiarity.** Selectors, properties, units, and cascade concepts are borrowed
   directly from CSS wherever they make sense, on the assumption that most authors already
   know CSS.
2. **Spec the whole language, not just today's backend.** Lustre defines properties and
   selectors it should eventually support even where the current backend (Penumbra) can't yet
   back them. Unsupported features are stubbed — accepted, resolved into the IR, applied
   nowhere — never silently dropped from the grammar and never a compile error for existing
   only on paper. Every such gap is recorded as a feature request in the relevant repo (see
   `penumbra-proto/docs/lustre_style_gaps_requirements.md`,
   `iris/docs/lustre_hotreload_iris_requirements.md`).
3. **Strictly scoped to appearance.** No logic, no conditionals, no data binding, no access to
   props or state. A dynamic look (e.g. a health bar changing color as health drops) is done
   by the host language passing a different `class` value based on state — Lustre defines what
   each class looks like; the host language decides which class is active.
4. **Backend-agnostic core.** This repo owns parsing, cascade/selector resolution, and
   variable resolution, producing a generic resolved-style IR (§3). It has no dependency on
   Penumbra or any other backend — mapping the IR to a concrete backend's style structs is a
   separate bridge repo's job (`iris-penumbra-backend` for Penumbra today), mirroring the
   `iris`/`iris-penumbra-backend` split exactly.

### The three-layer model

| Layer | Language | Responsibility |
| --- | --- | --- |
| Structure + logic | Iris (`.iris`/`.irisx`) | What exists and how it behaves |
| Style + animation | Lustre (`.lustre`) | What it looks like |
| Rendering | A backend (Penumbra today) | What is actually drawn |

### File model

| File | Purpose |
| --- | --- |
| `Name.lustre` | Component-scoped stylesheet, paired 1:1 with `Name.iris`/`Name.irisx`. |
| `global.lustre` | Project-wide baseline styles, variables, and cross-component rules. |

```
Button.iris
Button.lustre
HealthBar.iris
HealthBar.lustre
global.lustre
```

Loaded at application runtime (not compiled ahead of time like `iris_cc`), specifically to
support hot-reloading styles without a rebuild. See §7.

## 1. Grammar

### 1.1 Selectors

Three selector kinds, never combined into a compound selector:

- **Class selector** — `.card { }`. Matches any element whose single `class` prop equals
  `card`. An Iris element carries at most one class; there is no space-separated multi-class
  form.
- **Primitive-element selector** — `frame { }`, `inline { }`, `grid { }`, `image { }`,
  `text { }`. Lowercase, deliberately not matching `iris_core_spec.md`'s PascalCase tag casing
  (`<Frame>`) — that casing rule exists to distinguish primitives from components inside
  JSX-like angle brackets, a problem that doesn't exist in a CSS-like file. A closed mapping
  table translates each lowercase name to its real Iris tag at resolution time:

  | Lustre selector | Iris tag |
  | --- | --- |
  | `frame` | `Frame` |
  | `inline` | `Inline` |
  | `grid` | `Grid` |
  | `image` | `Image` |
  | `text` | `Text` |

  Components are never selectable by tag name — only by `class` — so this table is closed and
  will only grow if Iris Core itself grows a new primitive.
- **Pseudo-class selector** — `:hover`, `:active`, `:disabled`, nested inside a class or
  primitive selector's block. Valid on any element `class`/a primitive selector can target, in
  full generality — not limited to what the current backend can execute (§4, §6).

`Frame.card { }` (a compound tag+class selector) is **not valid syntax**.

### 1.2 Nesting, descendant matching, and specificity

A selector nested inside another's block is a real descendant selector, not file-organization
sugar:

```
.card {
    padding: 16px;

    .card-title {
        font-size: 20px;
    }
}
```

`.card-title` here only matches an element carrying `class="card-title"` that is an actual
descendant, at any depth, of an element carrying `class="card"` — not merely co-located in the
same file. This selector outranks a same-named top-level `.card-title { }` rule the way real
CSS specificity would (deeper nesting = higher specificity).

A pseudo-class block nests exactly the same way, and can itself contain further descendant
selectors:

```
.card:hover {
    .card-title {
        color: var(--color-text-emphasis);
    }
}
```

**Component-boundary rule:** a component's own `.lustre` file can only select elements it
directly authored in its own `render { }` tree. A nested/descendant selector cannot reach into
a child component's internals — e.g. `Card.lustre`'s `.card .health-bar-fill` cannot match
inside a `<HealthBar/>` used within `Card`, even if `HealthBar.lustre` defines its own
`.health-bar-fill` class. This is enforced as resolve-time behavior, not a compile-time
diagnostic (§6): the descendant-matching walk treats a child component's own root as opaque and
does not search past it. A selector that "reaches too far" simply matches nothing there,
silently — the same as any CSS selector that matches zero elements.

`global.lustre` is the one place without this restriction — it has no "own subtree" to be
bounded by, so a selector written there reaches however far its class names naturally imply.
This is the intended mechanism for genuinely cross-component styling.

### 1.3 Cascade

Exactly two layers, no specificity system beyond nesting (§1.2):

1. `global.lustre` — the baseline.
2. A component's own `Name.lustre` — overrides `global.lustre` for anything both define.

There is no third tier, no `!important`, and no cross-component override beyond what
`global.lustre` itself expresses.

### 1.4 Variables

```
:root {
    --color-primary: #E8593C;
    --spacing-md: 16px;
    --font-body: "assets/fonts/body.ttf";
}
```

- Declared with a `--` prefix inside a `:root { }` block (`global.lustre`) or directly inside a
  component's top-level class block (component-scoped).
- Referenced with `var(--name)`. **No fallback argument** — `var(--x, fallback)` is not valid
  syntax. Every reference must resolve to a real declared variable or the build fails
  (§6) — Lustre leans compile-time-strict here, the same as Iris does for unresolved imports.
- A component-scoped `--variable` sharing a name with one in `global.lustre` **shadows** it,
  but only within that component's own file — `global.lustre`'s value is unchanged for every
  other component. This is the same override rule as §1.3, applied to variables rather than
  treated as a separate mechanism.

### 1.5 Units

| Unit | Resolves against |
| --- | --- |
| `px` | Direct value — maps 1:1 to the backend's logical-pixel fields. |
| `%` | The parent's computed size. |
| `vw` / `vh` | The application window's current logical size (not necessarily fullscreen — Penumbra is a resizable desktop tool window; `vw`/`vh` mean "percent of window," not "percent of screen"). |
| `rem` | A root font size — **stubbed**, no root-font-size convention exists yet (open question, §8). |
| `em` | The current element's font size — **stubbed**, same reason. |

### 1.6 Comments

`/* ... */`, matching CSS. No line-comment form, consistent with CSS convention.

## 2. Property reference

One entry per property. **Status** is `real` (a real backend field exists and this maps to it
today) or `stubbed` (accepted and resolved into the IR, per §0 goal 2, but not yet applied by
any backend — see the linked feature-request doc for what would unblock it).

| Property | Values | Applies to | Status | Notes |
| --- | --- | --- | --- | --- |
| `background-color` | `<color>` | any | real | Maps to `BoxStyle::ColorBackground`. |
| `border-color` | `<color>` | any | real | Maps to `BoxStyle::ColorBorder`. |
| `border-width` | `<length>` | any | real | Maps to `BoxStyle::BorderWidth`. |
| `border-radius` | `<length>` | any | real | Single uniform value only — `BoxStyle::BorderRadius` is one float, no CSS per-corner shorthand. |
| `padding` | 1–4 `<length>` (CSS shorthand) | any | real | Maps to `BoxStyle::Padding` (`EdgeInsets`). |
| `margin` | 1–4 `<length>` (CSS shorthand) | any | real | Maps to `BoxStyle::Margin` (`EdgeInsets`). |
| `color` | `<color>` | `text` (or a class applied to `<Text>`) | real | Maps to `Label::ColorText`. |
| `font-family` | `var(--name)` resolving to a font path string | `text` | real | Combines with `font-size` into a font-request key — see below. |
| `font-size` | `<length>` | `text` | real | Combines with `font-family`; changing either requests a different `FontHandle`, not a live field mutation. Resolver caches by `(path, size)`. |
| `display` | `stack` \| `inline` | any container | real | Maps to `Box::Layout` (`None`/a stack mode). |
| `flex-direction` | `row` \| `column` | any container with `display: stack` | real | Maps to `Box::Layout` (`HorizontalStack`/`VerticalStack`). |
| `gap` | `<length>` | any container | real | Maps to `Box::ChildGap`. |
| `align-items` | `start` \| `center` \| `end` \| `stretch` | any container | real | Maps to `Box::CrossAlignment`. |
| `width` | `<length>` \| `%` | any | **stubbed** | No fixed-size override exists in Penumbra — sizing is purely intrinsic. See `penumbra-proto/docs/lustre_style_gaps_requirements.md` §3. |
| `height` | `<length>` \| `%` | any | **stubbed** | Same as `width`. |
| `transform` | `scale(<number>)` | any | **stubbed** | No transform primitive exists at all in Penumbra. See `penumbra-proto/docs/lustre_style_gaps_requirements.md` §2. |
| `transition` | `<color-property> <time>` (e.g. `background-color 0.2s`) | any | real, narrow | **Color properties only**, no easing-curve keyword. Maps to a per-instance `Anim::AnimatedColor` with the given duration used directly as its `TimeConstantSeconds`. Non-color transitions and `@keyframes` are out of scope for v1 — no backend wiring exists yet (`Anim::Tween` is unwired). |

### Pseudo-class-scoped variants

`:hover`, `:active`, `:disabled` blocks may set any property listed above; today only
`background-color` has a real backend field to receive a pseudo-class-scoped value
(`ButtonStyle::ColorBackgroundHovered`/`ColorBackgroundPressed`/`ColorBackgroundDisabled`), and
only for elements the resolver maps onto Penumbra's `Button`. A `:hover`/`:active`/`:disabled`
rule targeting any other primitive/widget type (`Frame`, `Checkbox`, ...) is resolved into the
IR but has nowhere to land yet — **stubbed**, see
`penumbra-proto/docs/lustre_style_gaps_requirements.md` §1. Interaction-state *detection*
(mouse hover/press/disabled) is entirely the backend's own responsibility
(`Button::UpdateInteractionState` already does this per frame from `InputState`) — Lustre and
Iris never detect interaction state themselves, only supply a complete resolved style once per
class change.

## 3. Resolved-style IR (sketch)

The output of Lustre's resolver — backend-agnostic, consumed by a bridge repo
(`iris-penumbra-backend` for Penumbra). Exact field types are a sketch, not a commitment; the
implementer should feel free to adjust shape as long as it stays backend-agnostic (no Penumbra
types) and carries every property in §2, real or stubbed:

```cpp
struct LustreColor { uint8_t R, G, B, A; };
struct LustreEdgeInsets { float Left, Top, Right, Bottom; };
struct LustreFontRequest { std::string Path; float SizeLogical; };
struct LustreColorTransition { std::string Property; float DurationSeconds; };

enum class LustreDisplay { Stack, Inline };
enum class LustreFlexDirection { Row, Column };
enum class LustreAlign { Start, Center, End, Stretch };

struct LustreResolvedStyle {
    std::optional<LustreColor>            BackgroundColor;
    std::optional<LustreColor>            BorderColor;
    std::optional<float>                  BorderWidth;
    std::optional<float>                  BorderRadius;
    std::optional<LustreEdgeInsets>       Padding;
    std::optional<LustreEdgeInsets>       Margin;
    std::optional<LustreColor>            TextColor;
    std::optional<LustreFontRequest>      Font;
    std::optional<LustreDisplay>          Display;
    std::optional<LustreFlexDirection>    FlexDirection;
    std::optional<float>                  Gap;
    std::optional<LustreAlign>            AlignItems;
    std::optional<LustreColorTransition>  Transition;

    // Pseudo-class-scoped overlays — present only if the source rule defined them.
    std::optional<LustreResolvedStyle> Hover;
    std::optional<LustreResolvedStyle> Active;
    std::optional<LustreResolvedStyle> Disabled;

    // Stubbed properties — carried through the IR so a future backend mapping has
    // something to consume, but no current backend reads these.
    std::optional<float> WidthLogical;
    std::optional<float> HeightLogical;
    std::optional<float> TransformScale;
};
```

`Hover`/`Active`/`Disabled` are recursive rather than flat fields so a pseudo-class block can
in principle override any property, including ones the base rule didn't set — matching §1.2's
"a pseudo-class block nests exactly like any other selector block."

## 4. Worked example

Updated from the earlier draft's `HealthBar` example for current Iris syntax and Lustre's
lowercase primitive selectors:

```cpp
// HealthBar.iris
struct HealthBarProps {
    float current;
};

component HealthBar(props: HealthBarProps) {
    std::string barClass = props.current < 25.0f ? "bar-critical" : "bar-normal";

    render {
        <Frame class="health-bar">
            <Frame class={barClass} />
        </Frame>
    }
}
```

```
/* HealthBar.lustre */
.health-bar {
    --bar-background: #333333;

    width: 200px;   /* stubbed — accepted, not yet applied (see §2) */
    height: 16px;   /* stubbed */
    background-color: var(--bar-background);
    border-radius: 8px;
}

.bar-normal {
    background-color: #4CAF50;
}

.bar-critical {
    background-color: #E8593C;
}
```

## 5. `.lustre` runtime loading

Unlike `iris_cc`, `.lustre` files are parsed by a real parser shipped inside the Lustre
runtime, loaded at application startup rather than compiled ahead of time — enabling
hot-reload of styles without a rebuild. This is a deliberate divergence from Iris's own
build-time-preprocessor philosophy (see `lustre_handoff.md` §3 for the full reasoning,
including its connection to Iris's own eventual need for host-language hot-reload). The exact
file-change-detection mechanism (fs watcher vs. explicit reload call) is an open question
(§8), as is the precise hook by which a widget's `ClassName` change triggers re-resolution —
both implementation-stage concerns once the resolver itself exists.

Restyling every mounted widget on a reload (not just the one whose class just changed)
requires a reference to the application's root widget. This is being added to Iris itself —
`iris::RegisterRoot(Umbra::IWidget*)`/`iris::GetRoot()`, likely folded into `IrisRuntime` — see
`iris/docs/lustre_hotreload_iris_requirements.md`.

## 6. Error catalogue (starter)

| Error | Trigger | Diagnostic |
| --- | --- | --- |
| Undefined variable | `var(--x)` where `--x` isn't declared globally or in the current component's scope | Compile-time error. No fallback form exists. |
| Duplicate selector | Two rule blocks with the literally identical selector in one file | Compile-time error — almost always a copy-paste mistake. Selectors that merely *tie* in computed specificity without being identical still resolve by source order, unflagged. |
| Compound tag+class selector | `Frame.card { }` | Compile-time parse error — not valid syntax (§1.1). |
| Multiple classes | `class="card bold"` in the paired `.iris` file | Not a Lustre error — `class` accepting only one identifier is enforced by Iris Core itself, not by Lustre. |

**Deliberately not diagnosed:**

- **Component-boundary crossing** (§1.2) — unresolvable at compile time (a lone `.lustre` file
  can't see the real render tree), so it's pure resolve-time behavior: a selector reaching past
  a child-component boundary just matches nothing there, the same as any selector matching
  zero elements.
- **Property-applicability** (e.g. `frame { font-family: ...; }`) — no validation in v1, even
  though it would be checkable for primitive selectors (Lustre knows a primitive selector's
  target type via §1.1's mapping table). Skipped for consistency: the same check can't be done
  for class selectors (Lustre doesn't parse `.iris` files to know which primitive a class ends
  up on), so partial coverage wasn't judged worth the complexity yet. Revisit once the resolver
  exists.

## 7. Open questions

- A `:root { font-size: ... }`-style convention for `rem`/`em` (§1.5) — resolvable entirely
  inside Lustre's own resolver, no Iris or Penumbra change implied, just not designed yet.
- The exact hot-reload file-change-detection mechanism (§5) — tangled up with the broader Iris
  hot-reload question in `iris/docs/lustre_hotreload_iris_requirements.md`.
- The precise runtime hook by which a `ClassName` change triggers re-resolution and
  re-application of a widget's style (§5) — an implementation-stage integration point once the
  resolver exists.
- Universal (`*`), attribute, and sibling (`+`/`~`) selectors — no clear need yet.
- The `umbra-engine` backend's own style-struct mapping — deferred the same way Iris itself
  defers that backend, Stage 6.
- Whether `opacity` (whole-element alpha, distinct from a single color's own alpha channel)
  belongs in v1 — not discussed yet; flagged here rather than silently added or omitted.
