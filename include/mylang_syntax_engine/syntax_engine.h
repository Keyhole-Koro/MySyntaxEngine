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

SyntaxGrammar *syntax_load_grammar(const char *path);
SyntaxTable *syntax_build_lr1_table(SyntaxGrammar *grammar);

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

void syntax_result_free(SyntaxResult *result);
size_t syntax_table_state_count(const SyntaxTable *table);
int syntax_table_conflict_count(const SyntaxTable *table);
void syntax_print_table(const SyntaxTable *table);
void syntax_free_table(SyntaxTable *table);
void syntax_free_grammar(SyntaxGrammar *grammar);

#endif
