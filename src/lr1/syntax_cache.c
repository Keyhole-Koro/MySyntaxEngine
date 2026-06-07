#include "syntax_engine_internal.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYNTAX_TABLE_CACHE_MAGIC "MLR1TAB"
#define SYNTAX_TABLE_CACHE_VERSION 1

static bool write_int(FILE *file, int value) {
    return fwrite(&value, sizeof(value), 1, file) == 1;
}

static bool read_int(FILE *file, int *value) {
    return fread(value, sizeof(*value), 1, file) == 1;
}

static bool write_string(FILE *file, const char *value) {
    int len = (int)strlen(value);
    return write_int(file, len) && fwrite(value, 1, (size_t)len, file) == (size_t)len;
}

static bool read_and_match_string(FILE *file, const char *expected) {
    int len = 0;
    if (!read_int(file, &len) || len < 0) return false;
    size_t expected_len = strlen(expected);
    if ((size_t)len != expected_len) return false;

    char buffer[256];
    if ((size_t)len >= sizeof(buffer)) return false;
    if (fread(buffer, 1, (size_t)len, file) != (size_t)len) return false;
    buffer[len] = '\0';
    return strcmp(buffer, expected) == 0;
}

static bool write_grammar_signature(FILE *file, const SyntaxGrammar *grammar) {
    if (!write_int(file, grammar->symbol_count) ||
        !write_int(file, grammar->production_count) ||
        !write_int(file, grammar->start_symbol) ||
        !write_int(file, grammar->eof_symbol)) {
        return false;
    }

    for (int i = 0; i < grammar->symbol_count; i++) {
        if (!write_int(file, (int)grammar->symbols[i].kind) ||
            !write_string(file, grammar->symbols[i].name)) {
            return false;
        }
    }

    for (int i = 0; i < grammar->production_count; i++) {
        const Production *production = &grammar->productions[i];
        if (!write_int(file, production->lhs) || !write_int(file, production->rhs_len)) {
            return false;
        }
        for (int j = 0; j < production->rhs_len; j++) {
            if (!write_int(file, production->rhs[j])) return false;
        }
    }

    return true;
}

static bool read_and_match_grammar_signature(FILE *file, const SyntaxGrammar *grammar) {
    int symbol_count = 0;
    int production_count = 0;
    int start_symbol = 0;
    int eof_symbol = 0;
    if (!read_int(file, &symbol_count) ||
        !read_int(file, &production_count) ||
        !read_int(file, &start_symbol) ||
        !read_int(file, &eof_symbol)) {
        return false;
    }
    if (symbol_count != grammar->symbol_count ||
        production_count != grammar->production_count ||
        start_symbol != grammar->start_symbol ||
        eof_symbol != grammar->eof_symbol) {
        return false;
    }

    for (int i = 0; i < grammar->symbol_count; i++) {
        int kind = 0;
        if (!read_int(file, &kind) || kind != (int)grammar->symbols[i].kind) return false;
        if (!read_and_match_string(file, grammar->symbols[i].name)) return false;
    }

    for (int i = 0; i < grammar->production_count; i++) {
        int lhs = 0;
        int rhs_len = 0;
        const Production *production = &grammar->productions[i];
        if (!read_int(file, &lhs) || !read_int(file, &rhs_len)) return false;
        if (lhs != production->lhs || rhs_len != production->rhs_len) return false;
        for (int j = 0; j < production->rhs_len; j++) {
            int rhs_symbol = 0;
            if (!read_int(file, &rhs_symbol) || rhs_symbol != production->rhs[j]) return false;
        }
    }

    return true;
}

int syntax_save_lr1_table(const SyntaxTable *table, const char *path) {
    FILE *file = fopen(path, "wb");
    if (!file) return 0;

    int version = SYNTAX_TABLE_CACHE_VERSION;
    bool ok = fwrite(SYNTAX_TABLE_CACHE_MAGIC, 1, 7, file) == 7 &&
        write_int(file, version) &&
        write_grammar_signature(file, table->grammar) &&
        write_int(file, table->state_count) &&
        write_int(file, table->conflict_count);

    const SyntaxGrammar *grammar = table->grammar;
    for (int state = 0; ok && state < table->state_count; state++) {
        for (int symbol = 0; ok && symbol < grammar->symbol_count; symbol++) {
            ok = write_int(file, (int)table->actions[state][symbol].kind) &&
                write_int(file, table->actions[state][symbol].value);
        }
        for (int symbol = 0; ok && symbol < grammar->symbol_count; symbol++) {
            ok = write_int(file, table->gotos[state][symbol]);
        }
    }

    fclose(file);
    return ok ? 1 : 0;
}

SyntaxTable *syntax_load_lr1_table(SyntaxGrammar *grammar, const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;

    char magic[7];
    int version = 0;
    bool ok = fread(magic, 1, sizeof(magic), file) == sizeof(magic) &&
        memcmp(magic, SYNTAX_TABLE_CACHE_MAGIC, sizeof(magic)) == 0 &&
        read_int(file, &version) &&
        version == SYNTAX_TABLE_CACHE_VERSION &&
        read_and_match_grammar_signature(file, grammar);

    int state_count = 0;
    int conflict_count = 0;
    if (ok) {
        ok = read_int(file, &state_count) &&
            read_int(file, &conflict_count) &&
            state_count > 0;
    }

    SyntaxTable *table = NULL;
    if (ok) {
        table = lr1_xcalloc(1, sizeof(SyntaxTable));
        table->grammar = grammar;
        table->state_count = state_count;
        table->row_cap = state_count;
        table->conflict_count = conflict_count;
        table->actions = lr1_xcalloc((size_t)table->row_cap, sizeof(ActionCell *));
        table->gotos = lr1_xcalloc((size_t)table->row_cap, sizeof(int *));

        for (int state = 0; ok && state < table->state_count; state++) {
            table->actions[state] = lr1_xcalloc((size_t)grammar->symbol_count, sizeof(ActionCell));
            table->gotos[state] = lr1_xcalloc((size_t)grammar->symbol_count, sizeof(int));
            for (int symbol = 0; ok && symbol < grammar->symbol_count; symbol++) {
                int kind = 0;
                int value = 0;
                ok = read_int(file, &kind) && read_int(file, &value);
                if (ok && (kind < ACTION_NONE || kind > ACTION_ACCEPT)) ok = false;
                table->actions[state][symbol].kind = (ActionKind)kind;
                table->actions[state][symbol].value = value;
            }
            for (int symbol = 0; ok && symbol < grammar->symbol_count; symbol++) {
                ok = read_int(file, &table->gotos[state][symbol]);
            }
        }
    }

    fclose(file);
    if (!ok) {
        syntax_free_table(table);
        return NULL;
    }
    return table;
}
