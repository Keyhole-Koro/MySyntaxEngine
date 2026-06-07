#include "syntax_engine_internal.h"

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
            SyntaxResult result = {0};
            result.status = SYNTAX_ERROR;
            result.token_index = i;
            result.state = 0;
            fill_expected(table, &result, 0);
            free(token_ids);
            return result;
        }
    }

    SyntaxResult result = syntax_parse_token_ids(table, token_ids, token_count);
    free(token_ids);
    return result;
}

SyntaxResult syntax_parse_token_ids(
    SyntaxTable *table,
    const int *token_ids,
    size_t token_count
) {
    SyntaxResult result = {0};
    result.status = SYNTAX_ERROR;
    const SyntaxGrammar *grammar = table->grammar;

    int stack_cap = 128;
    int stack_len = 1;
    int *stack = lr1_xcalloc((size_t)stack_cap, sizeof(int));
    stack[0] = 0;

    size_t index = 0;
    for (;;) {
        int state = stack[stack_len - 1];
        int lookahead = grammar->eof_symbol;
        if (index < token_count) {
            lookahead = token_ids[index];
            if (lookahead < 0) {
                result.status = SYNTAX_ERROR;
                result.token_index = index;
                fill_expected(table, &result, state);
                break;
            }
        }

        ActionCell action = table->actions[state][lookahead];
        if (action.kind == ACTION_NONE) {
            result.status = (lookahead == grammar->eof_symbol) ? SYNTAX_INCOMPLETE : SYNTAX_ERROR;
            result.token_index = index;
            fill_expected(table, &result, state);
            break;
        }

        if (action.kind == ACTION_ACCEPT) {
            result.status = SYNTAX_OK;
            result.token_index = index;
            result.state = state;
            break;
        }

        if (action.kind == ACTION_SHIFT) {
            if (stack_len == stack_cap) {
                stack_cap *= 2;
                stack = lr1_xrealloc(stack, (size_t)stack_cap * sizeof(int));
            }
            stack[stack_len++] = action.value;
            index++;
            continue;
        }

        if (action.kind == ACTION_REDUCE) {
            const Production *production = &grammar->productions[action.value];
            if (production->rhs_len >= stack_len) {
                result.status = SYNTAX_ERROR;
                result.token_index = index;
                fill_expected(table, &result, state);
                break;
            }

            stack_len -= production->rhs_len;
            int goto_from = stack[stack_len - 1];
            int goto_state = table->gotos[goto_from][production->lhs];
            if (goto_state < 0) {
                result.status = SYNTAX_ERROR;
                result.token_index = index;
                fill_expected(table, &result, goto_from);
                break;
            }

            if (stack_len == stack_cap) {
                stack_cap *= 2;
                stack = lr1_xrealloc(stack, (size_t)stack_cap * sizeof(int));
            }
            stack[stack_len++] = goto_state;
            continue;
        }
    }

    free(stack);
    return result;
}

void syntax_result_free(SyntaxResult *result) {
    free(result->expected);
    memset(result, 0, sizeof(*result));
}
