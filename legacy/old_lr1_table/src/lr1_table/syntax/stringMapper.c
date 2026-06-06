#include "stringMapper.h"

bool is_terminal = true;

int id_nonTerminal = 0;
int id_Terminal = 0;

int current_num_strmap = 0;
int max_size_strmap = 32;
StringMapping mappings_terminal = {NULL, 0, NULL};
StringMapping mappings_nonTerminal = {NULL, 0, NULL};

StringMapping *target_mappings;

StringMapping *registerStringMapping(const char *str);

StringMapping *checkIfExist(char *str);

int getNumNonTerminal() {
    return abs(id_nonTerminal);
}

int getNumTerminal() {
    return id_Terminal;
}

int arrayLengthNonTerminal() {
    return abs(id_nonTerminal) + 1;
}

int arrayLengthTerminal() {
    return id_Terminal + 1;
}


symbol mapString(char *str, bool isTerminal) {
    if (isTerminal) {
        is_terminal = true;
        target_mappings = &mappings_terminal;
    } else {        
        is_terminal = false;
        target_mappings = &mappings_nonTerminal;
    }

    // checks if str has existed
    StringMapping *foundMapping = checkIfExist(str);
    if (foundMapping) return foundMapping->number;
    
    StringMapping *new_mapping = registerStringMapping(str);
    return new_mapping->number;
}

StringMapping *registerStringMapping(const char *str) {
    StringMapping *new_mapping = (StringMapping *)malloc(sizeof(StringMapping));
    if (new_mapping == NULL) {
        DEBUG_PRINT("Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    new_mapping->string = my_strdup(str);
    if (new_mapping->string == NULL) {
        DEBUG_PRINT("String duplication failed\n");
        exit(EXIT_FAILURE);
    }
    new_mapping->number = is_terminal ? ++id_Terminal : --id_nonTerminal;
    new_mapping->next = NULL;

    StringMapping **ppCurrent = &target_mappings;
    while (*ppCurrent != NULL) {
        ppCurrent = &((*ppCurrent)->next);
    }
    *ppCurrent = new_mapping;

    return new_mapping;
}

StringMapping *checkIfExist(char *str) {
    for (StringMapping *current = target_mappings; current != NULL; current = current->next) {
        if (current->string && strcmp(current->string, str) == 0) {
            return current;
        }
    }
    return NULL;
}

void printMapping_Terminal() {
    StringMapping *current = &mappings_terminal;
    while (current) {
        printf("String: %s, number: %d\n", current->string, current->number);
        current = current->next;
    }
}

void printMapping_Non_Terminal() {
    StringMapping *current = &mappings_nonTerminal;
    while (current) {
        printf("String: %s, number: %d\n", current->string, current->number);
        current = current->next;
    }
}

char **symStringMap_nonTerminal;
char **symStringMap_Terminal;
void setStringExchange() {
    symStringMap_nonTerminal = calloc(arrayLengthNonTerminal(), sizeof(char*));
    for (StringMapping *mapping = &mappings_nonTerminal; mapping; mapping = mapping->next) {
        symStringMap_nonTerminal[abs(mapping->number)] = mapping->string;
    }
    symStringMap_Terminal = calloc(arrayLengthTerminal(), sizeof(char*));
    for (StringMapping *mapping = &mappings_terminal; mapping; mapping = mapping->next) {
        symStringMap_Terminal[mapping->number] = mapping->string;
    }
}

char *exchangeSymbol(symbol sym) {
    if (isTerminal(sym)) return symStringMap_Terminal[sym];
    if (isNonTerminal(sym)) return symStringMap_nonTerminal[abs(sym)];
    return NULL;
}

bool isTerminal(symbol sym) {
    return sym > 0;
}
bool isNonTerminal(symbol sym) {
    return sym < 0;
}

bool isEOI(symbol sym) {
    return strcmp(exchangeSymbol(sym), "$") == 0;
}