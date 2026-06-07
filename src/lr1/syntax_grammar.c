#include "syntax_engine_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           (c >= '0' && c <= '9');
}

int lr1_find_symbol(const SyntaxGrammar *grammar, const char *name, SymbolKind kind) {
    for (int i = 0; i < grammar->symbol_count; i++) {
        if (grammar->symbols[i].kind == kind && strcmp(grammar->symbols[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int add_symbol(SyntaxGrammar *grammar, const char *name, SymbolKind kind) {
    int found = lr1_find_symbol(grammar, name, kind);
    if (found >= 0) return found;

    if (grammar->symbol_count == grammar->symbol_cap) {
        grammar->symbol_cap = grammar->symbol_cap ? grammar->symbol_cap * 2 : 32;
        grammar->symbols = lr1_xrealloc(grammar->symbols, (size_t)grammar->symbol_cap * sizeof(SymbolDef));
    }

    int id = grammar->symbol_count++;
    grammar->symbols[id].name = lr1_xstrdup(name);
    grammar->symbols[id].kind = kind;
    return id;
}

static void add_production(SyntaxGrammar *grammar, int lhs, const int *rhs, int rhs_len) {
    if (grammar->production_count == grammar->production_cap) {
        grammar->production_cap = grammar->production_cap ? grammar->production_cap * 2 : 32;
        grammar->productions = lr1_xrealloc(
            grammar->productions,
            (size_t)grammar->production_cap * sizeof(Production)
        );
    }

    Production *production = &grammar->productions[grammar->production_count++];
    production->lhs = lhs;
    production->rhs_len = rhs_len;
    production->rhs = lr1_xcalloc((size_t)rhs_len, sizeof(int));
    memcpy(production->rhs, rhs, (size_t)rhs_len * sizeof(int));
}

static bool parse_rhs(SyntaxGrammar *grammar, char *text, int **out_rhs, int *out_len) {
    int cap = 8;
    int len = 0;
    int *rhs = lr1_xcalloc((size_t)cap, sizeof(int));
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
            rhs = lr1_xrealloc(rhs, (size_t)cap * sizeof(int));
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

    SyntaxGrammar *grammar = lr1_xcalloc(1, sizeof(SyntaxGrammar));
    grammar->start_symbol = -1;
    grammar->eof_symbol = add_symbol(grammar, "$", SYMBOL_TERMINAL);

    char line[1024];
    int current_lhs = -1;

    while (fgets(line, sizeof(line), file)) {
        char *text = lr1_trim(line);
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
            char *lhs_name = lr1_trim(text);
            if (*lhs_name == '\0') continue;
            current_lhs = add_symbol(grammar, lhs_name, SYMBOL_NONTERMINAL);
            if (grammar->start_symbol < 0) grammar->start_symbol = current_lhs;
            rhs_text = lr1_trim(colon + 1);
        } else if (pipe) {
            if (current_lhs < 0) {
                fprintf(stderr, "grammar alternative without lhs: %s\n", path);
                syntax_free_grammar(grammar);
                fclose(file);
                return NULL;
            }
            rhs_text = lr1_trim(pipe + 1);
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

bool lr1_symbol_is_terminal(const SyntaxGrammar *grammar, int symbol) {
    return grammar->symbols[symbol].kind == SYMBOL_TERMINAL;
}
