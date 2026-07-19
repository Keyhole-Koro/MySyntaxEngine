CC = gcc
CFLAGS = -Wall -Wextra

SRC = $(shell find src -name '*.c' | sort)
LIB_SRC = $(filter-out src/cli/main.c,$(SRC))
INCLUDE = -Iinc

BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin
TARGET = $(BIN_DIR)/output
SEMANTIC_ACTION_TEST = $(BIN_DIR)/test_semantic_actions
OBJ = $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SRC))

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

$(SEMANTIC_ACTION_TEST): $(LIB_SRC) tests/test_semantic_actions.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $^

test: $(SEMANTIC_ACTION_TEST)
	./$(SEMANTIC_ACTION_TEST)

clean:
	rm -rf $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET)

valgrind: $(TARGET)
	valgrind --leak-check=full ./$(TARGET)

.PHONY: all clean run test valgrind
