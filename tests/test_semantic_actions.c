#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "syntax_engine/semantic_actions.h"
#include "syntax_engine/syntax_engine.h"

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "semantic-action test failure: %s (%s:%d)\n", message, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

typedef struct {
    int reduce_count;
    int destroy_count;
} TestState;

static void *reduce_passthrough(const SyntaxReduceEvent *event, void *user_data) {
    TestState *state = user_data;
    state->reduce_count++;
    for (size_t i = 0; i < event->rhs_count; ++i) {
        if (event->rhs[i].value) return event->rhs[i].value;
    }
    return NULL;
}

static void destroy_counted(int symbol_id, void *value, void *user_data) {
    (void)symbol_id;
    TestState *state = user_data;
    state->destroy_count++;
    free(value);
}

static SyntaxSourceSpan span(size_t byte_start, size_t byte_end,
                             size_t line_start, size_t column_start,
                             size_t line_end, size_t column_end) {
    SyntaxSourceSpan value = {
        .byte_start = byte_start,
        .byte_end = byte_end,
        .line_start = line_start,
        .column_start = column_start,
        .line_end = line_end,
        .column_end = column_end,
    };
    return value;
}

static void test_success_returns_root_and_combined_span(SyntaxGrammar *grammar, SyntaxTable *table) {
    puts("test: success root and combined span");
    fflush(stdout);
    int id = syntax_terminal_id(grammar, "id");
    int semicolon = syntax_terminal_id(grammar, ";");
    CHECK(id >= 0, "id terminal must exist");
    CHECK(semicolon >= 0, "semicolon terminal must exist");

    char *root_value = malloc(8);
    CHECK(root_value != NULL, "root allocation");
    strcpy(root_value, "node");

    SyntaxInputToken tokens[] = {
        {.terminal_id = id, .lexeme = "item", .lexeme_length = 4,
         .span = span(0, 4, 1, 1, 1, 5), .value = root_value},
        {.terminal_id = semicolon, .lexeme = ";", .lexeme_length = 1,
         .span = span(4, 5, 1, 5, 1, 6), .value = NULL},
    };

    TestState state = {0};
    SyntaxActionSet actions = {
        .reduce = reduce_passthrough,
        .destroy = destroy_counted,
        .user_data = &state,
    };

    SyntaxActionResult result = syntax_parse_with_actions(table, tokens, 2, &actions);
    CHECK(result.syntax.status == SYNTAX_OK, "valid token stream must parse");
    CHECK(result.root == root_value, "accepted root must preserve callback value");
    CHECK(result.root_span.byte_start == 0, "root byte start");
    CHECK(result.root_span.byte_end == 5, "root byte end");
    CHECK(result.root_span.line_start == 1, "root line start");
    CHECK(result.root_span.column_start == 1, "root column start");
    CHECK(result.root_span.line_end == 1, "root line end");
    CHECK(result.root_span.column_end == 6, "root column end");
    CHECK(state.reduce_count >= 2, "reduce callback must run for value and program");
    CHECK(state.destroy_count == 0, "successful root must not be destroyed");

    syntax_action_result_free(&result);
    free(root_value);
}

static void test_error_destroys_shifted_values(SyntaxGrammar *grammar, SyntaxTable *table) {
    puts("test: error destroys shifted values");
    fflush(stdout);
    int id = syntax_terminal_id(grammar, "id");
    int num = syntax_terminal_id(grammar, "num");
    CHECK(id >= 0, "id terminal must exist");
    CHECK(num >= 0, "num terminal must exist");

    char *shifted_value = malloc(8);
    CHECK(shifted_value != NULL, "shifted allocation");
    strcpy(shifted_value, "owned");

    SyntaxInputToken tokens[] = {
        {.terminal_id = id, .lexeme = "item", .lexeme_length = 4,
         .span = span(0, 4, 1, 1, 1, 5), .value = shifted_value},
        {.terminal_id = num, .lexeme = "42", .lexeme_length = 2,
         .span = span(5, 7, 1, 6, 1, 8), .value = NULL},
    };

    TestState state = {0};
    SyntaxActionSet actions = {
        .reduce = reduce_passthrough,
        .destroy = destroy_counted,
        .user_data = &state,
    };

    SyntaxActionResult result = syntax_parse_with_actions(table, tokens, 2, &actions);
    CHECK(result.syntax.status == SYNTAX_ERROR, "invalid token stream must fail");
    CHECK(result.syntax.token_index == 1, "error must point at second token");
    CHECK(state.destroy_count == 1, "shifted owned value must be destroyed once");
    CHECK(result.root == NULL, "failed parse must not return root");
    syntax_action_result_free(&result);
}

static void test_invalid_terminal_is_reported(SyntaxTable *table) {
    puts("test: invalid terminal diagnostic");
    fflush(stdout);
    SyntaxInputToken token = {
        .terminal_id = -1, .lexeme = "?", .lexeme_length = 1,
        .span = span(0, 1, 1, 1, 1, 2), .value = NULL,
    };

    SyntaxActionResult result = syntax_parse_with_actions(table, &token, 1, NULL);
    CHECK(result.syntax.status == SYNTAX_ERROR, "invalid terminal must fail");
    CHECK(result.syntax.token_index == 0, "invalid terminal index");
    CHECK(result.syntax.expected_count > 0, "invalid terminal must report expectations");
    syntax_action_result_free(&result);
}

int main(void) {
    const char *fixture = "tests/fixtures/grammars/semantic_actions.txt";
    SyntaxGrammar *grammar = syntax_load_grammar(fixture);
    CHECK(grammar != NULL, "semantic action grammar must load");
    SyntaxTable *table = syntax_build_lr1_table(grammar);
    CHECK(table != NULL, "semantic action table must build");
    CHECK(syntax_table_conflict_count(table) == 0, "semantic action grammar must be conflict free");

    test_success_returns_root_and_combined_span(grammar, table);
    test_error_destroys_shifted_values(grammar, table);
    test_invalid_terminal_is_reported(table);

    syntax_free_table(table);
    syntax_free_grammar(grammar);
    puts("semantic action tests passed");
    return 0;
}
