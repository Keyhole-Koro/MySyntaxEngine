CC = gcc
CFLAGS = -Wall -Wextra

SRC = $(shell find src -name '*.c' | sort)
LIB_SRC = $(filter-out src/cli/main.c,$(SRC))
INCLUDE = -Iinc

BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin
TARGET = $(BIN_DIR)/output
TESTS = semantic_actions mylang_grammar
TEST_BINS = $(patsubst %,$(BIN_DIR)/test_%,$(TESTS))
OBJ = $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SRC))

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

# Each tests/test_<name>.c links against the library sources on its own.
$(BIN_DIR)/test_%: $(LIB_SRC) tests/test_%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $^

test: $(TEST_BINS)
	@set -e; for test in $(TEST_BINS); do ./$$test; done

clean:
	rm -rf $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET)

valgrind: $(TARGET)
	valgrind --leak-check=full ./$(TARGET)

.PHONY: all clean run test valgrind
