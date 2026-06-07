#include "syntax_engine_internal.h"

#include <stdbool.h>
#include <stdlib.h>

static void add_first_symbol(const SyntaxGrammar *grammar, bool **first, int nonterminal, int terminal) {
    (void)grammar;
    first[nonterminal][terminal] = true;
}

static bool **compute_first_sets(const SyntaxGrammar *grammar) {
    bool **first = lr1_xcalloc((size_t)grammar->symbol_count, sizeof(bool *));
    for (int i = 0; i < grammar->symbol_count; i++) {
        first[i] = lr1_xcalloc((size_t)grammar->symbol_count, sizeof(bool));
    }

    bool changed;
    do {
        changed = false;
        for (int i = 0; i < grammar->production_count; i++) {
            const Production *production = &grammar->productions[i];
            if (production->rhs_len == 0) continue;
            int first_sym = production->rhs[0];
            if (lr1_symbol_is_terminal(grammar, first_sym)) {
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
        set->items = lr1_xrealloc(set->items, (size_t)set->cap * sizeof(LR1Item));
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
    if (lr1_symbol_is_terminal(grammar, symbol)) {
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
            if (lr1_symbol_is_terminal(grammar, next_symbol)) continue;

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

    table->actions = lr1_xrealloc(table->actions, (size_t)table->row_cap * sizeof(ActionCell *));
    table->gotos = lr1_xrealloc(table->gotos, (size_t)table->row_cap * sizeof(int *));
    for (int i = old_cap; i < table->row_cap; i++) {
        table->actions[i] = lr1_xcalloc((size_t)grammar->symbol_count, sizeof(ActionCell));
        table->gotos[i] = lr1_xcalloc((size_t)grammar->symbol_count, sizeof(int));
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
        table->states = lr1_xrealloc(table->states, (size_t)table->state_cap * sizeof(ItemSet));
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
    SyntaxTable *table = lr1_xcalloc(1, sizeof(SyntaxTable));
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
            if (lr1_symbol_is_terminal(grammar, symbol)) {
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
