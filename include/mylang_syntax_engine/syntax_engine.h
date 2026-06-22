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

/* Semantic role assigned to an input token from its grammar context during the
   LR1 parse. Used by the language server to classify identifiers (function vs
   type vs property...) without source-text heuristics. */
typedef enum {
    SYNTAX_ROLE_NONE = 0,
    SYNTAX_ROLE_FUNCTION,
    SYNTAX_ROLE_TYPE,
    SYNTAX_ROLE_STRUCT,
    SYNTAX_ROLE_NAMESPACE,
    SYNTAX_ROLE_PARAMETER,
    SYNTAX_ROLE_PROPERTY
} SyntaxRole;

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
   SyntaxRole inferred for that input token (SYNTAX_ROLE_NONE if unclassified). */
SyntaxResult syntax_parse_token_ids_roles(
    SyntaxTable *table,
    const int *token_ids,
    size_t token_count,
    int *out_roles
);

void syntax_result_free(SyntaxResult *result);
size_t syntax_table_state_count(const SyntaxTable *table);
int syntax_table_conflict_count(const SyntaxTable *table);
void syntax_print_table(const SyntaxTable *table);
void syntax_print_conflicts(const SyntaxTable *table);
void syntax_free_table(SyntaxTable *table);
void syntax_free_grammar(SyntaxGrammar *grammar);

#endif
