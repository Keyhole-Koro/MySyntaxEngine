#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "syntax_engine/syntax_engine.h"

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "annotation test failure: %s (%s:%d)\n", message, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define MAX_TOKENS 64

typedef struct {
    SyntaxGrammar *grammar;
    SyntaxTable *table;
} Parser;

static Parser load(const char **paths, int count) {
    Parser parser;
    parser.grammar = syntax_load_grammar_multiple(paths, count);
    CHECK(parser.grammar != NULL, paths[count - 1]);
    parser.table = syntax_build_lr1_table(parser.grammar);
    CHECK(parser.table != NULL, "table must build");
    return parser;
}

static void unload(Parser *parser) {
    syntax_free_table(parser->table);
    syntax_free_grammar(parser->grammar);
}

static void token_ids(const Parser *parser, const char **names, size_t count, int *out) {
    CHECK(count <= MAX_TOKENS, "too many tokens for the test buffer");
    for (size_t i = 0; i < count; i++) {
        out[i] = syntax_terminal_id(parser->grammar, names[i]);
        CHECK(out[i] >= 0, names[i]);
    }
}

static int label(const Parser *parser, const char *name) {
    int id = syntax_label_id(parser->grammar, name);
    CHECK(id > 0, name);
    return id;
}

/* `roles` lists (token index, role name) pairs; every other token must have
   no role. */
typedef struct {
    size_t token;
    const char *role;
} ExpectedRole;

static void check_roles(const Parser *parser, const int *roles, size_t token_count,
                        const ExpectedRole *expected, size_t expected_count) {
    for (size_t i = 0; i < token_count; i++) {
        int want = 0;
        for (size_t e = 0; e < expected_count; e++) {
            if (expected[e].token == i) want = label(parser, expected[e].role);
        }
        if (roles[i] != want) {
            const char *got = syntax_label_name(parser->table, roles[i]);
            fprintf(stderr, "token %zu: expected role %s, got %s\n", i,
                    want ? syntax_label_name(parser->table, want) : "(none)", got ? got : "(none)");
            exit(1);
        }
    }
}

static void test_labels(void) {
    puts("test: label ids and names");
    const char *paths[] = {"tests/fixtures/grammars/annotations.txt"};
    Parser parser = load(paths, 1);
    CHECK(syntax_table_conflict_count(parser.table) == 0, "annotation grammar has no conflicts");

    /* "function" is both a role and a declaration kind: one label. */
    const char *names[] = {"function", "variable", "property"};
    for (size_t i = 0; i < COUNT(names); i++) {
        int id = label(&parser, names[i]);
        const char *name = syntax_label_name(parser.table, id);
        CHECK(name != NULL && strcmp(name, names[i]) == 0, "label name must round trip");
    }
    CHECK(label(&parser, "function") != label(&parser, "variable"), "labels must be distinct");

    CHECK(syntax_label_id(parser.grammar, "struct") == 0, "an unused label has no id");
    CHECK(syntax_label_id(parser.grammar, "single") == 0, "a modifier is not a label");
    CHECK(syntax_label_id(parser.grammar, "decl") == 0, "decl is not a label");
    CHECK(syntax_label_id(parser.grammar, NULL) == 0, "a NULL name has no id");
    CHECK(syntax_label_id(NULL, "function") == 0, "a NULL grammar has no labels");
    CHECK(syntax_label_name(parser.table, 0) == NULL, "label 0 is none");
    CHECK(syntax_label_name(parser.table, -1) == NULL, "a negative label is none");
    CHECK(syntax_label_name(parser.table, 1000) == NULL, "an unknown label is none");
    unload(&parser);
}

static void test_roles_and_symbols(void) {
    puts("test: roles, single and declarations by scope depth");
    const char *paths[] = {"tests/fixtures/grammars/annotations.txt"};
    Parser parser = load(paths, 1);

    /* var x; fn main { var local; f(); } a.b(); */
    const char *names[] = {
        "VAR", "ID", "SEMI",                                   /* 0-2 */
        "FN", "ID", "LB", "VAR", "ID", "SEMI",                 /* 3-8 */
        "ID", "LP", "RP", "SEMI", "RB",                        /* 9-13 */
        "ID", "DOT", "ID", "LP", "RP", "SEMI",                 /* 14-19 */
    };
    int ids[MAX_TOKENS];
    token_ids(&parser, names, COUNT(names), ids);
    const ExpectedRole expected[] = {
        {1, "variable"},
        {4, "function"},
        {7, "variable"},
        {9, "function"},   /* f is one token, so :single applies */
        {14, "variable"},  /* a.b is three tokens, so a keeps its role */
        {16, "property"},
    };

    int roles[MAX_TOKENS] = {0};
    SyntaxSymbol symbols[8];
    size_t symbol_count = 99;
    SyntaxResult result = syntax_parse_token_ids_ex(parser.table, ids, COUNT(names), roles,
                                                    symbols, COUNT(symbols), &symbol_count);
    CHECK(result.status == SYNTAX_OK, "annotated input must parse");
    syntax_result_free(&result);
    check_roles(&parser, roles, COUNT(names), expected, COUNT(expected));

    /* local is declared inside the braces, so it is not an outline symbol. */
    CHECK(symbol_count == 2, "two top-level declarations");
    CHECK(symbols[0].token_index == 1 && symbols[0].kind == label(&parser, "variable"), "x is a variable");
    CHECK(symbols[1].token_index == 4 && symbols[1].kind == label(&parser, "function"), "main is a function");

    /* symbol_cap bounds the symbols written. */
    SyntaxSymbol one[1];
    symbol_count = 99;
    result = syntax_parse_token_ids_ex(parser.table, ids, COUNT(names), NULL, one, 1, &symbol_count);
    CHECK(result.status == SYNTAX_OK, "parse without roles");
    syntax_result_free(&result);
    CHECK(symbol_count == 1, "symbol_cap must bound the count");
    CHECK(one[0].token_index == 1, "the first declaration fills the only slot");

    /* syntax_parse_token_ids_roles fills the same roles. */
    int roles_only[MAX_TOKENS] = {0};
    result = syntax_parse_token_ids_roles(parser.table, ids, COUNT(names), roles_only);
    CHECK(result.status == SYNTAX_OK, "roles-only parse");
    syntax_result_free(&result);
    CHECK(memcmp(roles, roles_only, COUNT(names) * sizeof(int)) == 0, "both entry points give one set of roles");

    /* No output buffers at all, and a count with no symbol buffer. */
    symbol_count = 99;
    result = syntax_parse_token_ids_ex(parser.table, ids, COUNT(names), NULL, NULL, 0, &symbol_count);
    CHECK(result.status == SYNTAX_OK, "parse without buffers");
    CHECK(symbol_count == 0, "the count is reset even without a symbol buffer");
    syntax_result_free(&result);
    unload(&parser);
}

static void test_nested_scope(void) {
    puts("test: a declaration two scopes deep is not top-level");
    const char *paths[] = {"tests/fixtures/grammars/annotations.txt"};
    Parser parser = load(paths, 1);

    /* fn outer { fn inner { } } fn after { } */
    const char *names[] = {"FN", "ID", "LB", "FN", "ID", "LB", "RB", "RB", "FN", "ID", "LB", "RB"};
    int ids[MAX_TOKENS];
    token_ids(&parser, names, COUNT(names), ids);

    SyntaxSymbol symbols[8];
    size_t symbol_count = 0;
    SyntaxResult result = syntax_parse_token_ids_ex(parser.table, ids, COUNT(names), NULL,
                                                    symbols, COUNT(symbols), &symbol_count);
    CHECK(result.status == SYNTAX_OK, "nested functions must parse");
    syntax_result_free(&result);
    CHECK(symbol_count == 2, "only the two outer functions are top-level");
    CHECK(symbols[0].token_index == 1, "outer comes first");
    CHECK(symbols[1].token_index == 9, "after comes once both scopes have closed");
    unload(&parser);
}

static void test_extend_layers_mlx(void) {
    puts("test: %extend layers mlx.grammar over the MyLang grammar");
    const char *base_only[] = {"tests/fixtures/grammars/mylang_lsp.grammar"};
    const char *layered[] = {
        "tests/fixtures/grammars/mylang_lsp.grammar",
        "tests/fixtures/grammars/mlx.grammar",
    };
    Parser base = load(base_only, 1);
    Parser mlx = load(layered, 2);
    CHECK(syntax_terminal_id(base.grammar, "MLX_TAG_OPEN") < 0, "MyLang alone has no mlx terminals");
    CHECK(syntax_terminal_id(mlx.grammar, "MLX_TAG_OPEN") >= 0, "mlx adds its terminals");
    CHECK(syntax_table_conflict_count(mlx.table) <= 2, "mlx must not add parser conflicts");

    /* i32 g; i32 main() { i32 local; return <View />; } */
    const char *names[] = {
        "I32", "IDENTIFIER", "SEMICOLON",                                         /* 0-2 */
        "I32", "IDENTIFIER", "L_PARENTHESES", "R_PARENTHESES", "L_BRACE",         /* 3-7 */
        "I32", "IDENTIFIER", "SEMICOLON",                                         /* 8-10 */
        "RETURN", "MLX_TAG_OPEN", "IDENTIFIER", "MLX_TAG_SELF_CLOSE", "SEMICOLON", /* 11-15 */
        "R_BRACE",                                                                /* 16 */
    };
    SyntaxResult result = syntax_parse_token_names(base.table, names, COUNT(names));
    CHECK(result.status == SYNTAX_ERROR && result.token_index == 12, "MyLang alone stops at the mlx tag");
    syntax_result_free(&result);

    int ids[MAX_TOKENS];
    token_ids(&mlx, names, COUNT(names), ids);
    int roles[MAX_TOKENS] = {0};
    SyntaxSymbol symbols[8];
    size_t symbol_count = 0;
    result = syntax_parse_token_ids_ex(mlx.table, ids, COUNT(names), roles,
                                       symbols, COUNT(symbols), &symbol_count);
    CHECK(result.status == SYNTAX_OK, "an mlx element must parse as a primary");
    syntax_result_free(&result);

    const ExpectedRole expected[] = {
        {1, "variable"},
        {4, "function"},
        {9, "variable"},
        {13, "struct"},
    };
    check_roles(&mlx, roles, COUNT(names), expected, COUNT(expected));
    CHECK(symbol_count == 2, "g and main are the top-level declarations");
    CHECK(symbols[0].token_index == 1 && symbols[0].kind == label(&mlx, "variable"), "g is a variable");
    CHECK(symbols[1].token_index == 4 && symbols[1].kind == label(&mlx, "function"), "main is a function");

    unload(&mlx);
    unload(&base);
}

int main(void) {
    test_labels();
    test_roles_and_symbols();
    test_nested_scope();
    test_extend_layers_mlx();
    puts("annotation tests passed");
    return 0;
}
