#ifndef SYNTAX_ENGINE_SEMANTIC_ACTIONS_H
#define SYNTAX_ENGINE_SEMANTIC_ACTIONS_H

#include <stddef.h>

#include "syntax_engine.h"

typedef struct {
    size_t byte_start;
    size_t byte_end;
    size_t line_start;
    size_t column_start;
    size_t line_end;
    size_t column_end;
} SyntaxSourceSpan;

typedef struct {
    int terminal_id;
    const char *lexeme;
    size_t lexeme_length;
    SyntaxSourceSpan span;
    void *value;
} SyntaxInputToken;

typedef struct {
    int symbol_id;
    void *value;
    SyntaxSourceSpan span;
} SyntaxSemanticValue;

typedef struct {
    int production_id;
    int lhs_symbol_id;
    const SyntaxSemanticValue *rhs;
    size_t rhs_count;
    SyntaxSourceSpan span;
} SyntaxReduceEvent;

typedef void *(*SyntaxReduceAction)(
    const SyntaxReduceEvent *event,
    void *user_data
);

typedef void (*SyntaxValueDestructor)(
    int symbol_id,
    void *value,
    void *user_data
);

typedef struct {
    SyntaxReduceAction reduce;
    SyntaxValueDestructor destroy;
    void *user_data;
} SyntaxActionSet;

typedef struct {
    SyntaxResult syntax;
    void *root;
    SyntaxSourceSpan root_span;
} SyntaxActionResult;

/*
 * Parse tokens while maintaining a semantic-value stack. Shifted values come
 * from SyntaxInputToken.value; reductions call actions->reduce. The returned
 * root is the semantic value of the accepted start symbol.
 *
 * Ownership contract:
 * - The engine owns semantic values while they remain on the parser stack.
 * - A reduce callback takes ownership of all non-NULL RHS values and returns
 *   the value that the engine should own for the LHS symbol.
 * - On syntax failure, values still on the stack are passed to destroy.
 * - If a computed LHS cannot be shifted because the goto is invalid, that LHS
 *   value is passed to destroy.
 * - On success, ownership of result.root transfers to the caller.
 * - syntax_action_result_free releases syntax diagnostics, never result.root.
 * - Token lexeme storage remains caller-owned for the entire parse.
 */
SyntaxActionResult syntax_parse_with_actions(
    SyntaxTable *table,
    const SyntaxInputToken *tokens,
    size_t token_count,
    const SyntaxActionSet *actions
);

void syntax_action_result_free(SyntaxActionResult *result);

#endif
