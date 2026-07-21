#include "Server.h"

#include "CompletionContext.h"
#include "JsonRpc.h"
#include "Lustre/Parser.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace LustreLsp {

namespace {

std::string UriToPath(const std::string& Uri) {
    constexpr std::string_view Prefix = "file://";
    return Uri.substr(0, Prefix.size()) == Prefix ? Uri.substr(Prefix.size()) : Uri;
}

std::string PathToUri(const std::string& Path) { return "file://" + Path; }

std::optional<std::string> ReadFile(const std::filesystem::path& Path) {
    std::ifstream Stream(Path, std::ios::binary);
    if (!Stream) {
        return std::nullopt;
    }
    std::ostringstream Buffer;
    Buffer << Stream.rdbuf();
    return Buffer.str();
}

Amanuensis::Value MakePosition(std::uint32_t Line, std::uint32_t Column) {
    Amanuensis::Value Position = Amanuensis::Value::MakeObject();
    Position.Insert("line", Amanuensis::Value(static_cast<long long>(Line - 1)));
    Position.Insert("character", Amanuensis::Value(static_cast<long long>(Column - 1)));
    return Position;
}

Amanuensis::Value MakeRange(std::uint32_t Line, std::uint32_t Column) {
    Amanuensis::Value Range = Amanuensis::Value::MakeObject();
    Range.Insert("start", MakePosition(Line, Column));
    Range.Insert("end", MakePosition(Line, Column));
    return Range;
}

std::pair<std::uint32_t, std::uint32_t> PositionFromParams(const Amanuensis::Value& Position) {
    return {static_cast<std::uint32_t>(Position.Get("line").AsInteger()) + 1,
            static_cast<std::uint32_t>(Position.Get("character").AsInteger()) + 1};
}

Amanuensis::Value MakeCompletionItem(const std::string& Label, int Kind, const std::string& Detail = "") {
    Amanuensis::Value Item = Amanuensis::Value::MakeObject();
    Item.Insert("label", Amanuensis::Value(Label));
    Item.Insert("kind", Amanuensis::Value(Kind));
    if (!Detail.empty()) {
        Item.Insert("detail", Amanuensis::Value(Detail));
    }
    return Item;
}

// LSP CompletionItemKind values used below (microsoft/language-server-protocol).
constexpr int kCompletionKindProperty = 10;
constexpr int kCompletionKindClass = 7;
constexpr int kCompletionKindKeyword = 14;
constexpr int kCompletionKindVariable = 6;

// A stylesheet's own sibling `global.lustre`, if it exists and isn't the file itself —
// the other half of §1.3's two-layer cascade. Read fresh each time rather than cached:
// completion/goto-definition are not latency-sensitive enough here to justify tracking
// global.lustre as its own open-or-watched document.
std::optional<Lustre::Stylesheet> LoadSiblingGlobalSheet(const std::string& Path) {
    const std::filesystem::path GlobalPath = std::filesystem::path(Path).parent_path() / "global.lustre";
    if (std::filesystem::path(Path).filename() == "global.lustre") {
        return std::nullopt;
    }
    const auto Text = ReadFile(GlobalPath);
    if (!Text) {
        return std::nullopt;
    }
    Lustre::Parser     P(*Text, GlobalPath.string());
    Lustre::ParseResult Result = P.Parse();
    return std::move(Result.Sheet);
}

} // namespace

void Server::Run(std::FILE* In, std::FILE* Out) {
    Out_ = Out;
    for (;;) {
        const std::optional<Amanuensis::Value> Message = JsonRpc::ReadMessage(In);
        if (!Message) {
            return;
        }
        const std::string Method =
            Message->IsObject() && Message->Contains("method") ? Message->Get("method").AsString() : std::string{};
        HandleMessage(*Message);
        if (Method == "exit") {
            return;
        }
    }
}

void Server::Reply(const Amanuensis::Value& Id, Amanuensis::Value Result) {
    Amanuensis::Value Message = Amanuensis::Value::MakeObject();
    Message.Insert("jsonrpc", Amanuensis::Value("2.0"));
    Message.Insert("id", Id);
    Message.Insert("result", std::move(Result));
    JsonRpc::WriteMessage(Out_, Message);
}

void Server::Notify(const std::string& Method, Amanuensis::Value Params) {
    Amanuensis::Value Message = Amanuensis::Value::MakeObject();
    Message.Insert("jsonrpc", Amanuensis::Value("2.0"));
    Message.Insert("method", Amanuensis::Value(Method));
    Message.Insert("params", std::move(Params));
    JsonRpc::WriteMessage(Out_, Message);
}

void Server::HandleMessage(const Amanuensis::Value& Message) {
    if (!Message.IsObject() || !Message.Contains("method")) {
        return;
    }
    const std::string       Method = Message.Get("method").AsString();
    const Amanuensis::Value Params = Message.Contains("params") ? Message.Get("params") : Amanuensis::Value();
    const bool               IsRequest = Message.Contains("id");
    const Amanuensis::Value  Id = IsRequest ? Message.Get("id") : Amanuensis::Value();

    if (Method == "initialize") {
        HandleInitialize(Id, Params);
    } else if (Method == "initialized" || Method == "$/setTrace" || Method == "workspace/didChangeConfiguration") {
        // Accepted, no action needed.
    } else if (Method == "shutdown") {
        Reply(Id, Amanuensis::Value());
    } else if (Method == "exit") {
        // Handled by Run()'s own loop after this returns.
    } else if (Method == "textDocument/didOpen") {
        HandleDidOpen(Params);
    } else if (Method == "textDocument/didChange") {
        HandleDidChange(Params);
    } else if (Method == "textDocument/didClose") {
        HandleDidClose(Params);
    } else if (Method == "textDocument/completion") {
        HandleCompletion(Id, Params);
    } else if (Method == "textDocument/definition") {
        HandleDefinition(Id, Params);
    } else if (IsRequest) {
        Amanuensis::Value Error = Amanuensis::Value::MakeObject();
        Error.Insert("code", Amanuensis::Value(-32601));
        Error.Insert("message", Amanuensis::Value("method not found: " + Method));
        Amanuensis::Value Response = Amanuensis::Value::MakeObject();
        Response.Insert("jsonrpc", Amanuensis::Value("2.0"));
        Response.Insert("id", Id);
        Response.Insert("error", std::move(Error));
        JsonRpc::WriteMessage(Out_, Response);
    }
    // An unknown notification (no id) is silently ignored, per LSP's own "must not
    // fail" guidance for messages a server doesn't recognise.
}

void Server::HandleInitialize(const Amanuensis::Value& Id, const Amanuensis::Value& /*Params*/) {
    Amanuensis::Value Completion = Amanuensis::Value::MakeObject();
    Amanuensis::Value TriggerChars = Amanuensis::Value::MakeArray();
    TriggerChars.PushBack(Amanuensis::Value(":"));
    TriggerChars.PushBack(Amanuensis::Value("("));
    TriggerChars.PushBack(Amanuensis::Value(" "));
    TriggerChars.PushBack(Amanuensis::Value("."));
    Completion.Insert("triggerCharacters", std::move(TriggerChars));

    Amanuensis::Value Capabilities = Amanuensis::Value::MakeObject();
    Capabilities.Insert("textDocumentSync", Amanuensis::Value(1)); // Full
    Capabilities.Insert("completionProvider", std::move(Completion));
    Capabilities.Insert("definitionProvider", Amanuensis::Value(true));

    Amanuensis::Value ServerInfo = Amanuensis::Value::MakeObject();
    ServerInfo.Insert("name", Amanuensis::Value("lustre-lsp"));
    ServerInfo.Insert("version", Amanuensis::Value("0.1.0"));

    Amanuensis::Value Result = Amanuensis::Value::MakeObject();
    Result.Insert("capabilities", std::move(Capabilities));
    Result.Insert("serverInfo", std::move(ServerInfo));
    Reply(Id, std::move(Result));
}

void Server::HandleDidOpen(const Amanuensis::Value& Params) {
    const Amanuensis::Value& TextDocument = Params.Get("textDocument");
    RebuildDocument(TextDocument.Get("uri").AsString(), TextDocument.Get("text").AsString());
}

void Server::HandleDidChange(const Amanuensis::Value& Params) {
    const Amanuensis::Value& TextDocument = Params.Get("textDocument");
    const Amanuensis::Value& Changes = Params.Get("contentChanges");
    if (Changes.Size() == 0) {
        return;
    }
    // Full-document sync only (textDocumentSync=1 in our own capabilities) -- the last
    // entry always carries the complete new text.
    RebuildDocument(TextDocument.Get("uri").AsString(), Changes.At(Changes.Size() - 1).Get("text").AsString());
}

void Server::HandleDidClose(const Amanuensis::Value& Params) {
    Documents_.erase(Params.Get("textDocument").Get("uri").AsString());
}

void Server::RebuildDocument(const std::string& Uri, std::string Text) {
    const std::string Path = UriToPath(Uri);

    OpenDocument& Doc = Documents_[Uri];
    Doc.Text = std::move(Text);

    Lustre::Parser P(Doc.Text, Path);
    Doc.Parsed = P.Parse();

    PublishDiagnostics(Uri, Doc);
}

void Server::PublishDiagnostics(const std::string& Uri, const OpenDocument& Doc) {
    Amanuensis::Value Diagnostics = Amanuensis::Value::MakeArray();
    for (const Lustre::ParseError& Err : Doc.Parsed.Errors) {
        Amanuensis::Value Diagnostic = Amanuensis::Value::MakeObject();
        Diagnostic.Insert("range", MakeRange(Err.Location.Line, Err.Location.Column));
        Diagnostic.Insert("severity", Amanuensis::Value(1)); // Error
        Diagnostic.Insert("source", Amanuensis::Value("lustre"));
        Diagnostic.Insert("message", Amanuensis::Value(Err.Message));
        Diagnostics.PushBack(std::move(Diagnostic));
    }

    Amanuensis::Value Params = Amanuensis::Value::MakeObject();
    Params.Insert("uri", Amanuensis::Value(Uri));
    Params.Insert("diagnostics", std::move(Diagnostics));
    Notify("textDocument/publishDiagnostics", std::move(Params));
}

void Server::HandleCompletion(const Amanuensis::Value& Id, const Amanuensis::Value& Params) {
    const std::string Uri = Params.Get("textDocument").Get("uri").AsString();
    const auto [Line, Column] = PositionFromParams(Params.Get("position"));

    Amanuensis::Value Items = Amanuensis::Value::MakeArray();
    const auto DocIt = Documents_.find(Uri);
    if (DocIt != Documents_.end()) {
        const OpenDocument&     Doc = DocIt->second;
        const CompletionContext Context = ClassifyCompletionContext(Doc.Text, Line, Column);

        if (Context.Kind == CompletionContextKind::VarRef) {
            for (const auto* V : CollectInScopeVariables(*Doc.Parsed.Sheet)) {
                Items.PushBack(MakeCompletionItem(V->Name, kCompletionKindVariable));
            }
            if (const auto Global = LoadSiblingGlobalSheet(UriToPath(Uri))) {
                for (const auto* V : CollectInScopeVariables(*Global)) {
                    Items.PushBack(MakeCompletionItem(V->Name, kCompletionKindVariable));
                }
            }
        } else if (Context.Kind == CompletionContextKind::Value) {
            for (std::string_view Keyword : PropertyValueKeywords(Context.Property)) {
                Items.PushBack(MakeCompletionItem(std::string(Keyword), kCompletionKindKeyword));
            }
        } else { // Statement
            for (std::string_view Name : kPrimitiveSelectorNames) {
                Items.PushBack(MakeCompletionItem(std::string(Name), kCompletionKindClass, "primitive selector"));
            }
            for (std::string_view Name : kPseudoClassNames) {
                Items.PushBack(MakeCompletionItem(std::string(Name), kCompletionKindKeyword, "pseudo-class"));
            }
            if (Context.Depth > 0) {
                for (std::string_view Name : kPropertyNames) {
                    Items.PushBack(MakeCompletionItem(std::string(Name), kCompletionKindProperty));
                }
            }
        }
    }

    Amanuensis::Value Result = Amanuensis::Value::MakeObject();
    Result.Insert("isIncomplete", Amanuensis::Value(false));
    Result.Insert("items", std::move(Items));
    Reply(Id, std::move(Result));
}

void Server::HandleDefinition(const Amanuensis::Value& Id, const Amanuensis::Value& Params) {
    const std::string Uri = Params.Get("textDocument").Get("uri").AsString();
    const auto [Line, Column] = PositionFromParams(Params.Get("position"));

    const auto DocIt = Documents_.find(Uri);
    if (DocIt == Documents_.end()) {
        Reply(Id, Amanuensis::Value());
        return;
    }
    const OpenDocument& Doc = DocIt->second;

    const auto Token = TokenAtPosition(Doc.Text, Line, Column);
    if (!Token || Token->Kind != Lustre::TokenKind::VariableName) {
        Reply(Id, Amanuensis::Value());
        return;
    }

    // Search this file's own in-scope variables first, then the sibling
    // global.lustre's (docs/lustre_core_spec.md §1.3's cascade order, applied to
    // lookup rather than value resolution).
    for (const auto* V : CollectInScopeVariables(*Doc.Parsed.Sheet)) {
        if (V->Name == Token->Text) {
            Amanuensis::Value Location = Amanuensis::Value::MakeObject();
            Location.Insert("uri", Amanuensis::Value(Uri));
            Location.Insert("range", MakeRange(V->Location.Line, V->Location.Column));
            Reply(Id, std::move(Location));
            return;
        }
    }

    const std::string Path = UriToPath(Uri);
    if (const auto Global = LoadSiblingGlobalSheet(Path)) {
        for (const auto* V : CollectInScopeVariables(*Global)) {
            if (V->Name == Token->Text) {
                const std::filesystem::path GlobalPath = std::filesystem::path(Path).parent_path() / "global.lustre";
                Amanuensis::Value Location = Amanuensis::Value::MakeObject();
                Location.Insert("uri", Amanuensis::Value(PathToUri(GlobalPath.string())));
                Location.Insert("range", MakeRange(V->Location.Line, V->Location.Column));
                Reply(Id, std::move(Location));
                return;
            }
        }
    }

    Reply(Id, Amanuensis::Value());
}

} // namespace LustreLsp
