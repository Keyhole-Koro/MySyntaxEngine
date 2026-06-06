#include "follow.h"
#include <stdlib.h>
#include <stdbool.h>

void computeFollowSets(symbol targetSymbol);
bool addToFollowSet(symbol currentSymbol, symbol nextSymbol, symbol leftHandSide);
bool addFirstSetToFollowSet(symbol currentSymbol, symbol nextSymbol);
bool addToAnotherFollowSet(symbol fromSymbol, symbol toSymbol);

bool **firstSetsTable = NULL;
bool **followSetsTable = NULL;
ProductionRule *grammarEntry = NULL;

#define EPSILON (0)

bool **follow(ProductionRule *entryRule, bool **firstSets) {
    int numNonTerminals = arrayLengthNonTerminal() + 1;
    int numTerminals = arrayLengthTerminal() + 1;

    followSetsTable = (bool **)malloc(numNonTerminals * sizeof(bool *));
    for (int i = 0; i < numNonTerminals; ++i) {
        followSetsTable[i] = (bool *)malloc(numTerminals * sizeof(bool));
        for (int j = 0; j < numTerminals; ++j) {
            followSetsTable[i][j] = false;
        }
    }

    firstSetsTable = firstSets;
    grammarEntry = entryRule;

    bool changed;
    do {
        changed = false;
        for (ProductionRule *rule = grammarEntry; rule; rule = rule->next) {
            for (int i = 0; i < rule->numSymbols; i++) {
                symbol currentSymbol = rule->rhs[i];
                if (isNonTerminal(currentSymbol)) {
                    symbol nextSymbol = (i + 1 < rule->numSymbols) ? rule->rhs[i + 1] : EPSILON;
                    changed |= addToFollowSet(currentSymbol, nextSymbol, rule->lhs);
                }
            }
        }
    } while (changed);

    printf("---------------\nFollow sets:\n");
    for (int i = 1; i < arrayLengthNonTerminal(); i++) {
        printf("FOLLOW(%s) = {", exchangeSymbol(-1 * i));
        for (int j = 1; j < numTerminals; j++) {
            if (!followSetsTable[i][j]) continue;
            printf("%s ", exchangeSymbol(j));
        }
        printf("}\n");
    }

    return followSetsTable;
}

bool addToFollowSet(symbol currentSymbol, symbol nextSymbol, symbol leftHandSide) {
    bool changed = false;
    if (isTerminal(nextSymbol)) {
        if (!followSetsTable[abs(currentSymbol)][nextSymbol]) {
            followSetsTable[abs(currentSymbol)][nextSymbol] = true;
            changed = true;
        }
    } else if (isNonTerminal(nextSymbol)) {
        changed |= addFirstSetToFollowSet(currentSymbol, nextSymbol);
    } else if (nextSymbol == EPSILON) {
        changed |= addToAnotherFollowSet(leftHandSide, currentSymbol);
    }
    return changed;
}

bool addFirstSetToFollowSet(symbol currentSymbol, symbol nextSymbol) {
    bool changed = false;
    for (int i = 0; i < arrayLengthTerminal(); i++) {
        if (firstSetsTable[abs(nextSymbol)][i] && !followSetsTable[abs(currentSymbol)][i]) {
            followSetsTable[abs(currentSymbol)][i] = true;
            changed = true;
        }
    }
    return changed;
}

bool addToAnotherFollowSet(symbol fromSymbol, symbol toSymbol) {
    bool changed = false;
    for (int i = 0; i < arrayLengthTerminal(); i++) {
        if (followSetsTable[abs(fromSymbol)][i] && !followSetsTable[abs(toSymbol)][i]) {
            followSetsTable[abs(toSymbol)][i] = true;
            changed = true;
        }
    }
    return changed;
}
