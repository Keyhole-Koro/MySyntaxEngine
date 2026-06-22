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

/* Role assigned to the direct IDENTIFIER terminals of a production, keyed by the
   production's left-hand-side nonterminal name. Returns SYNTAX_ROLE_NONE for
   productions whose identifiers should keep their default classification. */
static int role_for_lhs(const char *name) {
    if (strcmp(name, "funcDef") == 0 || strcmp(name, "funcProto") == 0)
        return SYNTAX_ROLE_FUNCTION;
    if (strcmp(name, "packageDecl") == 0 || strcmp(name, "importDecl") == 0)
        return SYNTAX_ROLE_NAMESPACE;
    if (strcmp(name, "structDecl") == 0 || strcmp(name, "enumDecl") == 0)
        return SYNTAX_ROLE_STRUCT;
    if (strcmp(name, "typedefStmt") == 0 || strcmp(name, "baseType") == 0)
        return SYNTAX_ROLE_TYPE;
    if (strcmp(name, "postfixExpr") == 0)
        return SYNTAX_ROLE_PROPERTY;  // only the DOT/MEMBER forms have a direct IDENTIFIER
    return SYNTAX_ROLE_NONE;
}

/* Outline-symbol kind a production declares, or -1 if it declares none. */
static int symbol_kind_for_lhs(const char *name) {
    if (strcmp(name, "funcDef") == 0 || strcmp(name, "funcProto") == 0)
        return SYNTAX_SYM_FUNCTION;
    if (strcmp(name, "structDecl") == 0) return SYNTAX_SYM_STRUCT;
    if (strcmp(name, "enumDecl") == 0) return SYNTAX_SYM_ENUM;
    if (strcmp(name, "typedefStmt") == 0) return SYNTAX_SYM_TYPE;
    if (strcmp(name, "varDecl") == 0) return SYNTAX_SYM_VARIABLE;
    return -1;
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
            const char *tname = grammar->symbols[lookahead].name;
            if (strcmp(tname, "L_BRACE") == 0) brace_depth++;
            else if (strcmp(tname, "R_BRACE") == 0 && brace_depth > 0) brace_depth--;
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

            /* Attribute grammar-derived roles to the production's direct
               IDENTIFIER terminals (and the param declarator). */
            if (out_roles) {
                const char *lhs_name = grammar->symbols[production->lhs].name;
                int role = role_for_lhs(lhs_name);
                bool is_param = strcmp(lhs_name, "param") == 0;
                if (role != SYNTAX_ROLE_NONE || is_param) {
                    for (int j = 0; j < production->rhs_len; j++) {
                        const char *sym = grammar->symbols[production->rhs[j]].name;
                        int tok = pos_stack[rhs_base + j];
                        if (tok < 0 || (size_t)tok >= token_count) continue;
                        if (role != SYNTAX_ROLE_NONE && strcmp(sym, "IDENTIFIER") == 0)
                            out_roles[tok] = role;
                        else if (is_param && strcmp(sym, "declarator") == 0)
                            out_roles[tok] = SYNTAX_ROLE_PARAMETER;
                    }
                }
                /* Simple call: callee is a single-token identifier directly
                   followed by '(' (so 'a.b()' / 'arr[i]()' do not match). */
                if (strcmp(lhs_name, "postfixExpr") == 0 && production->rhs_len >= 2 &&
                    strcmp(grammar->symbols[production->rhs[1]].name, "L_PARENTHESES") == 0) {
                    int callee = pos_stack[rhs_base];
                    int lparen = pos_stack[rhs_base + 1];
                    if (callee >= 0 && (size_t)callee < token_count && lparen == callee + 1 &&
                        strcmp(grammar->symbols[token_ids[callee]].name, "IDENTIFIER") == 0)
                        out_roles[callee] = SYNTAX_ROLE_FUNCTION;
                }
            }

            /* Top-level declaration symbols for the outline. Only at brace
               depth 0, which excludes locals, struct fields and call sites. */
            if (out_symbols && out_symbol_count && *out_symbol_count < symbol_cap &&
                brace_depth == 0) {
                const char *lhs_name = grammar->symbols[production->lhs].name;
                int skind = symbol_kind_for_lhs(lhs_name);
                if (skind >= 0) {
                    /* The declared name is a direct IDENTIFIER, except for
                       varDecl whose name is the declarator's leftmost token. */
                    const char *want = (skind == SYNTAX_SYM_VARIABLE) ? "declarator" : "IDENTIFIER";
                    for (int j = 0; j < production->rhs_len; j++) {
                        if (strcmp(grammar->symbols[production->rhs[j]].name, want) != 0)
                            continue;
                        int tok = pos_stack[rhs_base + j];
                        if (tok < 0 || (size_t)tok >= token_count) break;
                        out_symbols[*out_symbol_count].token_index = tok;
                        out_symbols[*out_symbol_count].kind = skind;
                        (*out_symbol_count)++;
                        break;
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
