# Semantic actions and source locations

MySyntaxEngine can parse token streams while carrying caller-defined semantic
values. This enables language frontends to build ASTs with the existing LR table
and error-reporting implementation.

## Goals

- Keep existing syntax-only APIs unchanged.
- Allow shifted tokens to carry a lexeme, source span, and caller-owned value.
- Invoke a caller callback for each reduction.
- Return the accepted start-symbol value as the parse root.
- Destroy values discarded during error handling.
- Keep the engine language-agnostic: AST node types remain caller-defined.

## Public model

`SyntaxInputToken` carries the terminal id, original lexeme, source span, and an
optional semantic value. During parsing, the runtime maintains a parallel stack
of `SyntaxSemanticValue` entries.

For a production such as:

```text
primary -> IDENTIFIER
```

the callback receives a `SyntaxReduceEvent` containing the production id, the
left-hand symbol id, the right-hand semantic values, and the span covering the
reduction. It returns the value to push for the left-hand side.

## Source span rules

- A shifted symbol uses the span supplied by its input token.
- A non-empty reduction spans from the first RHS symbol start to the last RHS
  symbol end.
- An empty reduction has a zero-width span at the current lookahead position.
- The accepted root exposes the final start-symbol span.

## Ownership

The parser owns semantic values while they remain on its stack. A reduce callback
takes ownership of the RHS values and returns a new LHS value. Values still on
the stack when parsing fails are passed to the registered destructor. On success,
ownership of the accepted root transfers to the caller.

## MyLang integration

This API allows `core.grammar` and `dom.grammar` to build extension AST nodes
without generating intermediate source text. DOM attribute expressions can
carry normal MyLang AST nodes, and diagnostics can continue to point at the
original `.dom.mln` source.

## Follow-up work

- Add focused semantic-value tests for AST construction and epsilon reductions.
- Add destructor-count tests for syntax failures.
- Add grammar composition support for core MyLang plus DOM syntax.
