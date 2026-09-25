#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace Lustre {

// docs/lustre_core_spec.md §3 — the resolver's output. Backend-agnostic:
// carries every property in §2 (real or stubbed), no Penumbra (or any other
// backend) type anywhere in here. A bridge repo (`iris-penumbra-backend` for
// Penumbra) maps this onto concrete style structs.

struct Color {
    std::uint8_t R{0};
    std::uint8_t G{0};
    std::uint8_t B{0};
    std::uint8_t A{255};

    friend bool operator==(const Color&, const Color&) = default;
};

struct EdgeInsets {
    float Left{0.0F};
    float Top{0.0F};
    float Right{0.0F};
    float Bottom{0.0F};

    friend bool operator==(const EdgeInsets&, const EdgeInsets&) = default;
};

struct FontRequest {
    std::string Path;
    float       SizeLogical{0.0F};

    friend bool operator==(const FontRequest&, const FontRequest&) = default;
};

struct ColorTransition {
    std::string Property;
    float       DurationSeconds{0.0F};

    friend bool operator==(const ColorTransition&, const ColorTransition&) = default;
};

enum class Display { Stack, Inline };
enum class FlexDirection { Row, Column };
enum class Align { Start, Center, End, Stretch };
// Main-axis distribution, parallel to Align's cross-axis alignment above --
// CSS's justify-content, scoped to the four keyword values a known consumer
// (pharos-proto's hand-rolled `ThreeZoneRow`) actually needs. No `SpaceAround`/
// `SpaceEvenly`: not requested by anything, see docs/next_steps.md.
enum class Justify { Start, Center, End, SpaceBetween };
// docs/penumbra_iris_lustre_componentization_gaps_requirements.md's
// InspectorRow migration finding: Label has no truncation concept at all,
// so a long value can overflow instead of clipping. Only meaningful paired
// with `max-width` below -- mirrors CSS's own text-overflow, which only
// does anything once something has actually constrained the box's width.
enum class TextOverflow { Clip, Ellipsis };
// `white-space` (`text` only) -- CSS's own keyword pair, scoped to the two
// values a known consumer (Cairn's Card Modal body text) actually needs:
// `nowrap` (the default -- single line, pairs with max-width/text-overflow
// above) and `normal` (reflow across as many lines as the box's own width
// allows). Real, not stubbed, the same way max-width/text-overflow are real:
// a backend `Label` can wrap its own text without needing Penumbra's general
// fixed-size-override gap closed first.
enum class WhiteSpace { Nowrap, Normal };

enum class LengthUnit { Px, Percent, Vw, Vh };

struct Length {
    float      Value{0.0F};
    LengthUnit Unit{LengthUnit::Px};

    friend bool operator==(const Length&, const Length&) = default;
};

struct ResolvedStyle {
    std::optional<Color>       BackgroundColor;
    // A top-to-bottom two-stop gradient fill (docs/
    // penumbra_iris_lustre_componentization_gaps_requirements.md §2) --
    // `background-gradient-start`/`background-gradient-end`. Populated as a
    // pair, same as Font below: the resolver only sets these once both
    // colors are present in the same rule (or the cascade across global +
    // component layers supplies both), never just one half.
    std::optional<Color>       BackgroundGradientStart;
    std::optional<Color>       BackgroundGradientEnd;
    std::optional<Color>       BorderColor;
    std::optional<float>       BorderWidth;
    std::optional<float>       BorderRadius;
    std::optional<EdgeInsets>  Padding;
    std::optional<EdgeInsets>  Margin;
    std::optional<Color>       TextColor;
    std::optional<FontRequest> Font;
    std::optional<Display>       DisplayMode;
    std::optional<FlexDirection> FlexDirectionMode;
    std::optional<float>         Gap;
    std::optional<Align>         AlignItems;
    std::optional<Justify>       JustifyContent;
    std::optional<ColorTransition> Transition;
    // A soft rectangular shadow -- color + blur radius, mirroring Penumbra's
    // own Renderer::DrawDropShadow two-argument shape. Populated as a pair,
    // same convention as BackgroundGradientStart/BackgroundGradientEnd above:
    // `box-shadow: <color> <length>` is one shorthand property, and the
    // resolver only keeps a color with no blur radius (or vice versa) if
    // that's genuinely all the shorthand supplied.
    std::optional<Color> ShadowColor;
    std::optional<float> ShadowBlurRadiusLogical;

    // Pseudo-class-scoped overlays — present only if the source rule defined
    // them. Recursive rather than flat fields so a pseudo-class block can in
    // principle override any property, matching §1.2's "a pseudo-class block
    // nests exactly like any other selector block."
    std::shared_ptr<ResolvedStyle> Hover;
    std::shared_ptr<ResolvedStyle> Active;
    std::shared_ptr<ResolvedStyle> Disabled;

    std::optional<Length> Width;
    std::optional<Length> Height;
    std::optional<Length> MinWidth;
    std::optional<Length> MaxWidth;
    std::optional<float>  TransformScale;

    std::optional<TextOverflow> TextOverflowMode;
    std::optional<WhiteSpace>   WhiteSpaceMode;

    std::optional<float> FlexGrow;
    std::optional<Color> ScrollbarThumbColor;
    std::optional<Color> ScrollbarTrackColor;
    std::optional<float> ScrollbarWidthLogical;
};

} // namespace Lustre
