#ifndef SYNTAX_ENGINE_INTERNAL_H
#define SYNTAX_ENGINE_INTERNAL_H

#include "mylang_syntax_engine/syntax_engine.h"

#include <stdbool.h>
#include <stddef.h>

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

char *lr1_xstrdup(const char *s);
void *lr1_xcalloc(size_t count, size_t size);
void *lr1_xrealloc(void *ptr, size_t size);

char *lr1_trim(char *s);
int lr1_find_symbol(const SyntaxGrammar *grammar, const char *name, SymbolKind kind);
bool lr1_symbol_is_terminal(const SyntaxGrammar *grammar, int symbol);

#endif
