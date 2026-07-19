# Semantic actions and source locations

MySyntaxEngine currently validates token streams and reports expected tokens.
The next runtime layer adds semantic values so language frontends can build ASTs
while retaining the same LR table and error reporting.

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

The caller owns token lexeme storage. Semantic values are temporarily owned by
the parser stack. When a value is discarded because of an error or cleanup, the
registered destructor is called. On successful acceptance, ownership of the
root value transfers to the caller.

## MyLang integration

This API allows `core.grammar` and `dom.grammar` to build extension AST nodes
without generating intermediate source text. DOM attribute expressions can
carry normal MyLang AST nodes, and diagnostics can continue to point at the
original `.dom.mln` source.

## Implementation sequence

1. Add a semantic-value stack beside the LR state stack.
2. Expose production and symbol ids during reduce actions.
3. Add source-span combination rules.
4. Add destructor calls on failure and parser cleanup.
5. Add tests for successful AST construction, epsilon reductions, and cleanup.
6. Add grammar composition tests for core MyLang plus DOM syntax.
