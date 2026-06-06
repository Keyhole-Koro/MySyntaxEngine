#include "first.h"
#include <stdlib.h>

ProductionRule *extractRulesWithCommonLhs(ProductionRule *entryRule, ProductionRule *commonLhsRules);

bool **first(ProductionRule *entryRule) {
    /* +1 since 0 is not used*/
    int numNonTerminal = arrayLengthNonTerminal();
    int numTerminal = arrayLengthTerminal();
    
    bool **firstSetsTable = (bool **)malloc(numNonTerminal * sizeof(bool *));
    for (int i = 0; i < numNonTerminal; ++i) {
        firstSetsTable[i] = (bool *)malloc(numTerminal * sizeof(bool));
        for (int j = 0; j < numTerminal; ++j) {
            firstSetsTable[i][j] = false;
        }
    }

    ExistenceArray *isLhsDone = createExistenceArray(numNonTerminal, reviseNonTerminal);

    for (ProductionRule *rule = entryRule; rule; rule = rule->next) {
        symbol lhs = rule->lhs;
        if (checkAndSetExistence(isLhsDone, lhs)) continue;

        ProductionRule *commonLhsRules = filterProductions(entryRule, getLhs, lhs);

        ProductionRule *probedRules = extractRulesWithCommonLhs(entryRule, commonLhsRules);

        for (ProductionRule *rule = probedRules; rule; rule = rule->next) {
            symbol sym = rule->rhs[0];
            if (isNonTerminal(sym)) continue;
            firstSetsTable[abs(lhs)][sym] = true;
        }
    }

    printf("---------------\nfirst table below\n");
    for (int i = 1; i < numNonTerminal; i++) {
        symbol non_terminal_sym = -1 * i;
        if (*exchangeSymbol(non_terminal_sym) == '$') continue;

        printf("FIRST(%s) = {", exchangeSymbol(non_terminal_sym));
        for (int j = 1; j < numTerminal; j++) {
            if (!firstSetsTable[i][j]) continue;
            printf("%s ", exchangeSymbol(j));
        }
        printf("}\n");
    }

    return firstSetsTable;
}

void probeNonTerminalRulesRecursively(ProductionRule *startProductionRule, symbol targetSymbol, ExistenceArray symbolExistenceArray[], ExistenceArray ruleExistenceArray[]) {
    for (ProductionRule *curRule = startProductionRule; curRule; curRule = curRule->next) {
        symbol left = curRule->lhs;

        if (*exchangeSymbol(left) == '$') continue;
        if (left != targetSymbol) continue;

        symbol firstRightSymbol = curRule->rhs[0];

        checkAndSetExistence(ruleExistenceArray, curRule->id);

        if (isTerminal(firstRightSymbol)) continue;

        if (!checkAndSetExistence(symbolExistenceArray, firstRightSymbol)) {
            probeNonTerminalRulesRecursively(startProductionRule, firstRightSymbol, symbolExistenceArray, ruleExistenceArray);        
        }
    }
}

ProductionRule *extractRulesWithCommonLhs(ProductionRule *entryRule, ProductionRule *commonLhsRules) {
    ExistenceArray *symbolExistenceArray = createExistenceArray(arrayLengthNonTerminal(), reviseNonTerminal);
    ExistenceArray *ruleExistenceArray = createExistenceArray(getNumProductionRuleSets(), noRevise);

    for (ProductionRule *rule = commonLhsRules; rule; rule = rule->next) {
        checkAndSetExistence(ruleExistenceArray, rule->id);
        probeNonTerminalRulesRecursively(entryRule, rule->rhs[0], symbolExistenceArray, ruleExistenceArray);
    }

    ProductionRule *start = NULL;
    ProductionRule **ppRule = &start;
    for (ProductionRule *curRule = entryRule; curRule; curRule = curRule->next) {
        if (!ruleExistenceArray->array[curRule->id]) continue;
        *ppRule = cloneProductionRules(curRule);
        ppRule = &(*ppRule)->next;
    }

    return start;
}
