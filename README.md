# MyLangSyntaxEngine

Canonical LR(1) syntax engine for MyLang grammar experiments.

The current implementation can load a grammar file, build an LR(1) parse table,
parse a token-name stream, and report expected tokens on syntax errors or
incomplete input.

## Layout

```text
.
├── include/mylang_syntax_engine/
│   └── syntax_engine.h          # Public API
├── src/
│   ├── cli/main.c               # Debug/demo CLI
│   ├── grammar/                 # Future grammar loader split
│   ├── lr1/syntax_engine.c      # LR(1) table builder and parser driver
│   ├── runtime/                 # Future parse runtime split
│   └── support/                 # Future shared helpers
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
