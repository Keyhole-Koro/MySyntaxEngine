#include "syntax_engine_internal.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define STATE_BUCKETS 65536
#define MAX_SUFFIX_FIRST 256

/* What building the table reads over and over, worked out once. A dot item is
   a production with a dot position, and a state is known by its kernel: the
   items a goto produced, before their closure. Two states with one closure have
   one kernel, because a closure only adds items with the dot at the start. */
typedef struct {
    const SyntaxGrammar *grammar;
    int *item_base;           /* production -> its first dot item */
    int **suffix_first;       /* dot item -> what follows its next symbol, or NULL
                                 when that is the item's own lookahead */
    int *suffix_first_counts;
    int **productions_of;     /* nonterminal -> its productions, in order */
    int *production_counts;
    int *seen;                /* (dot item, lookahead) -> last closure holding it */
    int closure_id;
    int *bucket_heads;        /* kernel hash -> first state, or -1 */
    int *next_in_bucket;      /* state -> next state in its bucket, or -1 */
    ItemSet *kernels;         /* state -> its kernel, sorted */
    int kernel_cap;
} Builder;

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

static void itemset_push(ItemSet *set, LR1Item item) {
    if (set->count == set->cap) {
        set->cap = set->cap ? set->cap * 2 : 16;
        set->items = lr1_xrealloc(set->items, (size_t)set->cap * sizeof(LR1Item));
    }
    set->items[set->count++] = item;
}

static int item_id(const Builder *builder, LR1Item item) {
    return builder->item_base[item.production] + item.dot;
}

/* True the first time this closure meets the item. */
static bool mark_seen(Builder *builder, LR1Item item) {
    size_t index = (size_t)item_id(builder, item) * (size_t)builder->grammar->symbol_count +
                   (size_t)item.lookahead;
    if (builder->seen[index] == builder->closure_id) return false;
    builder->seen[index] = builder->closure_id;
    return true;
}

static void set_suffix_first(Builder *builder, bool **first, int id,
                             const Production *production, int dot) {
    const SyntaxGrammar *grammar = builder->grammar;
    int after = dot + 1;
    if (after >= production->rhs_len) return;

    int symbol = production->rhs[after];
    int *terminals = lr1_xcalloc(MAX_SUFFIX_FIRST, sizeof(int));
    int count = 0;
    if (lr1_symbol_is_terminal(grammar, symbol)) {
        terminals[count++] = symbol;
    } else {
        for (int t = 0; t < grammar->symbol_count && count < MAX_SUFFIX_FIRST; t++) {
            if (first[symbol][t]) terminals[count++] = t;
        }
    }
    builder->suffix_first[id] = terminals;
    builder->suffix_first_counts[id] = count;
}

static void builder_init(Builder *builder, const SyntaxGrammar *grammar, bool **first) {
    memset(builder, 0, sizeof(*builder));
    builder->grammar = grammar;

    int item_count = 0;
    builder->item_base = lr1_xcalloc((size_t)grammar->production_count, sizeof(int));
    for (int p = 0; p < grammar->production_count; p++) {
        builder->item_base[p] = item_count;
        item_count += grammar->productions[p].rhs_len + 1;
    }

    builder->suffix_first = lr1_xcalloc((size_t)item_count, sizeof(int *));
    builder->suffix_first_counts = lr1_xcalloc((size_t)item_count, sizeof(int));
    for (int p = 0; p < grammar->production_count; p++) {
        const Production *production = &grammar->productions[p];
        for (int dot = 0; dot < production->rhs_len; dot++) {
            set_suffix_first(builder, first, builder->item_base[p] + dot, production, dot);
        }
    }

    builder->productions_of = lr1_xcalloc((size_t)grammar->symbol_count, sizeof(int *));
    builder->production_counts = lr1_xcalloc((size_t)grammar->symbol_count, sizeof(int));
    for (int p = 0; p < grammar->production_count; p++) {
        builder->production_counts[grammar->productions[p].lhs]++;
    }
    for (int symbol = 0; symbol < grammar->symbol_count; symbol++) {
        builder->productions_of[symbol] =
            lr1_xcalloc((size_t)builder->production_counts[symbol] + 1, sizeof(int));
        builder->production_counts[symbol] = 0;
    }
    for (int p = 0; p < grammar->production_count; p++) {
        int lhs = grammar->productions[p].lhs;
        builder->productions_of[lhs][builder->production_counts[lhs]++] = p;
    }

    builder->seen = lr1_xcalloc((size_t)item_count * (size_t)grammar->symbol_count, sizeof(int));
    builder->bucket_heads = lr1_xcalloc(STATE_BUCKETS, sizeof(int));
    for (int i = 0; i < STATE_BUCKETS; i++) builder->bucket_heads[i] = -1;
}

static void builder_free(Builder *builder, int state_count) {
    int item_count = 0;
    for (int p = 0; p < builder->grammar->production_count; p++) {
        item_count += builder->grammar->productions[p].rhs_len + 1;
    }
    for (int i = 0; i < item_count; i++) free(builder->suffix_first[i]);
    for (int i = 0; i < builder->grammar->symbol_count; i++) free(builder->productions_of[i]);
    for (int i = 0; i < state_count; i++) free(builder->kernels[i].items);
    free(builder->item_base);
    free(builder->suffix_first);
    free(builder->suffix_first_counts);
    free(builder->productions_of);
    free(builder->production_counts);
    free(builder->seen);
    free(builder->bucket_heads);
    free(builder->next_in_bucket);
    free(builder->kernels);
}

/* Items added while walking the set are walked too, so one pass finishes it. */
static void closure(Builder *builder, ItemSet *set) {
    const SyntaxGrammar *grammar = builder->grammar;
    builder->closure_id++;
    for (int i = 0; i < set->count; i++) mark_seen(builder, set->items[i]);

    for (int i = 0; i < set->count; i++) {
        LR1Item item = set->items[i];
        const Production *production = &grammar->productions[item.production];
        if (item.dot >= production->rhs_len) continue;

        int next_symbol = production->rhs[item.dot];
        if (lr1_symbol_is_terminal(grammar, next_symbol)) continue;

        int id = item_id(builder, item);
        const int *lookaheads = builder->suffix_first[id];
        int lookahead_count = builder->suffix_first_counts[id];
        if (!lookaheads) {
            lookaheads = &item.lookahead;
            lookahead_count = 1;
        }

        for (int p = 0; p < builder->production_counts[next_symbol]; p++) {
            for (int l = 0; l < lookahead_count; l++) {
                LR1Item next = {builder->productions_of[next_symbol][p], 0, lookaheads[l]};
                if (mark_seen(builder, next)) itemset_push(set, next);
            }
        }
    }
}

static int compare_items(const void *left, const void *right) {
    const LR1Item *a = left;
    const LR1Item *b = right;
    if (a->production != b->production) return a->production < b->production ? -1 : 1;
    if (a->dot != b->dot) return a->dot < b->dot ? -1 : 1;
    if (a->lookahead != b->lookahead) return a->lookahead < b->lookahead ? -1 : 1;
    return 0;
}

static unsigned int hash_items(const LR1Item *items, int count) {
    unsigned int hash = 2166136261u;
    for (int i = 0; i < count; i++) {
        hash = (hash ^ (unsigned int)items[i].production) * 16777619u;
        hash = (hash ^ (unsigned int)items[i].dot) * 16777619u;
        hash = (hash ^ (unsigned int)items[i].lookahead) * 16777619u;
    }
    return hash;
}

static int find_state(const Builder *builder, const LR1Item *kernel, int count, unsigned int hash) {
    for (int state = builder->bucket_heads[hash % STATE_BUCKETS]; state >= 0;
         state = builder->next_in_bucket[state]) {
        const ItemSet *known = &builder->kernels[state];
        if (known->hash == hash && known->count == count &&
            memcmp(known->items, kernel, (size_t)count * sizeof(LR1Item)) == 0) {
            return state;
        }
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

/* The state with this kernel, made if it is new. */
static int state_for_kernel(Builder *builder, SyntaxTable *table, const LR1Item *kernel, int count) {
    LR1Item *sorted = lr1_xcalloc((size_t)count, sizeof(LR1Item));
    memcpy(sorted, kernel, (size_t)count * sizeof(LR1Item));
    qsort(sorted, (size_t)count, sizeof(LR1Item), compare_items);
    unsigned int hash = hash_items(sorted, count);

    int found = find_state(builder, sorted, count, hash);
    if (found >= 0) {
        free(sorted);
        return found;
    }

    ItemSet set = {0};
    for (int i = 0; i < count; i++) itemset_push(&set, kernel[i]);
    closure(builder, &set);
    set.hash = hash;

    if (table->state_count == table->state_cap) {
        table->state_cap = table->state_cap ? table->state_cap * 2 : 32;
        table->states = lr1_xrealloc(table->states, (size_t)table->state_cap * sizeof(ItemSet));
    }
    if (table->state_count == builder->kernel_cap) {
        builder->kernel_cap = builder->kernel_cap ? builder->kernel_cap * 2 : 32;
        builder->kernels = lr1_xrealloc(builder->kernels,
                                        (size_t)builder->kernel_cap * sizeof(ItemSet));
        builder->next_in_bucket = lr1_xrealloc(builder->next_in_bucket,
                                               (size_t)builder->kernel_cap * sizeof(int));
    }

    int id = table->state_count++;
    table->states[id] = set;
    builder->kernels[id] = (ItemSet){.items = sorted, .count = count, .cap = count, .hash = hash};
    builder->next_in_bucket[id] = builder->bucket_heads[hash % STATE_BUCKETS];
    builder->bucket_heads[hash % STATE_BUCKETS] = id;
    ensure_table_rows(table, table->state_count);
    return id;
}

static void set_action(SyntaxTable *table, int state, int terminal, ActionKind kind, int value) {
    ActionCell *cell = &table->actions[state][terminal];
    if (cell->kind != ACTION_NONE && (cell->kind != kind || cell->value != value)) {
        if (table->conflict_count == table->conflict_cap) {
            table->conflict_cap = table->conflict_cap ? table->conflict_cap * 2 : 32;
            table->conflicts = lr1_xrealloc(table->conflicts, (size_t)table->conflict_cap * sizeof(ConflictInfo));
        }
        table->conflicts[table->conflict_count].state = state;
        table->conflicts[table->conflict_count].terminal = terminal;
        table->conflicts[table->conflict_count].existing = *cell;
        table->conflicts[table->conflict_count].incoming.kind = kind;
        table->conflicts[table->conflict_count].incoming.value = value;
        table->conflict_count++;
        return;
    }
    cell->kind = kind;
    cell->value = value;
}

/* Every state a state leads to, by symbol. A symbol's kernel is the items with
   that symbol after the dot, advanced past it, kept in the state's order. */
static void add_transitions(Builder *builder, SyntaxTable *table, int state,
                            int *group_starts, LR1Item **advanced, int *advanced_cap) {
    const SyntaxGrammar *grammar = builder->grammar;
    const LR1Item *items = table->states[state].items;
    int item_count = table->states[state].count;

    if (item_count > *advanced_cap) {
        *advanced_cap = item_count;
        *advanced = lr1_xrealloc(*advanced, (size_t)item_count * sizeof(LR1Item));
    }

    memset(group_starts, 0, (size_t)(grammar->symbol_count + 1) * sizeof(int));
    for (int i = 0; i < item_count; i++) {
        const Production *production = &grammar->productions[items[i].production];
        if (items[i].dot < production->rhs_len) group_starts[production->rhs[items[i].dot] + 1]++;
    }
    for (int symbol = 0; symbol < grammar->symbol_count; symbol++) {
        group_starts[symbol + 1] += group_starts[symbol];
    }

    int *filled = lr1_xcalloc((size_t)grammar->symbol_count, sizeof(int));
    for (int i = 0; i < item_count; i++) {
        const Production *production = &grammar->productions[items[i].production];
        if (items[i].dot >= production->rhs_len) continue;
        int symbol = production->rhs[items[i].dot];
        LR1Item next = {items[i].production, items[i].dot + 1, items[i].lookahead};
        (*advanced)[group_starts[symbol] + filled[symbol]++] = next;
    }

    for (int symbol = 0; symbol < grammar->symbol_count; symbol++) {
        if (filled[symbol] == 0) continue;
        int next_state = state_for_kernel(builder, table, *advanced + group_starts[symbol],
                                          filled[symbol]);
        if (lr1_symbol_is_terminal(grammar, symbol)) {
            set_action(table, state, symbol, ACTION_SHIFT, next_state);
        } else {
            table->gotos[state][symbol] = next_state;
        }
    }
    free(filled);
}

SyntaxTable *syntax_build_lr1_table(SyntaxGrammar *grammar) {
    bool **first = compute_first_sets(grammar);
    SyntaxTable *table = lr1_xcalloc(1, sizeof(SyntaxTable));
    table->grammar = grammar;

    ensure_table_rows(table, 1);

    Builder builder;
    builder_init(&builder, grammar, first);
    free_first_sets(first, grammar->symbol_count);

    LR1Item start_item = {0, 0, grammar->eof_symbol};
    state_for_kernel(&builder, table, &start_item, 1);

    int *group_starts = lr1_xcalloc((size_t)grammar->symbol_count + 1, sizeof(int));
    LR1Item *advanced = NULL;
    int advanced_cap = 0;

    for (int state = 0; state < table->state_count; state++) {
        add_transitions(&builder, table, state, group_starts, &advanced, &advanced_cap);

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

    free(group_starts);
    free(advanced);
    builder_free(&builder, table->state_count);
    return table;
}
