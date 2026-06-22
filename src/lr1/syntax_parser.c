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

const char *syntax_label_name(const SyntaxTable *table, int label_id) {
    return lr1_label_name(table->grammar, label_id);
}

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
    SyntaxResult result = {0};
    result.status = SYNTAX_ERROR;
    const SyntaxGrammar *grammar = table->grammar;
    if (out_symbol_count) *out_symbol_count = 0;
    /* Brace nesting: a declaration is top-level (outline symbol) iff it reduces
       at depth 0, which separates globals from locals and struct fields. */
    int brace_depth = 0;

    int stack_cap = 128;
    int stack_len = 1;
    int *stack = lr1_xcalloc((size_t)stack_cap, sizeof(int));
    /* Parallel stack: leftmost input-token index each stack symbol derives. */
    int *pos_stack = lr1_xcalloc((size_t)stack_cap, sizeof(int));
    stack[0] = 0;
    pos_stack[0] = -1;

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
            if (lookahead == grammar->scope_open) brace_depth++;
            else if (lookahead == grammar->scope_close && brace_depth > 0) brace_depth--;
            if (stack_len == stack_cap) {
                stack_cap *= 2;
                stack = lr1_xrealloc(stack, (size_t)stack_cap * sizeof(int));
                pos_stack = lr1_xrealloc(pos_stack, (size_t)stack_cap * sizeof(int));
            }
            pos_stack[stack_len] = (int)index;
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

            int rhs_base = stack_len - production->rhs_len;  // slot of first RHS symbol

            /* Apply the production's grammar annotations to its RHS symbols.
               Both roles and outline symbols name the symbol's leftmost token
               (pos_stack), so terminals and nonterminals are handled uniformly. */
            if (production->annot) {
                for (int j = 0; j < production->rhs_len; j++) {
                    const RhsAnnot *a = &production->annot[j];
                    if (!a->role && !a->decl) continue;
                    int tok = pos_stack[rhs_base + j];
                    if (tok < 0 || (size_t)tok >= token_count) continue;

                    if (a->role && out_roles) {
                        bool ok = true;
                        if (a->single) {
                            /* width = next symbol's start (or current index) - start */
                            int next = (j + 1 < production->rhs_len)
                                       ? pos_stack[rhs_base + j + 1] : (int)index;
                            ok = (next - tok == 1);
                        }
                        if (ok) out_roles[tok] = a->role;
                    }
                    if (a->decl && out_symbols && out_symbol_count &&
                        *out_symbol_count < symbol_cap && brace_depth == 0) {
                        out_symbols[*out_symbol_count].token_index = tok;
                        out_symbols[*out_symbol_count].kind = a->decl;
                        (*out_symbol_count)++;
                    }
                }
            }

            int lhs_pos = (production->rhs_len > 0) ? pos_stack[rhs_base] : (int)index;
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
                pos_stack = lr1_xrealloc(pos_stack, (size_t)stack_cap * sizeof(int));
            }
            pos_stack[stack_len] = lhs_pos;
            stack[stack_len++] = goto_state;
            continue;
        }
    }

    free(stack);
    free(pos_stack);
    return result;
}

void syntax_result_free(SyntaxResult *result) {
    free(result->expected);
    memset(result, 0, sizeof(*result));
}
