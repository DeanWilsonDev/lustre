-- Minimal Neovim wiring for lustre-lsp (docs/lustre_lsp_decision.md). Source this from
-- init.lua, or copy it into an existing config -- there's no plugin to install, just
-- Neovim's own built-in LSP client (vim.lsp.start), same as any stdio LSP server.
--
-- Requires `lustre_lsp` on PATH (built via `cmake --build build --target lustre_lsp` in
-- this repo). Unlike iris-lsp, there's no host-language server dependency to degrade
-- around -- `.lustre` is a self-contained grammar, so completion/goto-definition/
-- diagnostics all come straight from this one process.
--
-- Syntax highlighting is separate -- see lustre-treesitter.lua alongside this file.

vim.filetype.add({ extension = { lustre = "lustre" } })

vim.api.nvim_create_autocmd("FileType", {
  pattern = "lustre",
  callback = function(args)
    vim.lsp.start({
      name = "lustre-lsp",
      cmd = { "lustre_lsp" },
      root_dir = vim.fs.root(args.buf, { "global.lustre", ".git" }) or vim.fs.dirname(args.buf),
    })
  end,
})
