#include "syntax_engine_internal.h"

#include <stdio.h>
#include <stdlib.h>

size_t syntax_table_state_count(const SyntaxTable *table) {
    return (size_t)table->state_count;
}

int syntax_table_conflict_count(const SyntaxTable *table) {
    return table->conflict_count;
}

static const char *action_name(ActionKind kind) {
    switch (kind) {
        case ACTION_NONE: return "none";
        case ACTION_SHIFT: return "shift";
        case ACTION_REDUCE: return "reduce";
        case ACTION_ACCEPT: return "accept";
    }
    return "unknown";
}

static void print_production(const SyntaxGrammar *grammar, int production_id) {
    if (production_id < 0 || production_id >= grammar->production_count) {
        printf("<invalid production %d>", production_id);
        return;
    }

    const Production *production = &grammar->productions[production_id];
    printf("%s ->", grammar->symbols[production->lhs].name);
    for (int i = 0; i < production->rhs_len; i++) {
        printf(" %s", grammar->symbols[production->rhs[i]].name);
    }
    if (production->rhs_len == 0) printf(" <empty>");
}

static void print_action(const SyntaxGrammar *grammar, ActionCell action) {
    printf("%s", action_name(action.kind));
    if (action.kind == ACTION_SHIFT) {
        printf(" state %d", action.value);
    } else if (action.kind == ACTION_REDUCE) {
        printf(" ");
        print_production(grammar, action.value);
    }
}

void syntax_print_table(const SyntaxTable *table) {
    const SyntaxGrammar *grammar = table->grammar;
    printf("states: %d\n", table->state_count);
    printf("conflicts: %d\n", table->conflict_count);
    if (!table->states) {
        printf("items: unavailable for cached table\n");
        return;
    }
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

void syntax_print_conflicts(const SyntaxTable *table) {
    const SyntaxGrammar *grammar = table->grammar;
    printf("conflicts: %d\n", table->conflict_count);
    if (!table->conflicts || table->conflict_count == 0) return;

    for (int i = 0; i < table->conflict_count; i++) {
        const ConflictInfo *conflict = &table->conflicts[i];
        printf(
            "conflict %d: state %d, lookahead %s\n",
            i + 1,
            conflict->state,
            grammar->symbols[conflict->terminal].name
        );
        printf("  existing: ");
        print_action(grammar, conflict->existing);
        printf("\n");
        printf("  incoming: ");
        print_action(grammar, conflict->incoming);
        printf("\n");

        if (table->states) {
            printf("  state items:\n");
            for (int item_index = 0; item_index < table->states[conflict->state].count; item_index++) {
                LR1Item item = table->states[conflict->state].items[item_index];
                const Production *production = &grammar->productions[item.production];
                printf("    %s ->", grammar->symbols[production->lhs].name);
                for (int rhs_index = 0; rhs_index < production->rhs_len; rhs_index++) {
                    if (rhs_index == item.dot) printf(" .");
                    printf(" %s", grammar->symbols[production->rhs[rhs_index]].name);
                }
                if (item.dot == production->rhs_len) printf(" .");
                printf(", %s\n", grammar->symbols[item.lookahead].name);
            }
        } else {
            printf("  state items: unavailable for cached table\n");
        }
    }
}

void syntax_free_table(SyntaxTable *table) {
    if (!table) return;
    if (table->states) {
        for (int i = 0; i < table->state_count; i++) free(table->states[i].items);
    }
    free(table->states);
    if (table->actions) {
        for (int i = 0; i < table->row_cap; i++) free(table->actions[i]);
        free(table->actions);
    }
    if (table->gotos) {
        for (int i = 0; i < table->row_cap; i++) free(table->gotos[i]);
        free(table->gotos);
    }
    free(table->conflicts);
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
