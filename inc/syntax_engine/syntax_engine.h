#ifndef SYNTAX_ENGINE_H
#define SYNTAX_ENGINE_H

#include <stddef.h>

typedef struct SyntaxGrammar SyntaxGrammar;
typedef struct SyntaxTable SyntaxTable;

typedef enum {
    SYNTAX_OK = 0,
    SYNTAX_ERROR,
    SYNTAX_INCOMPLETE
} SyntaxStatus;

typedef struct {
    SyntaxStatus status;
    size_t token_index;
    int state;
    const char **expected;
    size_t expected_count;
} SyntaxResult;

/* Token roles and symbol kinds are grammar-defined labels, not a fixed enum:
   the grammar's @role / @decl(kind) annotations declare arbitrary strings that
   the engine carries through as interned label ids (0 = none). Resolve an id to
   its string with syntax_label_name(). This keeps the engine language-agnostic. */

SyntaxGrammar *syntax_load_grammar(const char *path);
SyntaxTable *syntax_build_lr1_table(SyntaxGrammar *grammar);
SyntaxTable *syntax_load_lr1_table(SyntaxGrammar *grammar, const char *path);
int syntax_save_lr1_table(const SyntaxTable *table, const char *path);

int syntax_terminal_id(const SyntaxGrammar *grammar, const char *name);

SyntaxResult syntax_parse_token_names(
    SyntaxTable *table,
    const char **token_names,
    size_t token_count
);

SyntaxResult syntax_parse_token_ids(
    SyntaxTable *table,
    const int *token_ids,
    size_t token_count
);

/* Same as syntax_parse_token_ids, but if out_roles is non-NULL it must point to
   a token_count-sized buffer (caller-zeroed); each entry is filled with the
   grammar-defined role label id for that token (0 if unclassified; resolve with
   syntax_label_name). */
SyntaxResult syntax_parse_token_ids_roles(
    SyntaxTable *table,
    const int *token_ids,
    size_t token_count,
    int *out_roles
);

typedef struct {
    int token_index;   /* input token of the declared name */
    int kind;          /* grammar-defined label id (resolve via syntax_label_name) */
} SyntaxSymbol;

/* Resolve a label id (from out_roles entries or SyntaxSymbol.kind) to its
   grammar-declared string, or NULL for 0/none. */
const char *syntax_label_name(const SyntaxTable *table, int label_id);

/* Full analysis: fills out_roles (see above) and, if out_symbols is non-NULL,
   collects up to symbol_cap top-level declaration symbols (functions, structs,
   enums, typedefs, globals) into it, writing the count to *out_symbol_count.
   Either output buffer may be NULL to skip it. */
SyntaxResult syntax_parse_token_ids_ex(
    SyntaxTable *table,
    const int *token_ids,
    size_t token_count,
    int *out_roles,
    SyntaxSymbol *out_symbols,
    size_t symbol_cap,
    size_t *out_symbol_count
);

void syntax_result_free(SyntaxResult *result);
size_t syntax_table_state_count(const SyntaxTable *table);
int syntax_table_conflict_count(const SyntaxTable *table);
void syntax_print_table(const SyntaxTable *table);
void syntax_print_conflicts(const SyntaxTable *table);
void syntax_free_table(SyntaxTable *table);
void syntax_free_grammar(SyntaxGrammar *grammar);

#endif
