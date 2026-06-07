#include "syntax_engine_internal.h"

#include <stdio.h>
#include <stdlib.h>

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
