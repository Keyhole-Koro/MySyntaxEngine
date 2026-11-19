#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "syntax_engine/syntax_engine.h"
/* White-box: the cache file is the table's rows, so the test reads the table
   itself to find cells to corrupt. */
#include "../src/lr1/syntax_engine_internal.h"

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "table cache test failure: %s (%s:%d)\n", message, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define GRAMMAR_PATH "tests/fixtures/grammars/sample2.txt"
#define OTHER_GRAMMAR_PATH "tests/fixtures/grammars/sample1.txt"
#define SMALL_GRAMMAR_PATH "tests/fixtures/grammars/semantic_actions.txt"
#define CACHE_DIR "build/test-table-cache"
#define CACHE_PATH CACHE_DIR "/sample2.table"
#define CORRUPT_PATH CACHE_DIR "/corrupt.table"

typedef struct {
    unsigned char *bytes;
    size_t size;
} FileBytes;

static FileBytes read_file(const char *path) {
    FileBytes file = {0};
    FILE *in = fopen(path, "rb");
    CHECK(in != NULL, "cache file must open");
    CHECK(fseek(in, 0, SEEK_END) == 0, "seek to end");
    long size = ftell(in);
    CHECK(size > 0, "cache file must not be empty");
    CHECK(fseek(in, 0, SEEK_SET) == 0, "seek to start");
    file.size = (size_t)size;
    file.bytes = malloc(file.size);
    CHECK(file.bytes != NULL, "file buffer allocation");
    CHECK(fread(file.bytes, 1, file.size, in) == file.size, "cache file must read");
    fclose(in);
    return file;
}

static void write_file(const char *path, const unsigned char *bytes, size_t size) {
    FILE *out = fopen(path, "wb");
    CHECK(out != NULL, "corrupt file must open");
    CHECK(fwrite(bytes, 1, size, out) == size, "corrupt file must write");
    CHECK(fclose(out) == 0, "corrupt file must close");
}

/* The saved rows are the last state_count * symbol_count * 3 ints of the file:
   per state, (kind, value) for every symbol, then a goto for every symbol. */
static size_t rows_offset(const FileBytes *file, const SyntaxTable *table) {
    size_t row_bytes = (size_t)table->grammar->symbol_count * 3 * sizeof(int);
    size_t rows_bytes = (size_t)table->state_count * row_bytes;
    CHECK(file->size > rows_bytes + 2 * sizeof(int), "cache file must hold its rows");
    return file->size - rows_bytes;
}

static size_t action_offset(const FileBytes *file, const SyntaxTable *table, int state, int symbol) {
    size_t row_bytes = (size_t)table->grammar->symbol_count * 3 * sizeof(int);
    return rows_offset(file, table) + (size_t)state * row_bytes + (size_t)symbol * 2 * sizeof(int);
}

static size_t goto_offset(const FileBytes *file, const SyntaxTable *table, int state, int symbol) {
    size_t row_bytes = (size_t)table->grammar->symbol_count * 3 * sizeof(int);
    return rows_offset(file, table) + (size_t)state * row_bytes +
        (size_t)table->grammar->symbol_count * 2 * sizeof(int) + (size_t)symbol * sizeof(int);
}

static void put_int(unsigned char *bytes, size_t offset, int value) {
    memcpy(bytes + offset, &value, sizeof(value));
}

/* A copy of the saved cache with one int replaced must not load. */
static void expect_rejected_with_int(SyntaxGrammar *grammar, const FileBytes *file, size_t offset,
                                     int value, const char *message) {
    unsigned char *copy = malloc(file->size);
    CHECK(copy != NULL, "copy allocation");
    memcpy(copy, file->bytes, file->size);
    put_int(copy, offset, value);
    write_file(CORRUPT_PATH, copy, file->size);
    free(copy);

    SyntaxTable *loaded = syntax_load_lr1_table(grammar, CORRUPT_PATH);
    if (loaded) {
        syntax_free_table(loaded);
        CHECK(false, message);
    }
}

static void find_action(const SyntaxTable *table, ActionKind kind, int *out_state, int *out_symbol) {
    for (int state = 0; state < table->state_count; state++) {
        for (int symbol = 0; symbol < table->grammar->symbol_count; symbol++) {
            if (table->actions[state][symbol].kind == kind) {
                *out_state = state;
                *out_symbol = symbol;
                return;
            }
        }
    }
    CHECK(false, "table must hold the action kind");
}

static void find_goto(const SyntaxTable *table, int *out_state, int *out_symbol) {
    for (int state = 0; state < table->state_count; state++) {
        for (int symbol = 0; symbol < table->grammar->symbol_count; symbol++) {
            if (table->gotos[state][symbol] >= 0) {
                *out_state = state;
                *out_symbol = symbol;
                return;
            }
        }
    }
    CHECK(false, "table must hold a goto");
}

static void expect_parse(SyntaxTable *table, const char **tokens, size_t count,
                         SyntaxStatus status, size_t token_index) {
    SyntaxResult result = syntax_parse_token_names(table, tokens, count);
    CHECK(result.status == status, "parse status must match");
    CHECK(result.token_index == token_index, "parse stop token must match");
    syntax_result_free(&result);
}

static void test_round_trip(SyntaxGrammar *grammar, SyntaxTable *built) {
    puts("test: save and load round trip");
    CHECK(syntax_save_lr1_table(built, CACHE_PATH) == 1, "table must save");

    SyntaxTable *loaded = syntax_load_lr1_table(grammar, CACHE_PATH);
    CHECK(loaded != NULL, "saved table must load");
    CHECK(syntax_table_state_count(loaded) == syntax_table_state_count(built), "state count must match");
    CHECK(syntax_table_conflict_count(loaded) == syntax_table_conflict_count(built),
          "conflict count must match");
    for (int state = 0; state < built->state_count; state++) {
        for (int symbol = 0; symbol < grammar->symbol_count; symbol++) {
            CHECK(loaded->actions[state][symbol].kind == built->actions[state][symbol].kind,
                  "action kinds must match");
            if (built->actions[state][symbol].kind != ACTION_NONE) {
                CHECK(loaded->actions[state][symbol].value == built->actions[state][symbol].value,
                      "action values must match");
            }
            CHECK(loaded->gotos[state][symbol] == built->gotos[state][symbol], "gotos must match");
        }
    }

    const char *valid[] = {"int", "id", "=", "id", ";"};
    const char *invalid[] = {"int", "=", "id", ";"};
    const char *incomplete[] = {"int", "id"};
    /* The grammar shifts the end of input, so an accepted parse stops past it. */
    expect_parse(built, valid, 5, SYNTAX_OK, 6);
    expect_parse(loaded, valid, 5, SYNTAX_OK, 6);
    expect_parse(built, invalid, 4, SYNTAX_ERROR, 1);
    expect_parse(loaded, invalid, 4, SYNTAX_ERROR, 1);
    expect_parse(built, incomplete, 2, SYNTAX_INCOMPLETE, 2);
    expect_parse(loaded, incomplete, 2, SYNTAX_INCOMPLETE, 2);
    syntax_free_table(loaded);
}

static void test_rejects_wrong_files(SyntaxGrammar *grammar, SyntaxTable *built, const FileBytes *file) {
    puts("test: truncated, foreign and other-grammar files are rejected");
    CHECK(syntax_load_lr1_table(grammar, CACHE_DIR "/missing.table") == NULL, "missing file must not load");

    write_file(CORRUPT_PATH, file->bytes, file->size - 1);
    CHECK(syntax_load_lr1_table(grammar, CORRUPT_PATH) == NULL, "truncated file must not load");
    write_file(CORRUPT_PATH, file->bytes, 10);
    CHECK(syntax_load_lr1_table(grammar, CORRUPT_PATH) == NULL, "truncated header must not load");

    unsigned char *copy = malloc(file->size);
    CHECK(copy != NULL, "copy allocation");
    memcpy(copy, file->bytes, file->size);
    copy[0] = 'X';
    write_file(CORRUPT_PATH, copy, file->size);
    free(copy);
    CHECK(syntax_load_lr1_table(grammar, CORRUPT_PATH) == NULL, "bad magic must not load");

    SyntaxGrammar *other = syntax_load_grammar(OTHER_GRAMMAR_PATH);
    CHECK(other != NULL, "other grammar must load");
    CHECK(syntax_load_lr1_table(other, CACHE_PATH) == NULL, "table of another grammar must not load");
    syntax_free_grammar(other);

    /* The header ints just before the rows. */
    size_t rows = rows_offset(file, built);
    size_t state_count_offset = rows - 2 * sizeof(int);
    size_t conflict_count_offset = rows - sizeof(int);
    expect_rejected_with_int(grammar, file, state_count_offset, 0, "zero states must not load");
    expect_rejected_with_int(grammar, file, state_count_offset, -1, "negative states must not load");
    expect_rejected_with_int(grammar, file, state_count_offset, built->state_count + 1,
                             "more states than the file holds must not load");
    /* Must be rejected before allocating, not by exiting on out of memory. */
    expect_rejected_with_int(grammar, file, state_count_offset, INT_MAX, "huge state count must not load");
    expect_rejected_with_int(grammar, file, conflict_count_offset, -1,
                             "negative conflict count must not load");
}

static void test_rejects_out_of_range_cells(SyntaxGrammar *grammar, SyntaxTable *built,
                                            const FileBytes *file) {
    puts("test: out-of-range shift, reduce and goto values are rejected");
    int state = 0;
    int symbol = 0;

    find_action(built, ACTION_SHIFT, &state, &symbol);
    size_t shift_value = action_offset(file, built, state, symbol) + sizeof(int);
    expect_rejected_with_int(grammar, file, shift_value, built->state_count, "shift past the last state");
    expect_rejected_with_int(grammar, file, shift_value, 1000000, "far shift target");
    expect_rejected_with_int(grammar, file, shift_value, -1, "negative shift target");

    find_action(built, ACTION_REDUCE, &state, &symbol);
    size_t reduce_value = action_offset(file, built, state, symbol) + sizeof(int);
    expect_rejected_with_int(grammar, file, reduce_value, grammar->production_count,
                             "reduce past the last production");
    expect_rejected_with_int(grammar, file, reduce_value, -1, "negative reduce production");

    size_t kind = action_offset(file, built, state, symbol);
    expect_rejected_with_int(grammar, file, kind, ACTION_ACCEPT + 1, "unknown action kind");
    expect_rejected_with_int(grammar, file, kind, -1, "negative action kind");

    find_goto(built, &state, &symbol);
    size_t goto_value = goto_offset(file, built, state, symbol);
    expect_rejected_with_int(grammar, file, goto_value, built->state_count, "goto past the last state");
    expect_rejected_with_int(grammar, file, goto_value, -2, "goto below -1");

    /* A shift in a nonterminal's column is never built. */
    int nonterminal = grammar->start_symbol;
    size_t nonterminal_kind = action_offset(file, built, 0, nonterminal);
    expect_rejected_with_int(grammar, file, nonterminal_kind, ACTION_SHIFT, "shift on a nonterminal");
}

static void test_save_reports_close_failure(void) {
    /* /dev/full accepts the buffered writes and fails when they are flushed
       at fclose; the table is small enough to stay in the stdio buffer. */
    FILE *probe = fopen("/dev/full", "wb");
    if (!probe) {
        puts("test: save reports a failed close (skipped: no /dev/full)");
        return;
    }
    fclose(probe);
    puts("test: save reports a failed close");

    SyntaxGrammar *grammar = syntax_load_grammar(SMALL_GRAMMAR_PATH);
    CHECK(grammar != NULL, "small grammar must load");
    SyntaxTable *table = syntax_build_lr1_table(grammar);
    CHECK(syntax_save_lr1_table(table, "/dev/full") == 0, "save to a full device must fail");
    CHECK(syntax_save_lr1_table(table, CACHE_DIR "/no-such-dir/x.table") == 0,
          "save to a missing directory must fail");
    syntax_free_table(table);
    syntax_free_grammar(grammar);
}

int main(void) {
    mkdir("build", 0777);
    mkdir(CACHE_DIR, 0777);

    SyntaxGrammar *grammar = syntax_load_grammar(GRAMMAR_PATH);
    CHECK(grammar != NULL, "grammar must load");
    SyntaxTable *built = syntax_build_lr1_table(grammar);
    CHECK(built != NULL, "table must build");

    test_round_trip(grammar, built);
    FileBytes file = read_file(CACHE_PATH);
    test_rejects_wrong_files(grammar, built, &file);
    test_rejects_out_of_range_cells(grammar, built, &file);
    test_save_reports_close_failure();

    free(file.bytes);
    remove(CORRUPT_PATH);
    remove(CACHE_PATH);
    syntax_free_table(built);
    syntax_free_grammar(grammar);
    puts("table cache tests passed");
    return 0;
}
