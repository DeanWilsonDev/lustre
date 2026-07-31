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
    Amanuensis::Value Position = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Position, "line", Amanuensis::Value(static_cast<long long>(Line - 1)));
    Amanuensis::Json::Insert(Position, "character", Amanuensis::Value(static_cast<long long>(Column - 1)));
    return Position;
}

Amanuensis::Value MakeRange(std::uint32_t Line, std::uint32_t Column) {
    Amanuensis::Value Range = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Range, "start", MakePosition(Line, Column));
    Amanuensis::Json::Insert(Range, "end", MakePosition(Line, Column));
    return Range;
}

std::pair<std::uint32_t, std::uint32_t> PositionFromParams(const Amanuensis::Value& Position) {
    return {static_cast<std::uint32_t>(Amanuensis::Json::AsInteger(Amanuensis::Json::Get(Position, "line"))) + 1,
            static_cast<std::uint32_t>(Amanuensis::Json::AsInteger(Amanuensis::Json::Get(Position, "character"))) +
                1};
}

Amanuensis::Value MakeCompletionItem(const std::string& Label, int Kind, const std::string& Detail = "") {
    Amanuensis::Value Item = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Item, "label", Amanuensis::Value(Label));
    Amanuensis::Json::Insert(Item, "kind", Amanuensis::Value(static_cast<long long>(Kind)));
    if (!Detail.empty()) {
        Amanuensis::Json::Insert(Item, "detail", Amanuensis::Value(Detail));
    }
    return Item;
}

// LSP CompletionItemKind values used below (microsoft/language-server-protocol).
constexpr int kCompletionKindProperty = 10;
constexpr int kCompletionKindClass = 7;
constexpr int kCompletionKindKeyword = 14;
constexpr int kCompletionKindVariable = 6;

// A stylesheet's own sibling `global.lustre`, if it exists and isn't the file itself --
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
        const std::string Method = Amanuensis::Json::IsObject(*Message) && Amanuensis::Json::Contains(*Message, "method")
                                        ? Amanuensis::Json::AsString(Amanuensis::Json::Get(*Message, "method"))
                                        : std::string{};
        HandleMessage(*Message);
        if (Method == "exit") {
            return;
        }
    }
}

void Server::Reply(const Amanuensis::Value& Id, Amanuensis::Value Result) {
    Amanuensis::Value Message = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Message, "jsonrpc", Amanuensis::Value("2.0"));
    Amanuensis::Json::Insert(Message, "id", Id);
    Amanuensis::Json::Insert(Message, "result", std::move(Result));
    JsonRpc::WriteMessage(Out_, Message);
}

void Server::Notify(const std::string& Method, Amanuensis::Value Params) {
    Amanuensis::Value Message = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Message, "jsonrpc", Amanuensis::Value("2.0"));
    Amanuensis::Json::Insert(Message, "method", Amanuensis::Value(Method));
    Amanuensis::Json::Insert(Message, "params", std::move(Params));
    JsonRpc::WriteMessage(Out_, Message);
}

void Server::HandleMessage(const Amanuensis::Value& Message) {
    if (!Amanuensis::Json::IsObject(Message) || !Amanuensis::Json::Contains(Message, "method")) {
        return;
    }
    const std::string       Method = Amanuensis::Json::AsString(Amanuensis::Json::Get(Message, "method"));
    const Amanuensis::Value Params =
        Amanuensis::Json::Contains(Message, "params") ? Amanuensis::Json::Get(Message, "params") : Amanuensis::Value();
    const bool               IsRequest = Amanuensis::Json::Contains(Message, "id");
    const Amanuensis::Value  Id = IsRequest ? Amanuensis::Json::Get(Message, "id") : Amanuensis::Value();

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
        Amanuensis::Value Error = Amanuensis::Json::MakeObject();
        Amanuensis::Json::Insert(Error, "code", Amanuensis::Value(static_cast<long long>(-32601)));
        Amanuensis::Json::Insert(Error, "message", Amanuensis::Value("method not found: " + Method));
        Amanuensis::Value Response = Amanuensis::Json::MakeObject();
        Amanuensis::Json::Insert(Response, "jsonrpc", Amanuensis::Value("2.0"));
        Amanuensis::Json::Insert(Response, "id", Id);
        Amanuensis::Json::Insert(Response, "error", std::move(Error));
        JsonRpc::WriteMessage(Out_, Response);
    }
    // An unknown notification (no id) is silently ignored, per LSP's own "must not
    // fail" guidance for messages a server doesn't recognise.
}

void Server::HandleInitialize(const Amanuensis::Value& Id, const Amanuensis::Value& /*Params*/) {
    Amanuensis::Value Completion = Amanuensis::Json::MakeObject();
    Amanuensis::Value TriggerChars = Amanuensis::Json::MakeArray();
    Amanuensis::Json::PushBack(TriggerChars, Amanuensis::Value(":"));
    Amanuensis::Json::PushBack(TriggerChars, Amanuensis::Value("("));
    Amanuensis::Json::PushBack(TriggerChars, Amanuensis::Value(" "));
    Amanuensis::Json::PushBack(TriggerChars, Amanuensis::Value("."));
    Amanuensis::Json::Insert(Completion, "triggerCharacters", std::move(TriggerChars));

    Amanuensis::Value Capabilities = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Capabilities, "textDocumentSync", Amanuensis::Value(static_cast<long long>(1))); // Full
    Amanuensis::Json::Insert(Capabilities, "completionProvider", std::move(Completion));
    Amanuensis::Json::Insert(Capabilities, "definitionProvider", Amanuensis::Value(true));

    Amanuensis::Value ServerInfo = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(ServerInfo, "name", Amanuensis::Value("lustre-lsp"));
    Amanuensis::Json::Insert(ServerInfo, "version", Amanuensis::Value("0.1.0"));

    Amanuensis::Value Result = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Result, "capabilities", std::move(Capabilities));
    Amanuensis::Json::Insert(Result, "serverInfo", std::move(ServerInfo));
    Reply(Id, std::move(Result));
}

void Server::HandleDidOpen(const Amanuensis::Value& Params) {
    const Amanuensis::Value& TextDocument = Amanuensis::Json::Get(Params, "textDocument");
    RebuildDocument(Amanuensis::Json::AsString(Amanuensis::Json::Get(TextDocument, "uri")),
                     Amanuensis::Json::AsString(Amanuensis::Json::Get(TextDocument, "text")));
}

void Server::HandleDidChange(const Amanuensis::Value& Params) {
    const Amanuensis::Value& TextDocument = Amanuensis::Json::Get(Params, "textDocument");
    const Amanuensis::Value& Changes = Amanuensis::Json::Get(Params, "contentChanges");
    if (Amanuensis::Json::Size(Changes) == 0) {
        return;
    }
    // Full-document sync only (textDocumentSync=1 in our own capabilities) -- the last
    // entry always carries the complete new text.
    RebuildDocument(Amanuensis::Json::AsString(Amanuensis::Json::Get(TextDocument, "uri")),
                     Amanuensis::Json::AsString(Amanuensis::Json::Get(
                         Amanuensis::Json::At(Changes, Amanuensis::Json::Size(Changes) - 1), "text")));
}

void Server::HandleDidClose(const Amanuensis::Value& Params) {
    Documents_.erase(
        Amanuensis::Json::AsString(Amanuensis::Json::Get(Amanuensis::Json::Get(Params, "textDocument"), "uri")));
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
    Amanuensis::Value Diagnostics = Amanuensis::Json::MakeArray();
    for (const Lustre::ParseError& Err : Doc.Parsed.Errors) {
        Amanuensis::Value Diagnostic = Amanuensis::Json::MakeObject();
        Amanuensis::Json::Insert(Diagnostic, "range", MakeRange(Err.Location.Line, Err.Location.Column));
        Amanuensis::Json::Insert(Diagnostic, "severity", Amanuensis::Value(static_cast<long long>(1))); // Error
        Amanuensis::Json::Insert(Diagnostic, "source", Amanuensis::Value("lustre"));
        Amanuensis::Json::Insert(Diagnostic, "message", Amanuensis::Value(Err.Message));
        Amanuensis::Json::PushBack(Diagnostics, std::move(Diagnostic));
    }

    Amanuensis::Value Params = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Params, "uri", Amanuensis::Value(Uri));
    Amanuensis::Json::Insert(Params, "diagnostics", std::move(Diagnostics));
    Notify("textDocument/publishDiagnostics", std::move(Params));
}

void Server::HandleCompletion(const Amanuensis::Value& Id, const Amanuensis::Value& Params) {
    const std::string Uri =
        Amanuensis::Json::AsString(Amanuensis::Json::Get(Amanuensis::Json::Get(Params, "textDocument"), "uri"));
    const auto [Line, Column] = PositionFromParams(Amanuensis::Json::Get(Params, "position"));

    Amanuensis::Value Items = Amanuensis::Json::MakeArray();
    const auto DocIt = Documents_.find(Uri);
    if (DocIt != Documents_.end()) {
        const OpenDocument&     Doc = DocIt->second;
        const CompletionContext Context = ClassifyCompletionContext(Doc.Text, Line, Column);

        if (Context.Kind == CompletionContextKind::VarRef) {
            for (const auto* V : CollectInScopeVariables(*Doc.Parsed.Sheet)) {
                Amanuensis::Json::PushBack(Items, MakeCompletionItem(V->Name, kCompletionKindVariable));
            }
            if (const auto Global = LoadSiblingGlobalSheet(UriToPath(Uri))) {
                for (const auto* V : CollectInScopeVariables(*Global)) {
                    Amanuensis::Json::PushBack(Items, MakeCompletionItem(V->Name, kCompletionKindVariable));
                }
            }
        } else if (Context.Kind == CompletionContextKind::Value) {
            for (std::string_view Keyword : PropertyValueKeywords(Context.Property)) {
                Amanuensis::Json::PushBack(Items, MakeCompletionItem(std::string(Keyword), kCompletionKindKeyword));
            }
        } else { // Statement
            for (std::string_view Name : kPrimitiveSelectorNames) {
                Amanuensis::Json::PushBack(
                    Items, MakeCompletionItem(std::string(Name), kCompletionKindClass, "primitive selector"));
            }
            for (std::string_view Name : kPseudoClassNames) {
                Amanuensis::Json::PushBack(Items,
                                            MakeCompletionItem(std::string(Name), kCompletionKindKeyword, "pseudo-class"));
            }
            if (Context.Depth > 0) {
                for (std::string_view Name : kPropertyNames) {
                    Amanuensis::Json::PushBack(Items, MakeCompletionItem(std::string(Name), kCompletionKindProperty));
                }
            }
        }
    }

    Amanuensis::Value Result = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Result, "isIncomplete", Amanuensis::Value(false));
    Amanuensis::Json::Insert(Result, "items", std::move(Items));
    Reply(Id, std::move(Result));
}

void Server::HandleDefinition(const Amanuensis::Value& Id, const Amanuensis::Value& Params) {
    const std::string Uri =
        Amanuensis::Json::AsString(Amanuensis::Json::Get(Amanuensis::Json::Get(Params, "textDocument"), "uri"));
    const auto [Line, Column] = PositionFromParams(Amanuensis::Json::Get(Params, "position"));

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
            Amanuensis::Value Location = Amanuensis::Json::MakeObject();
            Amanuensis::Json::Insert(Location, "uri", Amanuensis::Value(Uri));
            Amanuensis::Json::Insert(Location, "range", MakeRange(V->Location.Line, V->Location.Column));
            Reply(Id, std::move(Location));
            return;
        }
    }

    const std::string Path = UriToPath(Uri);
    if (const auto Global = LoadSiblingGlobalSheet(Path)) {
        for (const auto* V : CollectInScopeVariables(*Global)) {
            if (V->Name == Token->Text) {
                const std::filesystem::path GlobalPath = std::filesystem::path(Path).parent_path() / "global.lustre";
                Amanuensis::Value Location = Amanuensis::Json::MakeObject();
                Amanuensis::Json::Insert(Location, "uri", Amanuensis::Value(PathToUri(GlobalPath.string())));
                Amanuensis::Json::Insert(Location, "range", MakeRange(V->Location.Line, V->Location.Column));
                Reply(Id, std::move(Location));
                return;
            }
        }
    }

    Reply(Id, Amanuensis::Value());
}

} // namespace LustreLsp
