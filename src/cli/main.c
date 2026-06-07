#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mylang_syntax_engine/syntax_engine.h"

static const char *status_name(SyntaxStatus status) {
    switch (status) {
        case SYNTAX_OK:
            return "ok";
        case SYNTAX_ERROR:
            return "error";
        case SYNTAX_INCOMPLETE:
            return "incomplete";
    }
    return "unknown";
}

static void print_result(SyntaxResult *result) {
    printf("parse: %s at token %zu\n", status_name(result->status), result->token_index);
    if (result->expected_count > 0) {
        printf("expected:");
        for (size_t i = 0; i < result->expected_count; i++) {
            printf(" %s", result->expected[i]);
        }
        printf("\n");
    }
}

static int run_stdio_mode(const char *grammar_path) {
    SyntaxGrammar *grammar = syntax_load_grammar(grammar_path);
    if (!grammar) return EXIT_FAILURE;

    SyntaxTable *table = syntax_build_lr1_table(grammar);
    if (!table) {
        syntax_free_grammar(grammar);
        return EXIT_FAILURE;
    }

    printf(
        "ready states=%zu conflicts=%d\n",
        syntax_table_state_count(table),
        syntax_table_conflict_count(table)
    );
    fflush(stdout);

    char line[65536];
    while (fgets(line, sizeof(line), stdin)) {
        size_t token_count = 0;
        const char *tokens[8192];
        char *token = strtok(line, " \t\r\n");
        while (token && token_count < sizeof(tokens) / sizeof(tokens[0])) {
            tokens[token_count++] = token;
            token = strtok(NULL, " \t\r\n");
        }

        SyntaxResult result = syntax_parse_token_names(table, tokens, token_count);
        print_result(&result);
        printf("--\n");
        fflush(stdout);
        syntax_result_free(&result);
    }

    syntax_free_table(table);
    syntax_free_grammar(grammar);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
    bool print_table = false;
    bool print_conflicts = false;
    int arg_index = 1;

    if (argc > arg_index && strcmp(argv[arg_index], "--stdio") == 0) {
        arg_index++;
        const char *grammar_path = (argc > arg_index)
            ? argv[arg_index]
            : "tests/fixtures/grammars/sample2.txt";
        return run_stdio_mode(grammar_path);
    }

    while (argc > arg_index) {
        if (strcmp(argv[arg_index], "--table") == 0) {
            print_table = true;
            arg_index++;
            continue;
        }
        if (strcmp(argv[arg_index], "--conflicts") == 0) {
            print_conflicts = true;
            arg_index++;
            continue;
        }
        break;
    }

    const char *grammar_path = (argc > arg_index)
        ? argv[arg_index++]
        : "tests/fixtures/grammars/sample2.txt";

    SyntaxGrammar *grammar = syntax_load_grammar(grammar_path);
    if (!grammar) return EXIT_FAILURE;

    SyntaxTable *table = syntax_build_lr1_table(grammar);
    if (!table) {
        syntax_free_grammar(grammar);
        return EXIT_FAILURE;
    }

    printf(
        "lr1 table: %zu states, %d conflicts\n",
        syntax_table_state_count(table),
        syntax_table_conflict_count(table)
    );

    if (print_table) {
        syntax_print_table(table);
    }

    if (print_conflicts) {
        syntax_print_conflicts(table);
    }

    if (argc > arg_index) {
        const char **tokens = (const char **)&argv[arg_index];
        SyntaxResult result = syntax_parse_token_names(table, tokens, (size_t)(argc - arg_index));
        print_result(&result);
        syntax_result_free(&result);
    }

    syntax_free_table(table);
    syntax_free_grammar(grammar);
    return EXIT_SUCCESS;
}
