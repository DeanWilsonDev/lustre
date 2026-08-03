#include "cimmerian/test.hpp"

#include "Lustre/Parser.h"
#include "Lustre/Resolver.h"

using namespace Lustre;

namespace {

// Same minimal in-memory IStyleTarget ResolverTests.cpp uses.
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

DESCRIBE("Inheritance (§1.7)", {
    IT("a child with no color of its own inherits its parent's", {
        const auto Sheet = ParseOrFail(".row { color: #FFFFFF; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("row", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Label("", "Text", &Row); // no class at all -- nothing of its own could match

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Label, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_TRUE(Style.TextColor->R == 0xFF && Style.TextColor->G == 0xFF && Style.TextColor->B == 0xFF);
    });

    IT("a child's own explicit color wins over an inherited one", {
        const auto Sheet = ParseOrFail(R"(
.row { color: #FFFFFF; }
.label { color: #000000; }
)",
                                        "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("row", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Label("label", "Text", &Row);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Label, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_TRUE(Style.TextColor->R == 0x00 && Style.TextColor->G == 0x00 && Style.TextColor->B == 0x00);
    });

    IT("inherits through an intermediate node that sets no color of its own", {
        const auto Sheet = ParseOrFail(".ancestor { color: #FFFFFF; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Grandparent("ancestor", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Parent("", "Frame", &Grandparent); // no class, no rule of its own
        FakeElement Child("", "Text", &Parent);        // same

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Child, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_TRUE(Style.TextColor->R == 0xFF);
    });

    IT("the nearest ancestor that sets color wins over a farther one that also sets it", {
        const auto Sheet = ParseOrFail(R"(
.far { color: #FF0000; }
.near { color: #0000FF; }
)",
                                        "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Grandparent("far", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Parent("near", "Frame", &Grandparent);
        FakeElement Child("", "Text", &Parent);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Child, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_TRUE(Style.TextColor->B == 0xFF && Style.TextColor->R == 0x00); // .near's blue, not .far's red
    });

    IT("non-inherited properties do not flow down from an ancestor", {
        const auto Sheet = ParseOrFail(".row { background-color: #FF0000; padding: 8px; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("row", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Child("", "Frame", &Row);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Child, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        ASSERT_FALSE(Style.BackgroundColor.has_value());
        ASSERT_FALSE(Style.Padding.has_value());
    });

    IT("font inherits the same way as color", {
        const auto Sheet = ParseOrFail(R"(
.row {
    font-family: "assets/fonts/body.ttf";
    font-size: 14px;
}
)",
                                        "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("row", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Child("", "Text", &Row);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Child, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        REQUIRE_TRUE(Style.Font.has_value());
        ASSERT_TRUE(Style.Font->Path == "assets/fonts/body.ttf");
        ASSERT_TRUE(Style.Font->SizeLogical == 14.0F);
    });

    IT("a root element with no ancestor and no match stays unset, no crash", {
        const auto Sheet = ParseOrFail(".unrelated { color: #FFFFFF; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Root("", "Frame", nullptr, /*ComponentRoot=*/true); // no parent at all

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Root, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        ASSERT_FALSE(Style.TextColor.has_value());
    });

    IT("a :hover overlay is unaffected by inheritance, even when the base color was inherited", {
        const auto Sheet = ParseOrFail(R"(
.row { color: #FFFFFF; }

.child:hover {
    background-color: #FF0000;
}
)",
                                        "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("row", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Child("child", "Frame", &Row); // has its own class (for :hover to match), but no color rule

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Child, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        REQUIRE_TRUE(Style.TextColor.has_value()); // base value inherited from .row
        ASSERT_TRUE(Style.TextColor->R == 0xFF && Style.TextColor->G == 0xFF && Style.TextColor->B == 0xFF);

        REQUIRE_TRUE(Style.Hover != nullptr);
        ASSERT_TRUE(Style.Hover->BackgroundColor.has_value()); // the child's own :hover rule still applies
        ASSERT_FALSE(Style.Hover->TextColor.has_value());      // but the overlay itself never inherits
    });

    IT("still resolves the two-layer cascade correctly through the new entry point", {
        const auto GlobalSheet = ParseOrFail(".card { color: #000000; }", "global.lustre");
        const auto ComponentSheet = ParseOrFail(".card { color: #FFFFFF; }", "Card.lustre");
        REQUIRE_TRUE(GlobalSheet.has_value());
        REQUIRE_TRUE(ComponentSheet.has_value());

        FakeElement Card("card", "Frame", nullptr, /*ComponentRoot=*/true);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Card, StylesheetSet{&*GlobalSheet, &*ComponentSheet}, Diagnostics);

        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_TRUE(Style.TextColor->R == 0xFF); // component layer wins over global, same as Resolver::Resolve()
    });

    IT("global.lustre can still be the source of an inherited value across a component boundary", {
        // Mirrors ResolverTests.cpp's "lets global.lustre reach across component
        // boundaries", but for inheritance rather than selector matching: an
        // inherited *value* crossing a component root is expected (§1.7), unlike a
        // descendant *selector* crossing one (§1.2), which stays blocked.
        const auto Sheet = ParseOrFail(".app-root { color: #ABCDEF; }", "global.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement AppRoot("app-root", "Frame", nullptr, /*ComponentRoot=*/true);
        // ComponentRoot for CardRoot too -- from Label's own resolve, CardRoot is a
        // different component's root, exactly like HealthBarRoot in ResolverTests.cpp.
        FakeElement CardRoot("card", "Frame", &AppRoot, /*ComponentRoot=*/true);
        FakeElement Label("", "Text", &CardRoot);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Label, StylesheetSet{&*Sheet, nullptr}, Diagnostics);

        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_TRUE(Style.TextColor->R == 0xAB && Style.TextColor->G == 0xCD && Style.TextColor->B == 0xEF);
    });
});
