#include <limits.h>
#include <stdbool.h>
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

/* Parse `first bad` through both entry points: each must stop at `bad` with
   the same diagnostic instead of looking `bad` up in the table. */
static void expect_rejected_after(SyntaxTable *table, int first, int bad) {
    int token_ids[] = {first, bad};
    SyntaxResult syntax = syntax_parse_token_ids(table, token_ids, 2);
    CHECK(syntax.status == SYNTAX_ERROR, "token-id parse must reject a non-terminal id");
    CHECK(syntax.token_index == 1, "token-id parse must stop at the rejected id");
    CHECK(syntax.expected_count > 0, "token-id rejection must report expectations");

    SyntaxInputToken tokens[] = {
        {.terminal_id = first, .lexeme = "item", .lexeme_length = 4,
         .span = span(0, 4, 1, 1, 1, 5), .value = NULL},
        {.terminal_id = bad, .lexeme = "?", .lexeme_length = 1,
         .span = span(5, 6, 1, 6, 1, 7), .value = NULL},
    };
    SyntaxActionResult actions = syntax_parse_with_actions(table, tokens, 2, NULL);
    CHECK(actions.syntax.status == syntax.status, "both parsers must reject the id");
    CHECK(actions.syntax.token_index == syntax.token_index, "both parsers must stop at the id");
    CHECK(actions.syntax.state == syntax.state, "both parsers must stop in the same state");
    CHECK(actions.syntax.expected_count == syntax.expected_count, "both parsers must expect the same");
    for (size_t i = 0; i < syntax.expected_count; ++i) {
        CHECK(strcmp(actions.syntax.expected[i], syntax.expected[i]) == 0,
              "both parsers must expect the same terminals");
    }
    syntax_action_result_free(&actions);
    syntax_result_free(&syntax);
}

static void test_non_terminal_ids_are_rejected(SyntaxGrammar *grammar, SyntaxTable *table) {
    puts("test: non-terminal token ids are rejected");
    fflush(stdout);
    const char *names[] = {"$", ";", "id", "num"};
    int terminals[sizeof(names) / sizeof(names[0])];
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        terminals[i] = syntax_terminal_id(grammar, names[i]);
        CHECK(terminals[i] >= 0, "fixture terminal must exist");
    }

    /* The table has no column for an id past the grammar's last symbol. */
    expect_rejected_after(table, terminals[2], INT_MAX);

    /* Every other small id is one of the fixture's nonterminals or lies past
       its last symbol. */
    for (int bad = -1; bad < 64; ++bad) {
        bool terminal = false;
        for (size_t i = 0; i < sizeof(terminals) / sizeof(terminals[0]); ++i) {
            if (terminals[i] == bad) terminal = true;
        }
        if (!terminal) expect_rejected_after(table, terminals[2], bad);
    }
}

/* Records each reduction and owns values the way an AST builder would: it
   frees the right-hand side's values and returns a new one for the left. */
typedef struct {
    size_t rhs_count;
    bool rhs_is_null;
    SyntaxSourceSpan span;
} RecordedReduce;

typedef struct {
    RecordedReduce reduces[8];
    int reduce_count;
    int allocated;
    int freed;
    int destroyed;
} EmptyState;

static void *reduce_recording(const SyntaxReduceEvent *event, void *user_data) {
    EmptyState *state = user_data;
    CHECK(state->reduce_count < (int)(sizeof(state->reduces) / sizeof(state->reduces[0])),
          "too many reductions");
    RecordedReduce *recorded = &state->reduces[state->reduce_count++];
    recorded->rhs_count = event->rhs_count;
    recorded->rhs_is_null = event->rhs == NULL;
    recorded->span = event->span;
    for (size_t i = 0; event->rhs && i < event->rhs_count; ++i) {
        if (event->rhs[i].value) {
            free(event->rhs[i].value);
            state->freed++;
        }
    }
    void *node = malloc(1);
    CHECK(node != NULL, "node allocation");
    state->allocated++;
    return node;
}

static void destroy_recorded(int symbol_id, void *value, void *user_data) {
    (void)symbol_id;
    EmptyState *state = user_data;
    state->destroyed++;
    free(value);
}

static bool same_span(SyntaxSourceSpan a, SyntaxSourceSpan b) {
    return a.byte_start == b.byte_start && a.byte_end == b.byte_end &&
        a.line_start == b.line_start && a.column_start == b.column_start &&
        a.line_end == b.line_end && a.column_end == b.column_end;
}

static void *token_value(EmptyState *state) {
    void *value = malloc(1);
    CHECK(value != NULL, "token value allocation");
    state->allocated++;
    return value;
}

static SyntaxActionResult parse_empty(SyntaxTable *table, SyntaxInputToken *tokens, size_t count,
                                      EmptyState *state) {
    SyntaxActionSet actions = {
        .reduce = reduce_recording,
        .destroy = destroy_recorded,
        .user_data = state,
    };
    return syntax_parse_with_actions(table, tokens, count, &actions);
}

static void test_empty_reduction_at_lookahead(SyntaxGrammar *grammar, SyntaxTable *table) {
    puts("test: empty reduction spans zero width at the lookahead");
    fflush(stdout);
    EmptyState state = {0};
    SyntaxInputToken tokens[] = {
        {.terminal_id = syntax_terminal_id(grammar, "num"), .lexeme = "42", .lexeme_length = 2,
         .span = span(3, 5, 2, 4, 2, 6), .value = token_value(&state)},
    };

    SyntaxActionResult result = parse_empty(table, tokens, 1, &state);
    CHECK(result.syntax.status == SYNTAX_OK, "num alone must parse");
    CHECK(state.reduce_count >= 3, "head, tail and program must reduce");

    /* head -> <empty>, before num is shifted. */
    CHECK(state.reduces[0].rhs_count == 0, "head is empty");
    CHECK(state.reduces[0].rhs_is_null, "an empty reduction has no rhs array");
    CHECK(same_span(state.reduces[0].span, span(3, 3, 2, 4, 2, 4)), "empty head sits at num's start");
    /* tail -> num */
    CHECK(state.reduces[1].rhs_count == 1, "tail holds num");
    CHECK(same_span(state.reduces[1].span, span(3, 5, 2, 4, 2, 6)), "tail spans num");
    /* program -> head tail $, from the empty head to the end of input. */
    CHECK(state.reduces[2].rhs_count == 3, "program holds head, tail and $");
    CHECK(same_span(state.reduces[2].span, span(3, 5, 2, 4, 2, 6)), "program starts at the empty head");
    CHECK(same_span(result.root_span, span(3, 5, 2, 4, 2, 6)), "root span");

    CHECK(result.root != NULL, "the root is the program node");
    free(result.root);
    state.freed++;
    CHECK(state.destroyed == 0, "a successful parse destroys nothing");
    CHECK(state.allocated == state.freed, "every value is freed exactly once");
    syntax_action_result_free(&result);
}

static void test_empty_reduction_at_end_of_input(SyntaxGrammar *grammar, SyntaxTable *table) {
    puts("test: empty reduction at the end of input");
    fflush(stdout);
    EmptyState state = {0};
    SyntaxInputToken tokens[] = {
        {.terminal_id = syntax_terminal_id(grammar, "id"), .lexeme = "x", .lexeme_length = 1,
         .span = span(0, 1, 1, 1, 1, 2), .value = token_value(&state)},
    };

    SyntaxActionResult result = parse_empty(table, tokens, 1, &state);
    CHECK(result.syntax.status == SYNTAX_OK, "id alone must parse");
    CHECK(state.reduce_count >= 3, "head, tail and program must reduce");
    CHECK(state.reduces[0].rhs_count == 1, "head holds id");
    /* tail -> <empty> at the end of input: zero width where id ends. */
    CHECK(state.reduces[1].rhs_count == 0 && state.reduces[1].rhs_is_null, "tail is empty");
    CHECK(same_span(state.reduces[1].span, span(1, 1, 1, 2, 1, 2)), "empty tail sits after the last token");
    CHECK(same_span(result.root_span, span(0, 1, 1, 1, 1, 2)), "root spans id");

    free(result.root);
    state.freed++;
    CHECK(state.allocated == state.freed && state.destroyed == 0, "every value is freed exactly once");
    syntax_action_result_free(&result);

    /* With no tokens at all, every span is the empty span at the start. */
    EmptyState none = {0};
    result = parse_empty(table, NULL, 0, &none);
    CHECK(result.syntax.status == SYNTAX_OK, "empty input must parse");
    CHECK(none.reduce_count >= 3, "head, tail and program must reduce");
    CHECK(none.reduces[0].rhs_count == 0 && none.reduces[1].rhs_count == 0, "both are empty");
    for (int i = 0; i < 3; ++i) {
        CHECK(same_span(none.reduces[i].span, span(0, 0, 0, 0, 0, 0)), "empty input spans are zero");
    }
    free(result.root);
    none.freed++;
    CHECK(none.allocated == none.freed && none.destroyed == 0, "empty input frees every node once");
    syntax_action_result_free(&result);
}

static void test_error_destroys_empty_reduction_value(SyntaxGrammar *grammar, SyntaxTable *table) {
    puts("test: error destroys the value of an empty reduction");
    fflush(stdout);
    EmptyState state = {0};
    SyntaxInputToken tokens[] = {
        {.terminal_id = syntax_terminal_id(grammar, "num"), .lexeme = "42", .lexeme_length = 2,
         .span = span(0, 2, 1, 1, 1, 3), .value = token_value(&state)},
        {.terminal_id = syntax_terminal_id(grammar, "id"), .lexeme = "x", .lexeme_length = 1,
         .span = span(3, 4, 1, 4, 1, 5), .value = token_value(&state)},
    };

    SyntaxActionResult result = parse_empty(table, tokens, 2, &state);
    CHECK(result.syntax.status == SYNTAX_ERROR, "id after num must fail");
    CHECK(result.syntax.token_index == 1, "the error is at id");
    CHECK(result.root == NULL, "a failed parse has no root");
    /* The empty head's node and num are still on the stack; id was never
       shifted, so it stays the caller's. */
    CHECK(state.reduce_count == 1 && state.reduces[0].rhs_count == 0, "only the empty head reduced");
    CHECK(state.destroyed == 2, "head's node and num are destroyed");
    free(tokens[1].value);
    state.freed++;
    CHECK(state.allocated == state.freed + state.destroyed, "every value is freed exactly once");
    syntax_action_result_free(&result);
}

static void test_empty_reductions(void) {
    SyntaxGrammar *grammar = syntax_load_grammar("tests/fixtures/grammars/semantic_actions_empty.txt");
    CHECK(grammar != NULL, "empty-rule grammar must load");
    SyntaxTable *table = syntax_build_lr1_table(grammar);
    CHECK(syntax_table_conflict_count(table) == 0, "empty-rule grammar must be conflict free");
    CHECK(syntax_terminal_id(grammar, "id") >= 0 && syntax_terminal_id(grammar, "num") >= 0,
          "fixture terminals must exist");

    test_empty_reduction_at_lookahead(grammar, table);
    test_empty_reduction_at_end_of_input(grammar, table);
    test_error_destroys_empty_reduction_value(grammar, table);

    syntax_free_table(table);
    syntax_free_grammar(grammar);
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
    test_non_terminal_ids_are_rejected(grammar, table);

    syntax_free_table(table);
    syntax_free_grammar(grammar);

    test_empty_reductions();
    puts("semantic action tests passed");
    return 0;
}
