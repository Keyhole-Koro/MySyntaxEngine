#include "syntax.h"

#define IS_TERMINAL 1
#define IS_NONTERMINAL 0

ProductionRule *prod_rules;

int latestProdId = 0;

static ProductionRule *constructRule(symbol left, symbol *right);
static void registerSyntax(symbol left, symbol *right);
static symbol *processRightBuffer(Token *cur, Token **rest);

ProductionRule *processSyntaxTxt(char *file_path) {
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        DEBUG_PRINT("Failed to open file %s\n", file_path);
        exit(EXIT_FAILURE);
    }

    Token head;
    head.next = NULL;
    Token *cur = &head;
    char line[50];

    while (fgets(line, sizeof(line), file) != NULL) {
        cur = tokenizeLine(line, cur);
    }

    fclose(file);

    symbol left = 0;
    Token *rest = NULL;

    // register syntax (messy)
    for (Token *current = &head; current; current = current->next) {

        Token *next = current->next;

        /*
        if (next->kind == EQUAL) {
            registerRegex(current, next->next);
        }
        */

        /** 
            @brief example sample1.txt
            term          :
        */
        if (current->kind == NON_TERMINAL && next->kind == COLON) {
            left = mapString(current->value, IS_NONTERMINAL);
            continue;
        /**
            @brief example sample1.txt
                : factor
                | term '*' factor
        */
        } else if (current->kind == COLON || current->kind == PIPE) {
            symbol *right = processRightBuffer(next, &rest);
            registerSyntax(left, right);
            current = rest;
            continue;
        }
    }
    setStringExchange();

    return prod_rules;
}

static ProductionRule *constructRule(symbol left, symbol *right) {
    ProductionRule *rule = malloc(sizeof(ProductionRule));
    if (!rule) {
        DEBUG_PRINT("Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    rule->id = latestProdId++;
    rule->lhs = left;
    rule->rhs = right;
    rule->dotPos = 0;
    rule->next = NULL;
    int i = -1;
    while (right[++i] != END_SYMBOL_ARRAY) {}
    rule->numSymbols = i;
    return rule;
}

static void registerSyntax(symbol left, symbol *right) {
    ProductionRule *newRule = constructRule(left, right);
    if (!prod_rules) {
        prod_rules = newRule;
    } else {
        ProductionRule *current = prod_rules;
        while (current->next) {
            current = current->next;
        }
        current->next = newRule;
    }
}

static symbol *processRightBuffer(Token *cur, Token **rest) {
    int maxRight = 5;
    int numTk = 0;
    symbol *newRight = malloc(maxRight * sizeof(symbol));
    if (!newRight) {
        DEBUG_PRINT("Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    newRight[0] = END_SYMBOL_ARRAY;
    Token *current;
    bool EOI_found = false;
    for (current = cur; current->next; current = current->next) {
        if (current->kind == NEWLINE) break;
        if (current->kind == EOI) EOI_found = true;
        if (current->kind != TERMINAL && current->kind != NON_TERMINAL && current->kind != EOI) continue;
        if (numTk + 1 > maxRight) {
            newRight = realloc(newRight, (maxRight *= 2) * sizeof(symbol));
            if (!newRight) {
                DEBUG_PRINT("Memory allocation failed\n");
                exit(EXIT_FAILURE);
            }
        }
        switch (current->kind) {
            case TERMINAL:
                newRight[numTk++] = mapString(current->value, IS_TERMINAL);
                break;
            case NON_TERMINAL:
                newRight[numTk++] = mapString(current->value, IS_NONTERMINAL);
                break;
            case EOI:
                newRight[numTk++] = mapString("$", IS_TERMINAL);
                break;
            default:
                DEBUG_PRINT("unexpected token\n");
                exit(EXIT_FAILURE);
        }
    }
    
    if (latestProdId == 0 && !EOI_found) {
        DEBUG_PRINT("End Of Input $ must be set once.\n");
        exit(EXIT_FAILURE);
    }

    newRight[numTk] = END_SYMBOL_ARRAY;
    *rest = current;
    return newRight;
}

void showProductionRules() {
    ProductionRule *current = prod_rules;
    while (current) {
        printf("----\n");
        printf("ID: %d\n", current->id);
        printf("lhs: %s\n", exchangeSymbol(current->lhs));
        printf("Number of Elements of rhs: %d\n", current->numSymbols);
        printf("right hand side:");
        int i = 0;
        while (current->rhs[i] != END_SYMBOL_ARRAY) {
            printf(" %s", exchangeSymbol(current->rhs[i++]));
        }
        printf("\n");
        current = current->next;
    }
    printf("----\n");

}

int getNumProductionRuleSets() {
    return latestProdId;
}
