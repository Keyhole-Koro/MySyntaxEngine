#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "syntax_engine/syntax_engine.h"
/* White-box: %scope sets the grammar's scope terminals, which have no public
   accessor. */
#include "../src/lr1/syntax_engine_internal.h"

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "grammar loader test failure: %s (%s:%d)\n", message, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define GRAMMAR_DIR "build/test-grammar-loader"
#define GRAMMAR_PATH GRAMMAR_DIR "/grammar.txt"

static void write_grammar(const char *text) {
    FILE *out = fopen(GRAMMAR_PATH, "w");
    CHECK(out != NULL, "grammar file must open");
    CHECK(fputs(text, out) >= 0, "grammar file must write");
    CHECK(fclose(out) == 0, "grammar file must close");
}

static SyntaxGrammar *load_text(const char *text) {
    write_grammar(text);
    return syntax_load_grammar(GRAMMAR_PATH);
}

static void expect_rejected(const char *text, const char *message) {
    SyntaxGrammar *grammar = load_text(text);
    if (grammar) {
        syntax_free_grammar(grammar);
        CHECK(false, message);
    }
}

static void expect_loads(const char *text, const char *message) {
    SyntaxGrammar *grammar = load_text(text);
    CHECK(grammar != NULL, message);
    syntax_free_grammar(grammar);
}

/* `prefix`, then `fill` repeated to make the whole string `length` chars, then
   `suffix`. The caller frees it. */
static char *padded(const char *prefix, char fill, size_t length, const char *suffix) {
    size_t prefix_len = strlen(prefix);
    CHECK(length >= prefix_len, "padding must not be shorter than the prefix");
    char *text = malloc(length + strlen(suffix) + 1);
    CHECK(text != NULL, "text allocation");
    memcpy(text, prefix, prefix_len);
    memset(text + prefix_len, fill, length - prefix_len);
    strcpy(text + length, suffix);
    return text;
}

static void test_scope_directive(void) {
    puts("test: %scope takes exactly an open and a close terminal");
    expect_rejected("%scope LB\nS : 'a' $\n", "%scope with one terminal must not load");
    expect_rejected("%scope\nS : 'a' $\n", "%scope with no terminal must not load");
    expect_rejected("%scope LB RB EXTRA\nS : 'a' $\n", "%scope with three terminals must not load");

    SyntaxGrammar *grammar = load_text("%scope LB RB\nS : 'a' LB S RB $\n  | 'a' $\n");
    CHECK(grammar != NULL, "a valid %scope must load");
    CHECK(grammar->scope_open == syntax_terminal_id(grammar, "LB"), "%scope must set the open terminal");
    CHECK(grammar->scope_close == syntax_terminal_id(grammar, "RB"), "%scope must set the close terminal");
    CHECK(grammar->scope_open >= 0 && grammar->scope_close >= 0, "scope terminals must exist");
    syntax_free_grammar(grammar);

    grammar = load_text("%scope\tLB\tRB\nS : 'a' $\n");
    CHECK(grammar != NULL, "%scope separated by tabs must load");
    CHECK(grammar->scope_open == syntax_terminal_id(grammar, "LB"), "tab-separated open terminal");
    syntax_free_grammar(grammar);

    /* Other directives are still skipped, and %extend still adds a rule. */
    expect_loads("%scopes LB RB\n%unknown\nS : 'a' $\n", "unknown directives must be skipped");
    expect_loads("S : 'a' $\n%extend S : 'b' $\n", "%extend must still load");
}

static void test_line_length(void) {
    puts("test: a line longer than 1023 characters is rejected");
    /* A rule followed by a comment, 1023 characters before the newline. */
    char *longest = padded("S : 'a' $ //", 'x', 1023, "\n");
    expect_loads(longest, "a 1023-character line must load");
    free(longest);

    char *longest_at_eof = padded("S : 'a' $ //", 'x', 1023, "");
    expect_loads(longest_at_eof, "a 1023-character last line without a newline must load");
    free(longest_at_eof);

    char *longest_crlf = padded("S : 'a' $ //", 'x', 1023, "\r\n");
    expect_loads(longest_crlf, "a 1023-character line ending in CRLF must load");
    free(longest_crlf);

    char *too_long = padded("S : 'a' $ //", 'x', 1024, "\n");
    expect_rejected(too_long, "a 1024-character line must not load");
    free(too_long);

    char *too_long_crlf = padded("S : 'a' $ //", 'x', 1024, "\r\n");
    expect_rejected(too_long_crlf, "a 1024-character line ending in CRLF must not load");
    free(too_long_crlf);

    char *too_long_at_eof = padded("S : 'a' $ //", 'x', 1024, "");
    expect_rejected(too_long_at_eof, "a 1024-character last line must not load");
    free(too_long_at_eof);

    /* What was cut off used to be read as a line of its own. */
    char *split_rule = padded("S : 'a' $ //", 'x', 1030, " : 'b' $\n");
    expect_rejected(split_rule, "the tail of a long line must not become a rule");
    free(split_rule);
}

static void test_name_length(void) {
    puts("test: a name longer than 127 characters is rejected");
    char longest[128];
    memset(longest, 'n', 127);
    longest[127] = '\0';
    char too_long[129];
    memset(too_long, 'n', 128);
    too_long[128] = '\0';
    char text[512];

    snprintf(text, sizeof(text), "S : 'a' %s $\n%s : 'b'\n", longest, longest);
    expect_loads(text, "a 127-character name must load");
    snprintf(text, sizeof(text), "S : 'a' %s $\n", too_long);
    expect_rejected(text, "a 128-character name must not load");

    snprintf(text, sizeof(text), "S : '%s' $\n", longest);
    expect_loads(text, "a 127-character terminal must load");
    snprintf(text, sizeof(text), "S : '%s' $\n", too_long);
    expect_rejected(text, "a 128-character terminal must not load");

    snprintf(text, sizeof(text), "S : 'a'@%s $\n", longest);
    expect_loads(text, "a 127-character role must load");
    snprintf(text, sizeof(text), "S : 'a'@%s $\n", too_long);
    expect_rejected(text, "a 128-character role must not load");

    snprintf(text, sizeof(text), "S : 'a'@decl(%s) $\n", too_long);
    expect_rejected(text, "a 128-character declaration kind must not load");
    snprintf(text, sizeof(text), "S : 'a'@name:%s $\n", too_long);
    expect_rejected(text, "a 128-character role modifier must not load");

    expect_loads("S : 'a'@name:single 'b'@decl(function) $\n", "short annotations must load");
}

static void test_fixtures_load(void) {
    puts("test: every fixture grammar loads");
    const char *fixtures[] = {
        "tests/fixtures/grammars/sample1.txt",
        "tests/fixtures/grammars/sample2.txt",
        "tests/fixtures/grammars/semantic_actions.txt",
        "tests/fixtures/grammars/mylang_lsp.grammar",
    };
    for (size_t i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); i++) {
        SyntaxGrammar *grammar = syntax_load_grammar(fixtures[i]);
        CHECK(grammar != NULL, fixtures[i]);
        syntax_free_grammar(grammar);
    }

    const char *layered[] = {
        "tests/fixtures/grammars/mylang_lsp.grammar",
        "tests/fixtures/grammars/mlx.grammar",
    };
    SyntaxGrammar *grammar = syntax_load_grammar_multiple(layered, 2);
    CHECK(grammar != NULL, "MyLang grammar with mlx must load");
    CHECK(grammar->scope_open == syntax_terminal_id(grammar, "L_BRACE"), "MyLang scope opens at L_BRACE");
    CHECK(grammar->scope_close == syntax_terminal_id(grammar, "R_BRACE"), "MyLang scope closes at R_BRACE");
    syntax_free_grammar(grammar);
}

int main(void) {
    mkdir("build", 0777);
    mkdir(GRAMMAR_DIR, 0777);

    test_scope_directive();
    test_line_length();
    test_name_length();
    test_fixtures_load();

    remove(GRAMMAR_PATH);
    puts("grammar loader tests passed");
    return 0;
}
