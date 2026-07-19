#include "cimmerian/test.hpp"

#include "Lustre/Parser.h"

using namespace Lustre;

DESCRIBE("Parser", {
    IT("parses the HealthBar worked example (docs/lustre_core_spec.md §4)", {
        const std::string Source = R"(
.health-bar {
    --bar-background: #333333;

    width: 200px;
    height: 16px;
    background-color: var(--bar-background);
    border-radius: 8px;
}

.bar-normal {
    background-color: #4CAF50;
}

.bar-critical {
    background-color: #E8593C;
}
)";
        Parser TheParser(Source, "HealthBar.lustre");
        const ParseResult Result = TheParser.Parse();

        ASSERT_TRUE(Result.Errors.empty());
        REQUIRE_TRUE(Result.Sheet.has_value());
        REQUIRE_EQUAL(Result.Sheet->Rules.size(), static_cast<std::size_t>(3));

        const Rule& HealthBar = *Result.Sheet->Rules[0];
        ASSERT_TRUE(HealthBar.Kind == SelectorKind::Class && HealthBar.ClassName == "health-bar");
        ASSERT_EQUAL(HealthBar.Variables.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(HealthBar.Variables[0].Name == "bar-background");
        ASSERT_EQUAL(HealthBar.Declarations.size(), static_cast<std::size_t>(4));
    });

    IT("parses nested descendant selectors and a pseudo-class suffix", {
        const std::string Source = R"(
.card {
    padding: 16px;

    .card-title {
        font-size: 20px;
    }
}

.card:hover {
    .card-title {
        color: var(--color-text-emphasis);
    }
}
)";
        Parser TheParser(Source, "Card.lustre");
        const ParseResult Result = TheParser.Parse();

        ASSERT_TRUE(Result.Errors.empty());
        REQUIRE_TRUE(Result.Sheet.has_value());
        REQUIRE_EQUAL(Result.Sheet->Rules.size(), static_cast<std::size_t>(2));

        const Rule& Card = *Result.Sheet->Rules[0];
        ASSERT_FALSE(Card.Pseudo.has_value());
        REQUIRE_EQUAL(Card.NestedRules.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(Card.NestedRules[0]->ClassName == "card-title");
        ASSERT_TRUE(Card.NestedRules[0]->Depth == 1);

        const Rule& CardHover = *Result.Sheet->Rules[1];
        REQUIRE_TRUE(CardHover.Pseudo.has_value());
        ASSERT_TRUE(*CardHover.Pseudo == PseudoKind::Hover);
    });

    IT("parses a :root block", {
        const std::string Source = R"(
:root {
    --color-primary: #E8593C;
    --spacing-md: 16px;
}
)";
        Parser TheParser(Source, "global.lustre");
        const ParseResult Result = TheParser.Parse();

        ASSERT_TRUE(Result.Errors.empty());
        REQUIRE_TRUE(Result.Sheet.has_value());
        REQUIRE_TRUE(Result.Sheet->Root.has_value());
        ASSERT_EQUAL(Result.Sheet->Root->Variables.size(), static_cast<std::size_t>(2));
    });

    IT("rejects a compound tag+class selector", {
        Parser TheParser("Frame.card { padding: 16px; }", "test.lustre");
        const ParseResult Result = TheParser.Parse();

        ASSERT_FALSE(Result.Errors.empty());
        ASSERT_TRUE(Result.Errors[0].Message.find("Compound") != std::string::npos);
    });

    IT("rejects a duplicate selector in one file", {
        const std::string Source = R"(
.card { padding: 16px; }
.card { margin: 8px; }
)";
        Parser TheParser(Source, "test.lustre");
        const ParseResult Result = TheParser.Parse();

        ASSERT_FALSE(Result.Errors.empty());
        ASSERT_TRUE(Result.Errors[0].Message.find("Duplicate selector") != std::string::npos);
    });

    IT("parses a primitive selector and transform's scale(...) call value", {
        Parser TheParser("frame { transform: scale(1.5); }", "test.lustre");
        const ParseResult Result = TheParser.Parse();

        ASSERT_TRUE(Result.Errors.empty());
        REQUIRE_TRUE(Result.Sheet.has_value());
        REQUIRE_EQUAL(Result.Sheet->Rules.size(), static_cast<std::size_t>(1));
        const Rule& Frame = *Result.Sheet->Rules[0];
        ASSERT_TRUE(Frame.Kind == SelectorKind::Primitive && Frame.PrimitiveName == "frame");
        REQUIRE_EQUAL(Frame.Declarations.size(), static_cast<std::size_t>(1));
        REQUIRE_EQUAL(Frame.Declarations[0].Values.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(Frame.Declarations[0].Values[0].Literal == "scale");
        ASSERT_TRUE(Frame.Declarations[0].Values[0].CallArgument == "1.5");
    });
});
