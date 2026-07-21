#include "cimmerian/test.hpp"

#include "JsonRpc.h"
#include "Server.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace LustreLsp;

namespace {

// Feeds Requests (already-framed JSON-RPC messages) into a Server via a temp file
// standing in for stdin, capturing every message it writes to a second temp file
// standing in for stdout. Server::Run exits once it has processed every request
// (mirroring how a real client would end the session with "exit").
std::vector<Amanuensis::Value> RunServer(const std::vector<Amanuensis::Value>& Requests) {
    std::FILE* In = std::tmpfile();
    for (const auto& Req : Requests) {
        JsonRpc::WriteMessage(In, Req);
    }
    Amanuensis::Value Exit = Amanuensis::Value::MakeObject();
    Exit.Insert("jsonrpc", Amanuensis::Value("2.0"));
    Exit.Insert("method", Amanuensis::Value("exit"));
    JsonRpc::WriteMessage(In, Exit);
    std::rewind(In);

    std::FILE* Out = std::tmpfile();
    Server ThisServer;
    ThisServer.Run(In, Out);
    std::fclose(In);

    std::rewind(Out);
    std::vector<Amanuensis::Value> Responses;
    while (const auto Msg = JsonRpc::ReadMessage(Out)) {
        Responses.push_back(*Msg);
    }
    std::fclose(Out);
    return Responses;
}

Amanuensis::Value MakeDidOpen(const std::string& Uri, const std::string& Text) {
    Amanuensis::Value TextDocument = Amanuensis::Value::MakeObject();
    TextDocument.Insert("uri", Amanuensis::Value(Uri));
    TextDocument.Insert("text", Amanuensis::Value(Text));
    Amanuensis::Value Params = Amanuensis::Value::MakeObject();
    Params.Insert("textDocument", std::move(TextDocument));
    Amanuensis::Value Message = Amanuensis::Value::MakeObject();
    Message.Insert("jsonrpc", Amanuensis::Value("2.0"));
    Message.Insert("method", Amanuensis::Value("textDocument/didOpen"));
    Message.Insert("params", std::move(Params));
    return Message;
}

Amanuensis::Value MakeRequest(int Id, const std::string& Method, const std::string& Uri, std::uint32_t Line0,
                                std::uint32_t Character0) {
    Amanuensis::Value TextDocument = Amanuensis::Value::MakeObject();
    TextDocument.Insert("uri", Amanuensis::Value(Uri));
    Amanuensis::Value Position = Amanuensis::Value::MakeObject();
    Position.Insert("line", Amanuensis::Value(static_cast<long long>(Line0)));
    Position.Insert("character", Amanuensis::Value(static_cast<long long>(Character0)));
    Amanuensis::Value Params = Amanuensis::Value::MakeObject();
    Params.Insert("textDocument", std::move(TextDocument));
    Params.Insert("position", std::move(Position));
    Amanuensis::Value Message = Amanuensis::Value::MakeObject();
    Message.Insert("jsonrpc", Amanuensis::Value("2.0"));
    Message.Insert("id", Amanuensis::Value(Id));
    Message.Insert("method", Amanuensis::Value(Method));
    Message.Insert("params", std::move(Params));
    return Message;
}

} // namespace

DESCRIBE("Server", {
    IT("replies to initialize with completion and definition capabilities", {
        const auto Responses = RunServer({MakeRequest(1, "initialize", "", 0, 0)});
        REQUIRE_TRUE(!Responses.empty());
        const auto& Capabilities = Responses[0].Get("result").Get("capabilities");
        ASSERT_TRUE(Capabilities.Get("definitionProvider").AsBoolean());
        ASSERT_FALSE(Capabilities.Get("completionProvider").IsNull());
    });

    IT("publishes a diagnostic for a compound tag+class selector", {
        const std::string Uri = "file:///Bad.lustre";
        const auto Responses = RunServer({MakeDidOpen(Uri, "Frame.card {\n    padding: 8px;\n}\n")});

        bool Found = false;
        for (const auto& Msg : Responses) {
            if (Msg.Contains("method") && Msg.Get("method").AsString() == "textDocument/publishDiagnostics") {
                const auto& Diags = Msg.Get("params").Get("diagnostics");
                Found = Diags.Size() > 0;
            }
        }
        ASSERT_TRUE(Found);
    });

    IT("offers property names when completing inside a rule block", {
        const std::string Uri = "file:///Card.lustre";
        const auto Responses = RunServer({
            MakeDidOpen(Uri, ".card {\n    \n}\n"),
            MakeRequest(2, "textDocument/completion", Uri, 1, 4),
        });

        REQUIRE_TRUE(Responses.size() >= 2);
        const auto& Items = Responses.back().Get("result").Get("items");
        bool SawBackgroundColor = false;
        for (std::size_t I = 0; I < Items.Size(); ++I) {
            if (Items.At(I).Get("label").AsString() == "background-color") {
                SawBackgroundColor = true;
            }
        }
        ASSERT_TRUE(SawBackgroundColor);
    });

    IT("goto-definition on a var(--name) reference jumps to its declaration", {
        const std::string Uri = "file:///HealthBar.lustre";
        const std::string Text = ".health-bar {\n"
                                   "    --bar-background: #333333;\n"
                                   "\n"
                                   "    background-color: var(--bar-background);\n"
                                   "}\n";
        const auto Responses = RunServer({
            MakeDidOpen(Uri, Text),
            MakeRequest(3, "textDocument/definition", Uri, 3, 30), // inside "--bar-background" reference
        });

        REQUIRE_TRUE(Responses.size() >= 2);
        const auto& Result = Responses.back().Get("result");
        REQUIRE_TRUE(!Result.IsNull());
        ASSERT_EQUAL(static_cast<std::uint32_t>(Result.Get("range").Get("start").Get("line").AsInteger()),
                     static_cast<std::uint32_t>(1)); // 0-based line 1 == source line 2
    });
});
