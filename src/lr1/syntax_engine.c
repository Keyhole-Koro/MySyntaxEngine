#include "mylang_syntax_engine/syntax_engine.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    SYMBOL_TERMINAL,
    SYMBOL_NONTERMINAL
} SymbolKind;

typedef struct {
    char *name;
    SymbolKind kind;
} SymbolDef;

typedef struct {
    int lhs;
    int *rhs;
    int rhs_len;
} Production;

typedef struct {
    int production;
    int dot;
    int lookahead;
} LR1Item;

typedef struct {
    LR1Item *items;
    int count;
    int cap;
} ItemSet;

typedef enum {
    ACTION_NONE = 0,
    ACTION_SHIFT,
    ACTION_REDUCE,
    ACTION_ACCEPT
} ActionKind;

typedef struct {
    ActionKind kind;
    int value;
} ActionCell;

struct SyntaxGrammar {
    SymbolDef *symbols;
    int symbol_count;
    int symbol_cap;
    Production *productions;
    int production_count;
    int production_cap;
    int start_symbol;
    int eof_symbol;
};

struct SyntaxTable {
    SyntaxGrammar *grammar;
    ItemSet *states;
    int state_count;
    int state_cap;
    int row_cap;
    ActionCell **actions;
    int **gotos;
    int conflict_count;
};

static char *xstrdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (!copy) {
        fprintf(stderr, "out of memory\n");
        exit(EXIT_FAILURE);
    }
    memcpy(copy, s, len);
    return copy;
}

static void *xcalloc(size_t count, size_t size) {
    void *ptr = calloc(count, size);
    if (!ptr) {
        fprintf(stderr, "out of memory\n");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

static void *xrealloc(void *ptr, size_t size) {
    void *resized = realloc(ptr, size);
    if (!resized) {
        fprintf(stderr, "out of memory\n");
        exit(EXIT_FAILURE);
    }
    return resized;
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }
    return s;
}

static bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           (c >= '0' && c <= '9');
}

static int find_symbol(const SyntaxGrammar *grammar, const char *name, SymbolKind kind) {
    for (int i = 0; i < grammar->symbol_count; i++) {
        if (grammar->symbols[i].kind == kind && strcmp(grammar->symbols[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int add_symbol(SyntaxGrammar *grammar, const char *name, SymbolKind kind) {
    int found = find_symbol(grammar, name, kind);
    if (found >= 0) return found;

    if (grammar->symbol_count == grammar->symbol_cap) {
        grammar->symbol_cap = grammar->symbol_cap ? grammar->symbol_cap * 2 : 32;
        grammar->symbols = xrealloc(grammar->symbols, (size_t)grammar->symbol_cap * sizeof(SymbolDef));
    }

    int id = grammar->symbol_count++;
    grammar->symbols[id].name = xstrdup(name);
    grammar->symbols[id].kind = kind;
    return id;
}

static void add_production(SyntaxGrammar *grammar, int lhs, const int *rhs, int rhs_len) {
    if (grammar->production_count == grammar->production_cap) {
        grammar->production_cap = grammar->production_cap ? grammar->production_cap * 2 : 32;
        grammar->productions = xrealloc(
            grammar->productions,
            (size_t)grammar->production_cap * sizeof(Production)
        );
    }

    Production *production = &grammar->productions[grammar->production_count++];
    production->lhs = lhs;
    production->rhs_len = rhs_len;
    production->rhs = xcalloc((size_t)rhs_len, sizeof(int));
    memcpy(production->rhs, rhs, (size_t)rhs_len * sizeof(int));
}

static bool parse_rhs(SyntaxGrammar *grammar, char *text, int **out_rhs, int *out_len) {
    int cap = 8;
    int len = 0;
    int *rhs = xcalloc((size_t)cap, sizeof(int));
    char *p = text;

    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '\r') break;

        char token[128];
        int token_len = 0;
        SymbolKind kind = SYMBOL_NONTERMINAL;

        if (*p == '\'') {
            kind = SYMBOL_TERMINAL;
            p++;
            while (*p && *p != '\'' && token_len < (int)sizeof(token) - 1) {
                token[token_len++] = *p++;
            }
            if (*p != '\'') {
                free(rhs);
                return false;
            }
            p++;
        } else if (*p == '$') {
            kind = SYMBOL_TERMINAL;
            token[token_len++] = *p++;
        } else if (is_word_char(*p)) {
            while (*p && is_word_char(*p) && token_len < (int)sizeof(token) - 1) {
                token[token_len++] = *p++;
            }
        } else {
            free(rhs);
            return false;
        }

        token[token_len] = '\0';
        if (len == cap) {
            cap *= 2;
            rhs = xrealloc(rhs, (size_t)cap * sizeof(int));
        }
        rhs[len++] = add_symbol(grammar, token, kind);
    }

    *out_rhs = rhs;
    *out_len = len;
    return true;
}

SyntaxGrammar *syntax_load_grammar(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "failed to open grammar: %s\n", path);
        return NULL;
    }

    SyntaxGrammar *grammar = xcalloc(1, sizeof(SyntaxGrammar));
    grammar->start_symbol = -1;
    grammar->eof_symbol = add_symbol(grammar, "$", SYMBOL_TERMINAL);

    char line[1024];
    int current_lhs = -1;

    while (fgets(line, sizeof(line), file)) {
        char *text = trim(line);
        if (*text == '\0') continue;

        char *equals = strchr(text, '=');
        char *colon = strchr(text, ':');
        char *pipe = strchr(text, '|');
        char *rule_marker = colon;
        if (pipe && (!rule_marker || pipe < rule_marker)) rule_marker = pipe;

        if (equals && (!rule_marker || equals < rule_marker)) {
            continue;
        }

        char *rhs_text = NULL;
        if (colon) {
            *colon = '\0';
            char *lhs_name = trim(text);
            if (*lhs_name == '\0') continue;
            current_lhs = add_symbol(grammar, lhs_name, SYMBOL_NONTERMINAL);
            if (grammar->start_symbol < 0) grammar->start_symbol = current_lhs;
            rhs_text = trim(colon + 1);
        } else if (pipe) {
            if (current_lhs < 0) {
                fprintf(stderr, "grammar alternative without lhs: %s\n", path);
                syntax_free_grammar(grammar);
                fclose(file);
                return NULL;
            }
            rhs_text = trim(pipe + 1);
        } else {
            continue;
        }

        int *rhs = NULL;
        int rhs_len = 0;
        if (!parse_rhs(grammar, rhs_text, &rhs, &rhs_len)) {
            fprintf(stderr, "failed to parse grammar rhs: %s\n", rhs_text);
            syntax_free_grammar(grammar);
            fclose(file);
            return NULL;
        }
        add_production(grammar, current_lhs, rhs, rhs_len);
        free(rhs);
    }

    fclose(file);

    if (grammar->start_symbol < 0 || grammar->production_count == 0) {
        fprintf(stderr, "grammar has no productions: %s\n", path);
        syntax_free_grammar(grammar);
        return NULL;
    }

    int augmented = add_symbol(grammar, "__START__", SYMBOL_NONTERMINAL);
    int rhs[] = {grammar->start_symbol};
    add_production(grammar, augmented, rhs, 1);
    Production tmp = grammar->productions[grammar->production_count - 1];
    memmove(&grammar->productions[1], &grammar->productions[0], (size_t)(grammar->production_count - 1) * sizeof(Production));
    grammar->productions[0] = tmp;
    grammar->start_symbol = augmented;

    return grammar;
}

static bool symbol_is_terminal(const SyntaxGrammar *grammar, int symbol) {
    return grammar->symbols[symbol].kind == SYMBOL_TERMINAL;
}

static void add_first_symbol(const SyntaxGrammar *grammar, bool **first, int nonterminal, int terminal) {
    (void)grammar;
    first[nonterminal][terminal] = true;
}

static bool **compute_first_sets(const SyntaxGrammar *grammar) {
    bool **first = xcalloc((size_t)grammar->symbol_count, sizeof(bool *));
    for (int i = 0; i < grammar->symbol_count; i++) {
        first[i] = xcalloc((size_t)grammar->symbol_count, sizeof(bool));
    }

    bool changed;
    do {
        changed = false;
        for (int i = 0; i < grammar->production_count; i++) {
            const Production *production = &grammar->productions[i];
            if (production->rhs_len == 0) continue;
            int first_sym = production->rhs[0];
            if (symbol_is_terminal(grammar, first_sym)) {
                if (!first[production->lhs][first_sym]) {
                    add_first_symbol(grammar, first, production->lhs, first_sym);
                    changed = true;
                }
            } else {
                for (int t = 0; t < grammar->symbol_count; t++) {
                    if (!first[first_sym][t] || first[production->lhs][t]) continue;
                    first[production->lhs][t] = true;
                    changed = true;
                }
            }
        }
    } while (changed);

    return first;
}

static void free_first_sets(bool **first, int count) {
    if (!first) return;
    for (int i = 0; i < count; i++) free(first[i]);
    free(first);
}

static bool item_equals(LR1Item a, LR1Item b) {
    return a.production == b.production && a.dot == b.dot && a.lookahead == b.lookahead;
}

static bool itemset_contains(const ItemSet *set, LR1Item item) {
    for (int i = 0; i < set->count; i++) {
        if (item_equals(set->items[i], item)) return true;
    }
    return false;
}

static bool itemset_add(ItemSet *set, LR1Item item) {
    if (itemset_contains(set, item)) return false;
    if (set->count == set->cap) {
        set->cap = set->cap ? set->cap * 2 : 16;
        set->items = xrealloc(set->items, (size_t)set->cap * sizeof(LR1Item));
    }
    set->items[set->count++] = item;
    return true;
}

static int first_of_suffix(
    const SyntaxGrammar *grammar,
    bool **first,
    const Production *production,
    int dot,
    int lookahead,
    int *out,
    int out_cap
) {
    int after = dot + 1;
    if (after >= production->rhs_len) {
        out[0] = lookahead;
        return 1;
    }

    int symbol = production->rhs[after];
    if (symbol_is_terminal(grammar, symbol)) {
        out[0] = symbol;
        return 1;
    }

    int count = 0;
    for (int t = 0; t < grammar->symbol_count && count < out_cap; t++) {
        if (first[symbol][t]) out[count++] = t;
    }
    return count;
}

static void closure(const SyntaxGrammar *grammar, bool **first, ItemSet *set) {
    bool changed;
    do {
        changed = false;
        for (int i = 0; i < set->count; i++) {
            LR1Item item = set->items[i];
            const Production *production = &grammar->productions[item.production];
            if (item.dot >= production->rhs_len) continue;

            int next_symbol = production->rhs[item.dot];
            if (symbol_is_terminal(grammar, next_symbol)) continue;

            int lookaheads[256];
            int lookahead_count = first_of_suffix(
                grammar,
                first,
                production,
                item.dot,
                item.lookahead,
                lookaheads,
                256
            );

            for (int p = 0; p < grammar->production_count; p++) {
                if (grammar->productions[p].lhs != next_symbol) continue;
                for (int l = 0; l < lookahead_count; l++) {
                    LR1Item next = {p, 0, lookaheads[l]};
                    changed |= itemset_add(set, next);
                }
            }
        }
    } while (changed);
}

static ItemSet goto_set(const SyntaxGrammar *grammar, bool **first, const ItemSet *set, int symbol) {
    ItemSet next = {0};
    for (int i = 0; i < set->count; i++) {
        LR1Item item = set->items[i];
        const Production *production = &grammar->productions[item.production];
        if (item.dot >= production->rhs_len) continue;
        if (production->rhs[item.dot] != symbol) continue;
        LR1Item advanced = {item.production, item.dot + 1, item.lookahead};
        itemset_add(&next, advanced);
    }
    closure(grammar, first, &next);
    return next;
}

static bool itemset_equals(const ItemSet *a, const ItemSet *b) {
    if (a->count != b->count) return false;
    for (int i = 0; i < a->count; i++) {
        if (!itemset_contains(b, a->items[i])) return false;
    }
    return true;
}

static int find_state(const SyntaxTable *table, const ItemSet *set) {
    for (int i = 0; i < table->state_count; i++) {
        if (itemset_equals(&table->states[i], set)) return i;
    }
    return -1;
}

static void ensure_table_rows(SyntaxTable *table, int row_count) {
    const SyntaxGrammar *grammar = table->grammar;
    if (table->row_cap >= row_count) return;

    int old_cap = table->row_cap;
    table->row_cap = table->row_cap ? table->row_cap : 32;
    while (table->row_cap < row_count) table->row_cap *= 2;

    table->actions = xrealloc(table->actions, (size_t)table->row_cap * sizeof(ActionCell *));
    table->gotos = xrealloc(table->gotos, (size_t)table->row_cap * sizeof(int *));
    for (int i = old_cap; i < table->row_cap; i++) {
        table->actions[i] = xcalloc((size_t)grammar->symbol_count, sizeof(ActionCell));
        table->gotos[i] = xcalloc((size_t)grammar->symbol_count, sizeof(int));
        for (int j = 0; j < grammar->symbol_count; j++) table->gotos[i][j] = -1;
    }
}

static int add_state(SyntaxTable *table, ItemSet set) {
    int found = find_state(table, &set);
    if (found >= 0) {
        free(set.items);
        return found;
    }

    if (table->state_count == table->state_cap) {
        table->state_cap = table->state_cap ? table->state_cap * 2 : 32;
        table->states = xrealloc(table->states, (size_t)table->state_cap * sizeof(ItemSet));
    }

    int id = table->state_count++;
    table->states[id] = set;
    ensure_table_rows(table, table->state_count);
    return id;
}

static void set_action(SyntaxTable *table, int state, int terminal, ActionKind kind, int value) {
    ActionCell *cell = &table->actions[state][terminal];
    if (cell->kind != ACTION_NONE && (cell->kind != kind || cell->value != value)) {
        table->conflict_count++;
        return;
    }
    cell->kind = kind;
    cell->value = value;
}

SyntaxTable *syntax_build_lr1_table(SyntaxGrammar *grammar) {
    bool **first = compute_first_sets(grammar);
    SyntaxTable *table = xcalloc(1, sizeof(SyntaxTable));
    table->grammar = grammar;

    ensure_table_rows(table, 1);

    ItemSet start = {0};
    LR1Item start_item = {0, 0, grammar->eof_symbol};
    itemset_add(&start, start_item);
    closure(grammar, first, &start);
    add_state(table, start);

    for (int state = 0; state < table->state_count; state++) {
        for (int symbol = 0; symbol < grammar->symbol_count; symbol++) {
            ItemSet next = goto_set(grammar, first, &table->states[state], symbol);
            if (next.count == 0) {
                free(next.items);
                continue;
            }

            int next_state = add_state(table, next);
            if (symbol_is_terminal(grammar, symbol)) {
                set_action(table, state, symbol, ACTION_SHIFT, next_state);
            } else {
                table->gotos[state][symbol] = next_state;
            }
        }

        for (int i = 0; i < table->states[state].count; i++) {
            LR1Item item = table->states[state].items[i];
            const Production *production = &grammar->productions[item.production];
            if (item.dot != production->rhs_len) continue;

            if (item.production == 0 && item.lookahead == grammar->eof_symbol) {
                set_action(table, state, grammar->eof_symbol, ACTION_ACCEPT, 0);
            } else {
                set_action(table, state, item.lookahead, ACTION_REDUCE, item.production);
            }
        }
    }

    free_first_sets(first, grammar->symbol_count);
    return table;
}

static int find_terminal(const SyntaxGrammar *grammar, const char *name) {
    return find_symbol(grammar, name, SYMBOL_TERMINAL);
}

static void fill_expected(const SyntaxTable *table, SyntaxResult *result, int state) {
    const SyntaxGrammar *grammar = table->grammar;
    result->state = state;
    result->expected = xcalloc((size_t)grammar->symbol_count, sizeof(char *));
    for (int symbol = 0; symbol < grammar->symbol_count; symbol++) {
        if (!symbol_is_terminal(grammar, symbol)) continue;
        if (table->actions[state][symbol].kind == ACTION_NONE) continue;
        result->expected[result->expected_count++] = grammar->symbols[symbol].name;
    }
}

SyntaxResult syntax_parse_token_names(
    SyntaxTable *table,
    const char **token_names,
    size_t token_count
) {
    SyntaxResult result = {0};
    result.status = SYNTAX_ERROR;
    const SyntaxGrammar *grammar = table->grammar;

    int stack_cap = 128;
    int stack_len = 1;
    int *stack = xcalloc((size_t)stack_cap, sizeof(int));
    stack[0] = 0;

    size_t index = 0;
    for (;;) {
        int state = stack[stack_len - 1];
        int lookahead = grammar->eof_symbol;
        if (index < token_count) {
            lookahead = find_terminal(grammar, token_names[index]);
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
                stack = xrealloc(stack, (size_t)stack_cap * sizeof(int));
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
                stack = xrealloc(stack, (size_t)stack_cap * sizeof(int));
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

size_t syntax_table_state_count(const SyntaxTable *table) {
    return (size_t)table->state_count;
}

int syntax_table_conflict_count(const SyntaxTable *table) {
    return table->conflict_count;
}

void syntax_print_table(const SyntaxTable *table) {
    const SyntaxGrammar *grammar = table->grammar;
    printf("states: %d\n", table->state_count);
    printf("conflicts: %d\n", table->conflict_count);
    for (int state = 0; state < table->state_count; state++) {
        printf("state %d\n", state);
        for (int item_index = 0; item_index < table->states[state].count; item_index++) {
            LR1Item item = table->states[state].items[item_index];
            const Production *production = &grammar->productions[item.production];
            printf("  %s ->", grammar->symbols[production->lhs].name);
            for (int i = 0; i < production->rhs_len; i++) {
                if (i == item.dot) printf(" .");
                printf(" %s", grammar->symbols[production->rhs[i]].name);
            }
            if (item.dot == production->rhs_len) printf(" .");
            printf(", %s\n", grammar->symbols[item.lookahead].name);
        }
    }
}

void syntax_free_table(SyntaxTable *table) {
    if (!table) return;
    for (int i = 0; i < table->state_count; i++) free(table->states[i].items);
    free(table->states);
    if (table->actions) {
        for (int i = 0; i < table->row_cap; i++) free(table->actions[i]);
        free(table->actions);
    }
    if (table->gotos) {
        for (int i = 0; i < table->row_cap; i++) free(table->gotos[i]);
        free(table->gotos);
    }
    free(table);
}

void syntax_free_grammar(SyntaxGrammar *grammar) {
    if (!grammar) return;
    for (int i = 0; i < grammar->symbol_count; i++) free(grammar->symbols[i].name);
    free(grammar->symbols);
    for (int i = 0; i < grammar->production_count; i++) free(grammar->productions[i].rhs);
    free(grammar->productions);
    free(grammar);
}
