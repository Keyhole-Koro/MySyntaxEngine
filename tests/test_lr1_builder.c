#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "syntax_engine/syntax_engine.h"

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "LR(1) builder test failure: %s (%s:%d)\n", message, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define WIDE_DIR "build/test-lr1-builder"
#define WIDE_PATH WIDE_DIR "/wide.txt"
#define WIDE_TERMINALS 300

static void expect_status(SyntaxTable *table, const char *name, const char **tokens, size_t count,
                          SyntaxStatus status, size_t token_index) {
    SyntaxResult result = syntax_parse_token_names(table, tokens, count);
    if (result.status != status || result.token_index != token_index) {
        fprintf(stderr, "%s: expected status %d at token %zu, got %d at token %zu\n",
                name, status, token_index, result.status, result.token_index);
        syntax_result_free(&result);
        exit(1);
    }
    syntax_result_free(&result);
}

#define COUNT(tokens) (sizeof(tokens) / sizeof((tokens)[0]))
/* These grammars shift the end of input, so an accepted parse stops past it. */
#define EXPECT_OK(table, tokens) \
    expect_status(table, #tokens, tokens, COUNT(tokens), SYNTAX_OK, COUNT(tokens) + 1)
#define EXPECT_STOP(table, tokens, status, index) \
    expect_status(table, #tokens, tokens, COUNT(tokens), status, index)

static SyntaxTable *build(const char *path, SyntaxGrammar **out_grammar) {
    SyntaxGrammar *grammar = syntax_load_grammar(path);
    CHECK(grammar != NULL, path);
    SyntaxTable *table = syntax_build_lr1_table(grammar);
    CHECK(table != NULL, "table must build");
    CHECK(syntax_table_conflict_count(table) == 0, "grammar must have no conflicts");
    *out_grammar = grammar;
    return table;
}

static void test_nullable_suffix(void) {
    puts("test: lookahead past a nullable suffix");
    SyntaxGrammar *grammar = NULL;
    SyntaxTable *table = build("tests/fixtures/grammars/nullable.txt", &grammar);

    const char *neither[] = {"x", "z"};
    const char *only_a[] = {"x", "a", "z"};
    const char *only_b[] = {"x", "b", "z"};
    const char *both[] = {"x", "a", "b", "z"};
    EXPECT_OK(table, neither);
    EXPECT_OK(table, only_a);
    EXPECT_OK(table, only_b);
    EXPECT_OK(table, both);

    const char *swapped[] = {"x", "b", "a", "z"};
    const char *twice[] = {"x", "a", "a", "z"};
    const char *no_x[] = {"z"};
    const char *no_z[] = {"x", "a", "b"};
    EXPECT_STOP(table, swapped, SYNTAX_ERROR, 2);
    EXPECT_STOP(table, twice, SYNTAX_ERROR, 2);
    EXPECT_STOP(table, no_x, SYNTAX_ERROR, 0);
    EXPECT_STOP(table, no_z, SYNTAX_INCOMPLETE, 3);

    syntax_free_table(table);
    syntax_free_grammar(grammar);
}

static void test_first_past_nullable_prefix(void) {
    puts("test: FIRST past a nullable prefix");
    SyntaxGrammar *grammar = NULL;
    SyntaxTable *table = build("tests/fixtures/grammars/nullable_first.txt", &grammar);

    const char *without_a[] = {"y", "b", "c"};
    const char *with_a[] = {"y", "a", "b", "c"};
    EXPECT_OK(table, without_a);
    EXPECT_OK(table, with_a);

    const char *no_b[] = {"y", "c"};
    const char *a_no_b[] = {"y", "a", "c"};
    const char *no_y[] = {"b", "c"};
    EXPECT_STOP(table, no_b, SYNTAX_ERROR, 1);
    EXPECT_STOP(table, a_no_b, SYNTAX_ERROR, 2);
    EXPECT_STOP(table, no_y, SYNTAX_ERROR, 0);

    syntax_free_table(table);
    syntax_free_grammar(grammar);
}

/* S : X T $, X : 'x', T : 't0' | ... | 't299'. X reduces on any of the 300
   terminals, more than an earlier fixed-size lookahead list held. */
static void test_wide_first_set(void) {
    puts("test: a FIRST set of 300 terminals");
    mkdir("build", 0777);
    mkdir(WIDE_DIR, 0777);
    FILE *out = fopen(WIDE_PATH, "w");
    CHECK(out != NULL, "wide grammar must open");
    fputs("S : X T $\nX : 'x'\nT : 't0'\n", out);
    for (int i = 1; i < WIDE_TERMINALS; i++) fprintf(out, "  | 't%d'\n", i);
    CHECK(fclose(out) == 0, "wide grammar must write");

    SyntaxGrammar *grammar = NULL;
    SyntaxTable *table = build(WIDE_PATH, &grammar);
    const char *first[] = {"x", "t0"};
    const char *last[] = {"x", "t299"};
    const char *missing_x[] = {"t299"};
    EXPECT_OK(table, first);
    EXPECT_OK(table, last);
    EXPECT_STOP(table, missing_x, SYNTAX_ERROR, 0);

    syntax_free_table(table);
    syntax_free_grammar(grammar);
    remove(WIDE_PATH);
}

static void test_known_tables(void) {
    puts("test: known grammars keep their tables");
    SyntaxGrammar *grammar = syntax_load_grammar("tests/fixtures/grammars/sample2.txt");
    CHECK(grammar != NULL, "sample2 must load");
    SyntaxTable *table = syntax_build_lr1_table(grammar);
    CHECK(syntax_table_state_count(table) == 46, "sample2 has 46 states");
    CHECK(syntax_table_conflict_count(table) == 0, "sample2 has no conflicts");
    syntax_free_table(table);
    syntax_free_grammar(grammar);
}

int main(void) {
    test_nullable_suffix();
    test_first_past_nullable_prefix();
    test_wide_first_set();
    test_known_tables();
    puts("LR(1) builder tests passed");
    return 0;
}
