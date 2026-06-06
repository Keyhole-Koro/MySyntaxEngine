#include "item.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UNSET_ID (-1)

bool DEBUG_ITEM_ENABLED = false;

#define DEBUG_ITEM(fmt, ...) \
    do { \
        if (DEBUG_ITEM_ENABLED) { \
            fprintf(stderr, fmt, ##__VA_ARGS__); \
        } \
    } while (0)

    
ProductionRule *grammarRules;
ProductionRule startProductionRule = {UNSET_ID, 0, 0, NULL, 0, NULL};
const ProductionRule *startProductionRuleCopy = NULL;

LR1Item *startItemSet = NULL;
int numItemSets = -1;

LR1Item *constructItemSet(LR1Item *item);
void addGotoItem(LR1Item *item, LR1Item *gotoItem);
LR1Item *findItemSet(ProductionRule *production, symbol lookahead);
LR1Item *findItemInGotoSets(LR1Item *expectedItem);
symbol *extractLookaheadSymbols(ProductionRule *production);
void advanceDot(ProductionRule *prod);

bool isEndOfItem(LR1Item *item);

void addLR1ItemList(LR1Item *item);
LR1Item *findItemFromList(LR1Item *targetItem, bool (*cmpMethod)(LR1Item*, LR1Item*));

int getNumItemSets() {
    return numItemSets;
}

LR1Item *constructInitialItemSet() {
    if (startProductionRule.id == UNSET_ID) {
        DEBUG_PRINT("The start rule has not been set.\n");
        DEBUG_PRINT("Set the start by using setStartRule(ProductionRule*) before running constructInitialItemSet\n");
        exit(EXIT_FAILURE);
    }

    LR1Item *startItem = malloc(sizeof(LR1Item));
    if (!startItem) {
        DEBUG_PRINT("Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    
    startItem->stateId = ++numItemSets;
    startItem->production = &startProductionRule;
    startItem->lookahead = 0;
    startItem->numGotoItems = 0;
    startItem->maxGotoItems = 5;
    startItem->gotoItems = calloc(startItem->maxGotoItems, sizeof(LR1Item *));

    constructItemSet(startItem);

    return startItemSet;
}

LR1Item *constructItemSet(LR1Item *item) {
    if (!startItemSet) startItemSet = item;

    DEBUG_ITEM("------------------------------------\n");
    DEBUG_ITEM("Number of item sets: %d\n", numItemSets);

    ProductionRule *clonedProd = cloneProduction(item->production);
    symbol *lookaheadSymbols = extractLookaheadSymbols(clonedProd);

    int i = 0;
    symbol sym = 0;

    while ((sym = lookaheadSymbols[i++]) != END_SYMBOL_ARRAY) {
        if (isEOI(sym)) continue;

        ProductionRule *commonRules = filterProductions(clonedProd, getCurrentSymbol, sym);
        ProductionRule *closureRules = closure(&startProductionRule, commonRules);

        advanceDot(commonRules);
       
        ProductionRule *allRequiredProductions = combineProductions(closureRules, commonRules);
        LR1Item *foundItemSet = findItemSet(allRequiredProductions, sym);

        if (foundItemSet) {
            addGotoItem(item, foundItemSet);
            continue;
        }

        DEBUG_ITEM(">Next lookahead symbol: %s\n", exchangeSymbol(sym));
        LR1Item *newItemSet = calloc(1, sizeof(LR1Item));
        if (!newItemSet) {
            DEBUG_PRINT("Memory allocation failed\n");
            exit(EXIT_FAILURE);
        }

        newItemSet->stateId = ++numItemSets;
        newItemSet->production = allRequiredProductions;
        newItemSet->lookahead = sym;
        newItemSet->numGotoItems = 0;
        newItemSet->maxGotoItems = 6;
        newItemSet->gotoItems = calloc(newItemSet->maxGotoItems, sizeof(LR1Item *));
        if (!newItemSet->gotoItems) {
            DEBUG_PRINT("Memory allocation failed\n");
            exit(EXIT_FAILURE);
        }

        for (ProductionRule *rule = allRequiredProductions; rule; rule = rule->next) {
            DEBUG_ITEM("Rule ID: %d, lookahead symbol: '%s', number of symbols: %d, dot position: %d\n",
                rule->id,
                exchangeSymbol(rule->rhs[rule->dotPos]),
                rule->numSymbols,
                rule->dotPos);
            
            DEBUG_ITEM("[");
            for (int i = 0; i < rule->numSymbols; i++) {
                DEBUG_ITEM("%s", exchangeSymbol(rule->rhs[i]));
                if (i < rule->numSymbols - 1) {
                    DEBUG_ITEM(" ");
                }
            }
            DEBUG_ITEM("]\n");
        }

        addLR1ItemList(newItemSet);
        
        addGotoItem(item, newItemSet);
        constructItemSet(newItemSet);
    }
    
    free(lookaheadSymbols);
    return NULL;
}

bool isEndOfItem(LR1Item *item) {
    int num_prods = 1;
    
    ProductionRule *prod = NULL;
    for (prod = item->production; prod->next; prod = prod->next) {
        num_prods++;
    }
    if (num_prods > 1) return false;

    if (prod->dotPos != prod->numSymbols) return false;

    return true;
}

void addGotoItem(LR1Item *item, LR1Item *gotoItem) {
    if (item->numGotoItems > item->maxGotoItems) {
        LR1Item **newGotoItem = malloc((item->maxGotoItems *= 2) * sizeof(LR1Item *));
        if (!newGotoItem) {
            DEBUG_PRINT("Memory allocation failed\n");
            exit(EXIT_FAILURE);
        }
        memcpy(newGotoItem, item->gotoItems, item->maxGotoItems * sizeof(LR1Item *));
        free(item->gotoItems);
        item->gotoItems = newGotoItem;
    }
    item->gotoItems[item->numGotoItems++] = gotoItem;
}

int list_numLR1Items = 0;
int list_maxLR1Items = 20;
LR1Item **LR1ItemList = NULL;
LR1Item **initializeLR1ItemList() {
    LR1ItemList = calloc(list_maxLR1Items, sizeof(LR1Item *));
    return LR1ItemList;
}

void addLR1ItemList(LR1Item *item) {
    if (!LR1ItemList) {
        DEBUG_PRINT("LR1 item list hasnt been initialized; use initializeLR1ItemList");
        exit(EXIT_FAILURE);
    }

    if (list_numLR1Items > list_maxLR1Items) {
        LR1ItemList = realloc(LR1ItemList, (list_maxLR1Items *= 2) * sizeof(LR1Item *));
        if (!LR1ItemList) {
            DEBUG_PRINT("Memory allocation failed\n");
            exit(EXIT_FAILURE);
        }
    }
    LR1ItemList[list_numLR1Items++] = item;
}

LR1Item *findItemFromList(LR1Item *targetItem, bool (*cmpMethod)(LR1Item*, LR1Item*)) {
    for (int i = 0; i < list_numLR1Items; i++) {
        if (cmpMethod(LR1ItemList[i], targetItem)) return LR1ItemList[i];
    }
    return NULL;
}

LR1Item *findItemSet(ProductionRule *production, symbol lookahead) {
    LR1Item tempItem = {0, production, lookahead, 0, 0, NULL};
    return findItemInGotoSets(&tempItem);
}

bool ifItemTargetExists(LR1Item *item1, LR1Item *item2) {
    if (item1->lookahead != item2->lookahead) return false;
    if ((item1->production->numSymbols != item2->production->numSymbols)) return false;

    ExistenceArray *exArray = createExistenceArray(getNumProductionRuleSets(), noRevise);

    for (ProductionRule *rule = item1->production; rule; rule = rule->next) {
        exArray->array[rule->id] = true;
    }

    for (ProductionRule *rule = item2->production; rule; rule = rule->next) {
        if (!checkAndSetExistence(exArray, rule->id)) {
            freeExistenceArray(exArray);
            return false;
        }
    }

    return true;
}

LR1Item *findItemInGotoSets(LR1Item *expectedItem) {
    return findItemFromList(expectedItem, ifItemTargetExists);
}

symbol *extractLookaheadSymbols(ProductionRule *prod) {
    ExistenceArray *isNonTerminalSet = createExistenceArray(getNumNonTerminal(), reviseNonTerminal);
    ExistenceArray *isTerminalSet = createExistenceArray(getNumTerminal(), noRevise);

    int existenceArrLength = getNumNonTerminal() + getNumTerminal();
    bool symbolExistenceArray[existenceArrLength];
    for (int i = 0; i < existenceArrLength; i++) {
        symbolExistenceArray[i] = false;
    }

    int symArrayLength = 10;
    int numSymbols = 0;
    symbol *symbols = malloc(symArrayLength * sizeof(symbol));
    symbols[0] = END_SYMBOL_ARRAY;
    for (ProductionRule *rule = prod; rule; rule = rule->next) {
        if (rule->dotPos >= rule->numSymbols) continue;

        symbol readSym = getCurrentSymbol(rule);

        if (isNonTerminal(readSym) && checkAndSetExistence(isNonTerminalSet, readSym)) continue;
        if (isTerminal(readSym) && checkAndSetExistence(isTerminalSet, readSym)) continue;

        if (numSymbols + 1 > symArrayLength) {
            symbols = realloc(symbols, (symArrayLength *= 2) * sizeof(symbol));
            if (!symbols) {
                DEBUG_PRINT("Memory allocation failed\n");
                exit(EXIT_FAILURE);
            }
        }
        
        symbols[numSymbols++] = readSym;
    }
    symbols[numSymbols] = END_SYMBOL_ARRAY;
    
    return symbols;
}

void enable_item_debug() {
    DEBUG_ITEM_ENABLED = true;
}

void setStartRule(const ProductionRule *entryRule) {
    
    startProductionRule = *entryRule;
    initializeLR1ItemList();
}

bool isACC(LR1Item *item, ProductionRule **accProd, symbol *accSym) {
    for (ProductionRule *prod = item->production; prod; prod = prod->next) {
        for (int i = 0; i < prod->numSymbols; i++) {
            if (!isEOI(prod->rhs[i])) continue;
            if (prod->dotPos != prod->numSymbols - 1) continue;
            *accProd = prod;
            *accSym = prod->rhs[i];
            return true;
        }
    }
    return false;
}