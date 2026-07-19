#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "syntax_engine/semantic_actions.h"
#include "syntax_engine/syntax_engine.h"

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

static SyntaxSourceSpan span(
    size_t byte_start,
    size_t byte_end,
    size_t line_start,
    size_t column_start,
    size_t line_end,
    size_t column_end
) {
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
    int id = syntax_terminal_id(grammar, "id");
    int semicolon = syntax_terminal_id(grammar, ";");
    assert(id >= 0);
    assert(semicolon >= 0);

    char *root_value = malloc(8);
    assert(root_value);
    strcpy(root_value, "node");

    SyntaxInputToken tokens[] = {
        {
            .terminal_id = id,
            .lexeme = "item",
            .lexeme_length = 4,
            .span = span(0, 4, 1, 1, 1, 5),
            .value = root_value,
        },
        {
            .terminal_id = semicolon,
            .lexeme = ";",
            .lexeme_length = 1,
            .span = span(4, 5, 1, 5, 1, 6),
            .value = NULL,
        },
    };

    TestState state = {0};
    SyntaxActionSet actions = {
        .reduce = reduce_passthrough,
        .destroy = destroy_counted,
        .user_data = &state,
    };

    SyntaxActionResult result = syntax_parse_with_actions(table, tokens, 2, &actions);
    assert(result.syntax.status == SYNTAX_OK);
    assert(result.root == root_value);
    assert(result.root_span.byte_start == 0);
    assert(result.root_span.byte_end == 5);
    assert(result.root_span.line_start == 1);
    assert(result.root_span.column_start == 1);
    assert(result.root_span.line_end == 1);
    assert(result.root_span.column_end == 6);
    assert(state.reduce_count >= 2);
    assert(state.destroy_count == 0);

    syntax_action_result_free(&result);
    free(root_value);
}

static void test_error_destroys_shifted_values(SyntaxGrammar *grammar, SyntaxTable *table) {
    int id = syntax_terminal_id(grammar, "id");
    int num = syntax_terminal_id(grammar, "num");
    assert(id >= 0);
    assert(num >= 0);

    char *shifted_value = malloc(8);
    assert(shifted_value);
    strcpy(shifted_value, "owned");

    SyntaxInputToken tokens[] = {
        {
            .terminal_id = id,
            .lexeme = "item",
            .lexeme_length = 4,
            .span = span(0, 4, 1, 1, 1, 5),
            .value = shifted_value,
        },
        {
            .terminal_id = num,
            .lexeme = "42",
            .lexeme_length = 2,
            .span = span(5, 7, 1, 6, 1, 8),
            .value = NULL,
        },
    };

    TestState state = {0};
    SyntaxActionSet actions = {
        .reduce = reduce_passthrough,
        .destroy = destroy_counted,
        .user_data = &state,
    };

    SyntaxActionResult result = syntax_parse_with_actions(table, tokens, 2, &actions);
    assert(result.syntax.status == SYNTAX_ERROR);
    assert(result.syntax.token_index == 1);
    assert(state.destroy_count == 1);
    assert(result.root == NULL);

    syntax_action_result_free(&result);
}

static void test_invalid_terminal_is_reported(SyntaxTable *table) {
    SyntaxInputToken token = {
        .terminal_id = -1,
        .lexeme = "?",
        .lexeme_length = 1,
        .span = span(0, 1, 1, 1, 1, 2),
        .value = NULL,
    };

    SyntaxActionResult result = syntax_parse_with_actions(table, &token, 1, NULL);
    assert(result.syntax.status == SYNTAX_ERROR);
    assert(result.syntax.token_index == 0);
    assert(result.syntax.expected_count > 0);
    syntax_action_result_free(&result);
}

int main(void) {
    const char *fixture = "tests/fixtures/grammars/semantic_actions.txt";
    SyntaxGrammar *grammar = syntax_load_grammar(fixture);
    assert(grammar);

    SyntaxTable *table = syntax_build_lr1_table(grammar);
    assert(table);
    assert(syntax_table_conflict_count(table) == 0);

    test_success_returns_root_and_combined_span(grammar, table);
    test_error_destroys_shifted_values(grammar, table);
    test_invalid_terminal_is_reported(table);

    syntax_free_table(table);
    syntax_free_grammar(grammar);
    puts("semantic action tests passed");
    return 0;
}
