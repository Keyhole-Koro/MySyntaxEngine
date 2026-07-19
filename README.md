# MyLangSyntaxEngine

Canonical LR(1) syntax engine for MyLang grammar experiments.

The engine can load a grammar file, build an LR(1) parse table, parse token-name
or token-id streams, report expected tokens, and parse caller-provided semantic
values with reduction callbacks and source spans.

## Layout

```text
.
├── inc/syntax_engine/
│   ├── syntax_engine.h          # Syntax-only public API
│   └── semantic_actions.h       # AST-building and source-span API
├── src/
│   ├── cli/main.c               # Debug/demo CLI
│   ├── grammar/                 # Future grammar loader split
│   ├── lr1/                     # Table builder and parser runtime
│   ├── runtime/                 # Future runtime split
│   └── support/                 # Shared helpers
├── tests/fixtures/grammars/     # Sample grammars
└── legacy/old_lr1_table/        # Previous prototype, not built by default
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
