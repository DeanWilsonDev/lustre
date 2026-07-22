#include "cimmerian/test.hpp"

#include "Lustre/Parser.h"
#include "Lustre/Resolver.h"

using namespace Lustre;

namespace {

// A minimal, in-memory IStyleTarget for testing -- stands in for whatever
// real Iris tree a host adapter would wrap.
class FakeElement : public IStyleTarget {
public:
    FakeElement(std::string ClassNameValue, std::string PrimitiveTagValue, FakeElement* ParentValue = nullptr,
                bool ComponentRoot = false)
        : ClassName_(std::move(ClassNameValue)), PrimitiveTag_(std::move(PrimitiveTagValue)), Parent_(ParentValue),
          ComponentRoot_(ComponentRoot) {}

    std::string          ClassName() const override { return ClassName_; }
    std::string          PrimitiveTag() const override { return PrimitiveTag_; }
    bool                  IsComponentRoot() const override { return ComponentRoot_; }
    const IStyleTarget*   Parent() const override { return Parent_; }

private:
    std::string  ClassName_;
    std::string  PrimitiveTag_;
    FakeElement* Parent_;
    bool         ComponentRoot_;
};

std::optional<Stylesheet> ParseOrFail(const std::string& Source, const std::string& FilePath) {
    Parser TheParser(Source, FilePath);
    ParseResult Result = TheParser.Parse();
    if (!Result.Errors.empty()) {
        return std::nullopt;
    }
    return std::move(Result.Sheet);
}

} // namespace

DESCRIBE("Resolver", {
    IT("resolves the HealthBar worked example end to end", {
        const auto Sheet = ParseOrFail(R"(
.health-bar {
    --bar-background: #333333;

    background-color: var(--bar-background);
    border-radius: 8px;
}

.bar-critical {
    background-color: #E8593C;
}
)",
                                        "HealthBar.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Bar("bar-critical", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                          R;
        std::vector<ResolveDiagnostic>    Diagnostics;
        const ResolvedStyle Style = R.Resolve(Bar, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        REQUIRE_TRUE(Style.BackgroundColor.has_value());
        ASSERT_TRUE(Style.BackgroundColor->R == 0xE8 && Style.BackgroundColor->G == 0x59 && Style.BackgroundColor->B == 0x3C);
        // .bar-critical's own background-color wins for this element --
        // .health-bar's border-radius doesn't apply, since Bar itself only
        // carries class="bar-critical", not "health-bar".
        ASSERT_FALSE(Style.BorderRadius.has_value());
    });

    IT("resolves a background-gradient-start/-end pair", {
        const auto Sheet = ParseOrFail(R"(
.selected-row {
    background-gradient-start: #4A90FF;
    background-gradient-end: #2A5ADD;
}
)",
                                        "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("selected-row", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Row, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.BackgroundGradientStart.has_value());
        REQUIRE_TRUE(Style.BackgroundGradientEnd.has_value());
        ASSERT_TRUE(Style.BackgroundGradientStart->R == 0x4A && Style.BackgroundGradientStart->G == 0x90 &&
                    Style.BackgroundGradientStart->B == 0xFF);
        ASSERT_TRUE(Style.BackgroundGradientEnd->R == 0x2A && Style.BackgroundGradientEnd->G == 0x5A &&
                    Style.BackgroundGradientEnd->B == 0xDD);
    });

    IT("a lone background-gradient-start with no -end resolves to neither", {
        const auto Sheet = ParseOrFail(".selected-row { background-gradient-start: #4A90FF; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("selected-row", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Row, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_FALSE(Style.BackgroundGradientStart.has_value());
        ASSERT_FALSE(Style.BackgroundGradientEnd.has_value());
    });

    IT("a gradient pair split across global and component layers still resolves", {
        const auto GlobalSheet = ParseOrFail(".selected-row { background-gradient-start: #4A90FF; }", "global.lustre");
        const auto ComponentSheet = ParseOrFail(".selected-row { background-gradient-end: #2A5ADD; }", "Card.lustre");
        REQUIRE_TRUE(GlobalSheet.has_value());
        REQUIRE_TRUE(ComponentSheet.has_value());

        FakeElement Row("selected-row", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Row, StylesheetSet{&*GlobalSheet, &*ComponentSheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.BackgroundGradientStart.has_value());
        REQUIRE_TRUE(Style.BackgroundGradientEnd.has_value());
    });

    IT("resolves a box-shadow color+blur-radius shorthand", {
        const auto Sheet = ParseOrFail(R"(
.gradient-button {
    box-shadow: #000000AA 12px;
}
)",
                                        "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Button("gradient-button", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Button, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.ShadowColor.has_value());
        REQUIRE_TRUE(Style.ShadowBlurRadiusLogical.has_value());
        ASSERT_TRUE(Style.ShadowColor->R == 0x00 && Style.ShadowColor->G == 0x00 && Style.ShadowColor->B == 0x00 &&
                    Style.ShadowColor->A == 0xAA);
        ASSERT_TRUE(*Style.ShadowBlurRadiusLogical == 12.0F);
    });

    IT("a lone box-shadow color with no blur radius resolves to neither", {
        const auto Sheet = ParseOrFail(".gradient-button { box-shadow: #000000AA; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Button("gradient-button", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Button, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_FALSE(Style.ShadowColor.has_value());
        ASSERT_FALSE(Style.ShadowBlurRadiusLogical.has_value());
    });

    IT("resolves max-width and text-overflow: ellipsis", {
        const auto Sheet = ParseOrFail(R"(
.inspector-row-value {
    max-width: 220px;
    text-overflow: ellipsis;
}
)",
                                        "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("inspector-row-value", "Text", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.MaxWidthLogical.has_value());
        ASSERT_TRUE(*Style.MaxWidthLogical == 220.0F);
        REQUIRE_TRUE(Style.TextOverflowMode.has_value());
        ASSERT_TRUE(*Style.TextOverflowMode == TextOverflow::Ellipsis);
    });

    IT("resolves text-overflow: clip", {
        const auto Sheet = ParseOrFail(".x { max-width: 100px; text-overflow: clip; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("x", "Text", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.TextOverflowMode.has_value());
        ASSERT_TRUE(*Style.TextOverflowMode == TextOverflow::Clip);
    });

    IT("max-width with no text-overflow still resolves (clamps size, no truncation mode set)", {
        const auto Sheet = ParseOrFail(".x { max-width: 100px; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("x", "Text", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.MaxWidthLogical.has_value());
        ASSERT_FALSE(Style.TextOverflowMode.has_value());
    });

    IT("resolves a descendant selector across real ancestors", {
        const auto Sheet = ParseOrFail(R"(
.card {
    padding: 16px;

    .card-title {
        color: #FFFFFF;
    }
}
)",
                                        "Card.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Card("card", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Title("card-title", "Text", &Card);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Title, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_TRUE(Style.TextColor->R == 0xFF && Style.TextColor->G == 0xFF && Style.TextColor->B == 0xFF);
    });

    IT("does not let a descendant selector cross a child-component boundary", {
        const auto Sheet = ParseOrFail(R"(
.card {
    .health-bar-fill {
        background-color: #FF0000;
    }
}
)",
                                        "Card.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Card("card", "Frame", nullptr, /*ComponentRoot=*/true);
        // HealthBarRoot stands in for a mounted <HealthBar/> instance's own
        // root -- IsComponentRoot() is true for it because, from Card's
        // resolver call, everything at and below it belongs to a different
        // component's own file (docs/lustre_core_spec.md §1.2).
        FakeElement HealthBarRoot("health-bar", "Frame", &Card, /*ComponentRoot=*/true);
        FakeElement Fill("health-bar-fill", "Frame", &HealthBarRoot);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Fill, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        // `.card .health-bar-fill` never matches -- the ancestor walk from
        // Fill stops at HealthBarRoot (its own component boundary) and
        // never reaches Card.
        ASSERT_FALSE(Style.BackgroundColor.has_value());
    });

    IT("lets global.lustre reach across component boundaries", {
        const auto Sheet = ParseOrFail(R"(
.card {
    .health-bar-fill {
        background-color: #FF0000;
    }
}
)",
                                        "global.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Card("card", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement HealthBarRoot("health-bar", "Frame", &Card, /*ComponentRoot=*/true);
        FakeElement Fill("health-bar-fill", "Frame", &HealthBarRoot);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Fill, StylesheetSet{&*Sheet, nullptr}, /*Unbounded=*/true, Diagnostics);

        REQUIRE_TRUE(Style.BackgroundColor.has_value());
        ASSERT_TRUE(Style.BackgroundColor->R == 0xFF);
    });

    IT("lets a component's own file override global.lustre for the same class", {
        const auto GlobalSheet = ParseOrFail(".card { background-color: #000000; }", "global.lustre");
        const auto ComponentSheet = ParseOrFail(".card { background-color: #FFFFFF; }", "Card.lustre");
        REQUIRE_TRUE(GlobalSheet.has_value());
        REQUIRE_TRUE(ComponentSheet.has_value());

        FakeElement Card("card", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style =
            R.Resolve(Card, StylesheetSet{&*GlobalSheet, &*ComponentSheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.BackgroundColor.has_value());
        ASSERT_TRUE(Style.BackgroundColor->R == 0xFF); // component layer wins over global
    });

    IT("resolves a pseudo-class overlay separately from the base style", {
        const auto Sheet = ParseOrFail(R"(
.button {
    background-color: #4CAF50;
}

.button:hover {
    background-color: #66BB6A;
}
)",
                                        "Button.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Button("button", "Frame", nullptr, true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Button, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.BackgroundColor.has_value());
        ASSERT_TRUE(Style.BackgroundColor->G == 0xAF); // base .button

        REQUIRE_TRUE(Style.Hover != nullptr);
        REQUIRE_TRUE(Style.Hover->BackgroundColor.has_value());
        ASSERT_TRUE(Style.Hover->BackgroundColor->G == 0xBB); // :hover overlay, distinct from base
    });

    IT("reports an undefined variable reference", {
        const auto Sheet = ParseOrFail(".card { background-color: var(--not-declared); }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Card("card", "Frame", nullptr, true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Card, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_FALSE(Diagnostics.empty());
        ASSERT_FALSE(Style.BackgroundColor.has_value());
    });

    IT("shadows a global variable with a component-scoped one of the same name", {
        const auto GlobalSheet = ParseOrFail(":root { --accent: #000000; }", "global.lustre");
        const auto ComponentSheet = ParseOrFail(R"(
.card {
    --accent: #FFFFFF;

    background-color: var(--accent);
}
)",
                                                 "Card.lustre");
        REQUIRE_TRUE(GlobalSheet.has_value());
        REQUIRE_TRUE(ComponentSheet.has_value());

        FakeElement Card("card", "Frame", nullptr, true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style =
            R.Resolve(Card, StylesheetSet{&*GlobalSheet, &*ComponentSheet}, false, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        REQUIRE_TRUE(Style.BackgroundColor.has_value());
        ASSERT_TRUE(Style.BackgroundColor->R == 0xFF); // component-scoped --accent shadows global.lustre's
    });

    IT("reports display: stack applied to a leaf primitive instead of silently resolving it", {
        const auto Sheet = ParseOrFail(".json-path { display: stack; }", "Toolbar.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Field("json-path", "Input", nullptr, true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Field, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_FALSE(Diagnostics.empty());
        ASSERT_FALSE(Style.DisplayMode.has_value());
    });

    IT("still resolves display: stack on a real container", {
        const auto Sheet = ParseOrFail(".toolbar { display: stack; }", "Toolbar.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Bar("toolbar", "Frame", nullptr, true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Bar, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        REQUIRE_TRUE(Style.DisplayMode.has_value());
        ASSERT_TRUE(*Style.DisplayMode == Display::Stack);
    });
});
