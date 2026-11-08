#include "syntax_engine_internal.h"
#include "syntax_engine/semantic_actions.h"

#include <stdlib.h>
#include <string.h>

static int find_terminal(const SyntaxGrammar *grammar, const char *name) {
    return lr1_find_symbol(grammar, name, SYMBOL_TERMINAL);
}

int syntax_terminal_id(const SyntaxGrammar *grammar, const char *name) {
    return find_terminal(grammar, name);
}

static void fill_expected(const SyntaxTable *table, SyntaxResult *result, int state) {
    const SyntaxGrammar *grammar = table->grammar;
    result->state = state;
    result->expected = lr1_xcalloc((size_t)grammar->symbol_count, sizeof(char *));
    for (int symbol = 0; symbol < grammar->symbol_count; symbol++) {
        if (!lr1_symbol_is_terminal(grammar, symbol)) continue;
        if (table->actions[state][symbol].kind == ACTION_NONE) continue;
        result->expected[result->expected_count++] = grammar->symbols[symbol].name;
    }
}

/* A parse that stops at input token `index` in `state`, with the terminals
   that state would have accepted. */
static SyntaxResult fail_at(const SyntaxTable *table, SyntaxStatus status, size_t index, int state) {
    SyntaxResult result = {0};
    result.status = status;
    result.token_index = index;
    fill_expected(table, &result, state);
    return result;
}

SyntaxResult syntax_parse_token_names(
    SyntaxTable *table,
    const char **token_names,
    size_t token_count
) {
    const SyntaxGrammar *grammar = table->grammar;
    int *token_ids = lr1_xcalloc(token_count ? token_count : 1, sizeof(int));

    for (size_t i = 0; i < token_count; i++) {
        token_ids[i] = find_terminal(grammar, token_names[i]);
        if (token_ids[i] < 0) {
            free(token_ids);
            return fail_at(table, SYNTAX_ERROR, i, 0);
        }
    }

    SyntaxResult result = syntax_parse_token_ids(table, token_ids, token_count);
    free(token_ids);
    return result;
}

const char *syntax_label_name(const SyntaxTable *table, int label_id) {
    return lr1_label_name(table->grammar, label_id);
}

/* The LR stack. Beside each state it keeps the input token where the symbol
   the entry stands for starts and, for a parse with data of its own,
   `data_size` bytes of that data. Entry 0 holds the start state and stands
   for no symbol. */
typedef struct {
    int *states;
    int *starts;
    char *data;
    size_t data_size;
    int len;
    int cap;
} ParseStack;

static ParseStack stack_new(size_t data_size) {
    ParseStack stack;
    stack.cap = 128;
    stack.len = 1;
    stack.data_size = data_size;
    stack.states = lr1_xcalloc((size_t)stack.cap, sizeof(int));
    stack.starts = lr1_xcalloc((size_t)stack.cap, sizeof(int));
    stack.data = data_size ? lr1_xcalloc((size_t)stack.cap, data_size) : NULL;
    stack.states[0] = 0;
    stack.starts[0] = -1;
    return stack;
}

static void *stack_data(const ParseStack *stack, int entry) {
    return stack->data + (size_t)entry * stack->data_size;
}

static void stack_grow(ParseStack *stack) {
    stack->cap *= 2;
    stack->states = lr1_xrealloc(stack->states, (size_t)stack->cap * sizeof(int));
    stack->starts = lr1_xrealloc(stack->starts, (size_t)stack->cap * sizeof(int));
    if (stack->data) {
        stack->data = lr1_xrealloc(stack->data, (size_t)stack->cap * stack->data_size);
    }
}

static void stack_free(ParseStack *stack) {
    free(stack->states);
    free(stack->starts);
    free(stack->data);
}

/* What a parse adds to the LR driver. Any hook may be NULL. */
typedef struct {
    /* Bytes of data the parse keeps per stack entry, or 0; the next four
       hooks look after that data. */
    size_t data_size;
    /* Fill `data` for `terminal`, just shifted from input token `index`
       (index >= token_count when the grammar shifts the end of input). */
    void (*shift)(void *ctx, void *data, int terminal, size_t index);
    /* Reduce by production `production_id` in place: `data` holds the data of
       the production's right-hand side, which leaves the stack after this
       call; write the left-hand side's data over its first entry (the free
       entry at `data`, for an empty right-hand side). `index` is the
       lookahead token. */
    void (*reduce)(void *ctx, int production_id, void *data, size_t index);
    /* Take over the data of the accepted start symbol. */
    void (*accept)(void *ctx, void *data);
    /* Release data still on the stack when the parse ends, and the data of a
       reduced left-hand side that has no goto. */
    void (*discard)(void *ctx, void *data);
    /* Apply the @role/@decl annotations of a production being reduced:
       `starts` holds the input token each right-hand-side symbol starts at,
       `index` is the lookahead token, and `scope_depth` is how many %scope
       openers the input shifted so far has left open. */
    void (*annotate)(void *ctx, const Production *production, const int *starts,
                     size_t index, int scope_depth);
} ParseHooks;

/* The terminal at input token `index`: the end of input past the last token,
   or -1 when the token's id is not one of the grammar's terminals. */
static int lookahead_at(const SyntaxGrammar *grammar, const int *token_ids, size_t token_count,
                        size_t index) {
    if (index >= token_count) return grammar->eof_symbol;
    int id = token_ids[index];
    if (id < 0 || id >= grammar->symbol_count || !lr1_symbol_is_terminal(grammar, id)) return -1;
    return id;
}

/* The LR(1) driver behind every parse: run `token_ids` through the table. The
   driver tracks what the grammar's annotations need -- where each stack
   symbol starts, and the %scope depth -- and `hooks` keep any data of the
   parse's own. */
static SyntaxResult lr_parse(
    const SyntaxTable *table,
    const int *token_ids,
    size_t token_count,
    const ParseHooks *hooks,
    void *ctx
) {
    const SyntaxGrammar *grammar = table->grammar;
    ParseStack stack = stack_new(hooks->data_size);
    int scope_depth = 0;
    SyntaxResult result;

    int state = stack.states[0];  /* the state on top of the stack */
    size_t index = 0;
    int lookahead = lookahead_at(grammar, token_ids, token_count, index);
    for (;;) {
        if (lookahead < 0) {
            result = fail_at(table, SYNTAX_ERROR, index, state);
            break;
        }

        ActionCell action = table->actions[state][lookahead];
        if (action.kind == ACTION_NONE) {
            SyntaxStatus status = (lookahead == grammar->eof_symbol) ? SYNTAX_INCOMPLETE : SYNTAX_ERROR;
            result = fail_at(table, status, index, state);
            break;
        }

        if (action.kind == ACTION_ACCEPT) {
            result = (SyntaxResult){.status = SYNTAX_OK, .token_index = index, .state = state};
            if (hooks->accept && stack.len > 1) hooks->accept(ctx, stack_data(&stack, stack.len - 1));
            break;
        }

        /* Either action may take a new entry: a shift always does, and so does
           the left-hand side of an empty right-hand side. */
        if (stack.len == stack.cap) stack_grow(&stack);

        if (action.kind == ACTION_SHIFT) {
            if (lookahead == grammar->scope_open) scope_depth++;
            else if (lookahead == grammar->scope_close && scope_depth > 0) scope_depth--;
            stack.starts[stack.len] = (int)index;
            if (hooks->shift) hooks->shift(ctx, stack_data(&stack, stack.len), lookahead, index);
            state = action.value;
            stack.states[stack.len++] = state;
            lookahead = lookahead_at(grammar, token_ids, token_count, ++index);
            continue;
        }

        const Production *production = &grammar->productions[action.value];
        if (production->rhs_len >= stack.len) {
            result = fail_at(table, SYNTAX_ERROR, index, state);
            break;
        }

        int rhs_base = stack.len - production->rhs_len;
        if (production->annot && hooks->annotate) {
            hooks->annotate(ctx, production, &stack.starts[rhs_base], index, scope_depth);
        }
        /* The left-hand side starts where its first symbol does, or at the
           lookahead when it has none. */
        int start = (production->rhs_len > 0) ? stack.starts[rhs_base] : (int)index;
        if (hooks->reduce) hooks->reduce(ctx, action.value, stack_data(&stack, rhs_base), index);
        stack.len = rhs_base;

        int goto_from = stack.states[rhs_base - 1];
        int goto_state = table->gotos[goto_from][production->lhs];
        if (goto_state < 0) {
            if (hooks->discard) hooks->discard(ctx, stack_data(&stack, rhs_base));
            result = fail_at(table, SYNTAX_ERROR, index, goto_from);
            break;
        }
        state = goto_state;
        stack.starts[stack.len] = start;
        stack.states[stack.len++] = state;
    }

    for (int i = 1; hooks->discard && i < stack.len; i++) {
        hooks->discard(ctx, stack_data(&stack, i));
    }
    stack_free(&stack);
    return result;
}

/* A syntax-only parse, which applies the grammar's @role and @decl annotations
   to the caller's buffers as it reduces. */
typedef struct {
    size_t token_count;
    int *out_roles;
    SyntaxSymbol *out_symbols;
    size_t symbol_cap;
    size_t *out_symbol_count;
} TokenParse;

static void token_annotate(void *ctx, const Production *production, const int *starts,
                           size_t index, int scope_depth) {
    TokenParse *parse = ctx;
    for (int j = 0; j < production->rhs_len; j++) {
        const RhsAnnot *a = &production->annot[j];
        if (!a->role && !a->decl) continue;
        int tok = starts[j];
        if (tok < 0 || (size_t)tok >= parse->token_count) continue;

        if (a->role && parse->out_roles) {
            bool ok = true;
            if (a->single) {
                int next = (j + 1 < production->rhs_len) ? starts[j + 1] : (int)index;
                ok = (next - tok == 1);
            }
            if (ok) parse->out_roles[tok] = a->role;
        }
        /* A declaration is top-level (outline symbol) iff it reduces at scope
           depth 0, which separates globals from locals and struct fields. */
        if (a->decl && parse->out_symbols && parse->out_symbol_count &&
            *parse->out_symbol_count < parse->symbol_cap && scope_depth == 0) {
            parse->out_symbols[*parse->out_symbol_count].token_index = tok;
            parse->out_symbols[*parse->out_symbol_count].kind = a->decl;
            (*parse->out_symbol_count)++;
        }
    }
}

static const ParseHooks token_hooks = {
    .annotate = token_annotate,
};

SyntaxResult syntax_parse_token_ids(
    SyntaxTable *table,
    const int *token_ids,
    size_t token_count
) {
    return syntax_parse_token_ids_ex(table, token_ids, token_count, NULL, NULL, 0, NULL);
}

SyntaxResult syntax_parse_token_ids_roles(
    SyntaxTable *table,
    const int *token_ids,
    size_t token_count,
    int *out_roles
) {
    return syntax_parse_token_ids_ex(table, token_ids, token_count, out_roles, NULL, 0, NULL);
}

SyntaxResult syntax_parse_token_ids_ex(
    SyntaxTable *table,
    const int *token_ids,
    size_t token_count,
    int *out_roles,
    SyntaxSymbol *out_symbols,
    size_t symbol_cap,
    size_t *out_symbol_count
) {
    if (out_symbol_count) *out_symbol_count = 0;
    TokenParse parse = {
        .token_count = token_count,
        .out_roles = out_roles,
        .out_symbols = out_symbols,
        .symbol_cap = symbol_cap,
        .out_symbol_count = out_symbol_count,
    };
    return lr_parse(table, token_ids, token_count, &token_hooks, &parse);
}

static SyntaxSourceSpan zero_width_span(const SyntaxInputToken *tokens, size_t token_count, size_t index) {
    SyntaxSourceSpan span = {0};
    if (index < token_count) {
        span.byte_start = tokens[index].span.byte_start;
        span.byte_end = tokens[index].span.byte_start;
        span.line_start = tokens[index].span.line_start;
        span.column_start = tokens[index].span.column_start;
        span.line_end = span.line_start;
        span.column_end = span.column_start;
    } else if (token_count > 0) {
        span.byte_start = tokens[token_count - 1].span.byte_end;
        span.byte_end = span.byte_start;
        span.line_start = tokens[token_count - 1].span.line_end;
        span.column_start = tokens[token_count - 1].span.column_end;
        span.line_end = span.line_start;
        span.column_end = span.column_start;
    }
    return span;
}

/* A parse that builds the caller's semantic values. The data of a stack entry
   is its symbol's SyntaxSemanticValue, which the parse owns while it is on
   the stack. */
typedef struct {
    const SyntaxGrammar *grammar;
    const SyntaxInputToken *tokens;
    size_t token_count;
    const SyntaxActionSet *actions;
    SyntaxActionResult *result;
} ActionParse;

static void action_shift(void *ctx, void *data, int terminal, size_t index) {
    const ActionParse *parse = ctx;
    SyntaxSemanticValue *value = data;
    value->symbol_id = terminal;
    if (index < parse->token_count) {
        value->value = parse->tokens[index].value;
        value->span = parse->tokens[index].span;
    } else {
        value->value = NULL;
        value->span = zero_width_span(parse->tokens, parse->token_count, index);
    }
}

static void action_reduce(void *ctx, int production_id, void *data, size_t index) {
    const ActionParse *parse = ctx;
    const Production *production = &parse->grammar->productions[production_id];
    SyntaxSemanticValue *rhs = data;

    SyntaxSourceSpan span = zero_width_span(parse->tokens, parse->token_count, index);
    if (production->rhs_len > 0) {
        span = rhs[0].span;
        span.byte_end = rhs[production->rhs_len - 1].span.byte_end;
        span.line_end = rhs[production->rhs_len - 1].span.line_end;
        span.column_end = rhs[production->rhs_len - 1].span.column_end;
    }

    SyntaxReduceEvent event;
    event.production_id = production_id;
    event.lhs_symbol_id = production->lhs;
    event.rhs = production->rhs_len > 0 ? rhs : NULL;
    event.rhs_count = (size_t)production->rhs_len;
    event.span = span;

    void *lhs_value = NULL;
    if (parse->actions && parse->actions->reduce) {
        lhs_value = parse->actions->reduce(&event, parse->actions->user_data);
    }

    /* The callback owns the right-hand side's values now. */
    rhs[0].symbol_id = production->lhs;
    rhs[0].value = lhs_value;
    rhs[0].span = span;
}

static void action_accept(void *ctx, void *data) {
    const ActionParse *parse = ctx;
    SyntaxSemanticValue *root = data;
    /* Ownership of the root passes to the caller. */
    parse->result->root = root->value;
    parse->result->root_span = root->span;
    root->value = NULL;
}

static void action_discard(void *ctx, void *data) {
    const ActionParse *parse = ctx;
    SyntaxSemanticValue *value = data;
    if (value->value && parse->actions && parse->actions->destroy) {
        parse->actions->destroy(value->symbol_id, value->value, parse->actions->user_data);
        value->value = NULL;
    }
}

static const ParseHooks action_hooks = {
    .data_size = sizeof(SyntaxSemanticValue),
    .shift = action_shift,
    .reduce = action_reduce,
    .accept = action_accept,
    .discard = action_discard,
};

SyntaxActionResult syntax_parse_with_actions(
    SyntaxTable *table,
    const SyntaxInputToken *tokens,
    size_t token_count,
    const SyntaxActionSet *actions
) {
    SyntaxActionResult result;
    memset(&result, 0, sizeof(result));
    result.syntax.status = SYNTAX_ERROR;

    if (!table || (token_count > 0 && !tokens)) {
        return result;
    }

    int *token_ids = lr1_xcalloc(token_count ? token_count : 1, sizeof(int));
    for (size_t i = 0; i < token_count; i++) token_ids[i] = tokens[i].terminal_id;

    ActionParse parse = {
        .grammar = table->grammar,
        .tokens = tokens,
        .token_count = token_count,
        .actions = actions,
        .result = &result,
    };
    result.syntax = lr_parse(table, token_ids, token_count, &action_hooks, &parse);
    free(token_ids);
    return result;
}

void syntax_action_result_free(SyntaxActionResult *result) {
    if (!result) return;
    syntax_result_free(&result->syntax);
    result->root = NULL;
    memset(&result->root_span, 0, sizeof(result->root_span));
}

void syntax_result_free(SyntaxResult *result) {
    free(result->expected);
    memset(result, 0, sizeof(*result));
}
