#ifndef SYNTAX_ENGINE_INTERNAL_H
#define SYNTAX_ENGINE_INTERNAL_H

#include "syntax_engine/syntax_engine.h"

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

/* Per-RHS-symbol annotation from the grammar file. Drives language-agnostic
   classification: role tags the symbol's leftmost token, decl declares a
   top-level outline symbol named by it. Both are label ids (0 = none). */
typedef struct {
    int role;      /* label id for token role, 0 = none */
    int decl;      /* label id for outline-symbol kind, 0 = none */
    bool single;   /* role only applies if the symbol spans exactly one token */
} RhsAnnot;

typedef struct {
    int lhs;
    int *rhs;
    int rhs_len;
    RhsAnnot *annot;   /* length rhs_len, or NULL if the production has no tags */
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

typedef struct {
    int state;
    int terminal;
    ActionCell existing;
    ActionCell incoming;
} ConflictInfo;

struct SyntaxGrammar {
    SymbolDef *symbols;
    int symbol_count;
    int symbol_cap;
    Production *productions;
    int production_count;
    int production_cap;
    int start_symbol;
    int eof_symbol;
    /* Interned annotation labels (role/symbol-kind strings). Label ids are
       1-based so 0 means "none". */
    char **labels;
    int label_count;
    int label_cap;
    /* Terminals that open/close a scope (for top-level symbol detection), or
       -1 if the grammar declares no %scope. */
    int scope_open;
    int scope_close;
};

int lr1_intern_label(SyntaxGrammar *grammar, const char *name);
const char *lr1_label_name(const SyntaxGrammar *grammar, int label_id);

struct SyntaxTable {
    SyntaxGrammar *grammar;
    ItemSet *states;
    int state_count;
    int state_cap;
    int row_cap;
    ActionCell **actions;
    int **gotos;
    int conflict_count;
    ConflictInfo *conflicts;
    int conflict_cap;
};

char *lr1_xstrdup(const char *s);
void *lr1_xcalloc(size_t count, size_t size);
void *lr1_xrealloc(void *ptr, size_t size);

char *lr1_trim(char *s);
int lr1_find_symbol(const SyntaxGrammar *grammar, const char *name, SymbolKind kind);
bool lr1_symbol_is_terminal(const SyntaxGrammar *grammar, int symbol);

#endif
