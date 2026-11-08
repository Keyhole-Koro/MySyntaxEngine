# MySyntaxEngine

A grammar-driven canonical LR(1) syntax engine for editor tooling. It has no
language built in: a grammar file supplies the rules, and annotations in the
grammar say how to classify tokens and which declarations make up an outline.

Given one grammar file, or several layered with `%extend`, the engine builds
the LR(1) table (which it can cache in a file) and parses the token-id stream
a caller's lexer produces. A parse reports the token where the input stops
fitting the grammar and the terminals expected there, or that the input ended
early. As it reduces, it applies the grammar's annotations: `@role` labels the
first token of a symbol, and `@decl(kind)` records a declaration outside any
`%scope` nesting as an outline symbol. Callers can also build their own values
from reductions, with source spans (see `docs/semantic-actions.md`).

It is used today by:

- MyLangCompiler's `mylang-syntax-check` tool (`tools/syntax_check.c`), which
  compiles this repository's `src/lr1` sources in (`make syntax-check`). It
  lexes MyLang with the compiler's lexer, parses with
  `tests/fixtures/grammars/mylang_lsp.grammar`, and prints the diagnostics,
  token roles and outline symbols of each file as JSON.
- The MyLang language server (`tools/MyLangServerProtocol` in MyComputer),
  which keeps that tool running in `--stdio` mode with `mlx.grammar` layered on
  for MLX UI tags, and serves its output as syntax diagnostics, semantic tokens
  and document symbols.

The compiler does not parse with this engine; it keeps its own
recursive-descent parser and AST.

## Layout

```text
.
├── inc/syntax_engine/
│   ├── syntax_engine.h          # Syntax-only public API
│   └── semantic_actions.h       # AST-building and source-span API
├── src/
│   ├── cli/main.c               # Debug/demo CLI
│   └── lr1/                     # Table builder and parser runtime
└── tests/fixtures/grammars/     # Sample grammars
```

## Build

```bash
make
```

## CLI

```bash
./build/bin/output tests/fixtures/grammars/sample2.txt int id '=' id ';'
```

Use `--table` to print LR(1) item sets:

```bash
./build/bin/output --table tests/fixtures/grammars/sample2.txt
```

## Semantic actions

Include `syntax_engine/semantic_actions.h` and call
`syntax_parse_with_actions()` to build caller-defined AST values during LR
reductions. See `docs/semantic-actions.md` for source-span and ownership rules.

## MyLang generic tokens

The bundled MyLang grammar accepts generic struct and function declarations,
generic types, and generic calls. A caller must classify generic angle brackets
as `GENERIC_LT` and `GENERIC_GT`; ordinary `LT` and `GT` remain relational
operators. This keeps `max<i32>(a, b)` distinct from `a < b` without adding
language-specific name resolution to the syntax engine.

When a lexer combines adjacent generic closers into `RSH`, the caller must split
that token into two `GENERIC_GT` terminals before parsing. An `RSH` outside a
generic span remains the right-shift operator.
