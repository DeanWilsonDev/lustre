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
});
