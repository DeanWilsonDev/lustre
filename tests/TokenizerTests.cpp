#include "cimmerian/test.hpp"

#include "Lustre/Tokenizer.h"

using namespace Lustre;

DESCRIBE("Tokenizer", {
    IT("lexes a class selector and a declaration", {
        Tokenizer Tok(".card { padding: 16px; }", "test.lustre");

        const Token T1 = Tok.NextToken();
        ASSERT_TRUE(T1.Kind == TokenKind::ClassName && T1.Text == "card"); // class selector without leading dot

        const Token T2 = Tok.NextToken();
        ASSERT_TRUE(T2.Kind == TokenKind::OpenBrace);

        const Token T3 = Tok.NextToken();
        ASSERT_TRUE(T3.Kind == TokenKind::Identifier && T3.Text == "padding");

        const Token T4 = Tok.NextToken();
        ASSERT_TRUE(T4.Kind == TokenKind::Colon);

        const Token T5 = Tok.NextToken();
        ASSERT_TRUE(T5.Kind == TokenKind::Number && T5.Text == "16px"); // unit suffix stays attached

        const Token T6 = Tok.NextToken();
        ASSERT_TRUE(T6.Kind == TokenKind::Semicolon);

        const Token T7 = Tok.NextToken();
        ASSERT_TRUE(T7.Kind == TokenKind::CloseBrace);

        const Token T8 = Tok.NextToken();
        ASSERT_TRUE(T8.Kind == TokenKind::EndOfFile);
    });

    IT("lexes variable declarations and var() references", {
        Tokenizer Tok("--color-primary: #E8593C; var(--color-primary)", "test.lustre");

        const Token T1 = Tok.NextToken();
        ASSERT_TRUE(T1.Kind == TokenKind::VariableName && T1.Text == "color-primary");

        Tok.NextToken(); // colon
        const Token T3 = Tok.NextToken();
        ASSERT_TRUE(T3.Kind == TokenKind::HexColor && T3.Text == "#E8593C");

        Tok.NextToken(); // semicolon
        const Token T5 = Tok.NextToken();
        ASSERT_TRUE(T5.Kind == TokenKind::VarKeyword);

        Tok.NextToken(); // open paren
        const Token T7 = Tok.NextToken();
        ASSERT_TRUE(T7.Kind == TokenKind::VariableName && T7.Text == "color-primary");
    });

    IT("lexes pseudo-classes and :root", {
        Tokenizer Tok(":hover :root", "test.lustre");

        const Token T1 = Tok.NextToken();
        ASSERT_TRUE(T1.Kind == TokenKind::PseudoClass && T1.Text == "hover");

        const Token T2 = Tok.NextToken();
        ASSERT_TRUE(T2.Kind == TokenKind::PseudoClass && T2.Text == "root");
        // `:root` lexes as a pseudo-class-shaped token; the parser gives it
        // special meaning at the top level (docs/lustre_core_spec.md §1.4).
    });

    IT("skips block comments but not their content", {
        Tokenizer Tok("/* a comment { with braces } */ .card { }", "test.lustre");

        const Token T1 = Tok.NextToken();
        ASSERT_TRUE(T1.Kind == TokenKind::ClassName && T1.Text == "card"); // comment fully skipped
    });

    IT("lexes string literals", {
        Tokenizer Tok("\"assets/fonts/body.ttf\"", "test.lustre");

        const Token T1 = Tok.NextToken();
        ASSERT_TRUE(T1.Kind == TokenKind::StringLiteral && T1.Text == "assets/fonts/body.ttf");
    });
});
