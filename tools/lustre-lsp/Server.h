#pragma once

#include "Lustre/Ast.h"
#include "Lustre/Parser.h"

#include <amanuensis.hpp>

#include <cstdio>
#include <string>
#include <unordered_map>

namespace LustreLsp {

// The whole server: one process, one client connection over stdio, request dispatch for
// the handful of LSP methods this tool implements (docs/lustre_lsp_decision.md). Unlike
// iris-lsp, there is no host-language proxy and no VirtualDocument position-translation
// step — `.lustre` is a self-contained grammar with its own real Tokenizer/Parser
// (Lustre::Parser), so every open document is just reparsed in full on each change.
// Lustre files are small stylesheets, so re-parsing on every keystroke is not worth
// debouncing for v1 (same call iris-lsp made for its own Iris::CompileFile step).
//
// Single-threaded: there is no background proxy thread here, so — unlike iris-lsp —
// no mutex is needed around Documents_; Run()'s read loop is the only thing that ever
// touches it.
class Server {
public:
    // Blocks reading JsonRpc messages from In until "exit" or EOF.
    void Run(std::FILE* In, std::FILE* Out);

private:
    struct OpenDocument {
        std::string        Text;
        Lustre::ParseResult Parsed;
    };

    void HandleMessage(const Amanuensis::Value& Message);
    void Reply(const Amanuensis::Value& Id, Amanuensis::Value Result);
    void Notify(const std::string& Method, Amanuensis::Value Params);

    void HandleInitialize(const Amanuensis::Value& Id, const Amanuensis::Value& Params);
    void HandleDidOpen(const Amanuensis::Value& Params);
    void HandleDidChange(const Amanuensis::Value& Params);
    void HandleDidClose(const Amanuensis::Value& Params);
    void HandleCompletion(const Amanuensis::Value& Id, const Amanuensis::Value& Params);
    void HandleDefinition(const Amanuensis::Value& Id, const Amanuensis::Value& Params);

    // Reparses Documents_[Uri] from Text and publishes fresh diagnostics — the one path
    // both didOpen and didChange funnel through.
    void RebuildDocument(const std::string& Uri, std::string Text);
    void PublishDiagnostics(const std::string& Uri, const OpenDocument& Doc);

    std::FILE* Out_{nullptr};

    std::unordered_map<std::string, OpenDocument> Documents_;
};

} // namespace LustreLsp
