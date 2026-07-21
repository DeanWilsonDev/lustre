-- Treesitter syntax highlighting for .lustre via the "css" grammar
-- (docs/lustre_lsp_decision.md). No .lustre-specific tree-sitter grammar exists -- and,
-- unlike .iris (which needed the JSX-flavored render{} block carved out separately),
-- none is needed: Lustre's grammar (docs/lustre_core_spec.md §1) is a strict subset of
-- CSS's own syntax -- class/pseudo-class selectors, nested rules, `--variable`
-- declarations, `var(--name)` references, hex colors, numbers with unit suffixes, and
-- even `scale(1.5)`-shaped function calls all parse under tree-sitter-css with zero
-- ERROR nodes (verified against every construct in the core spec's worked examples).
-- Primitive-element selectors (`frame { }`) parse as plain CSS type selectors, the same
-- way `div { }` would in real CSS.
--
-- Requires the `css` parser installed (:TSInstall css, or add "css" to nvim-treesitter's
-- own ensure_installed list if you manage parsers that way).

vim.treesitter.language.register("css", "lustre")

vim.api.nvim_create_autocmd("FileType", {
  pattern = "lustre",
  callback = function(args)
    pcall(vim.treesitter.start, args.buf, "css")
  end,
})
