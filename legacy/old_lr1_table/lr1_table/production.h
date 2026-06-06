#ifndef PRODUCITON_H
#define PRODUCITON_H

#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "symbol.h"
#include "existanceArray.h"
#include "stringMapper.h"
#include "debug.h"

typedef struct ProductionRule ProductionRule;

struct ProductionRule {
    int id;
    symbol lhs; // Left hand side
    int numSymbols; // the number of elements of production below
    symbol *rhs; // Right hand side
    int dotPos;
    ProductionRule *next;
};

void advanceDot(ProductionRule *prod);

symbol consumeSymbol(ProductionRule *rule);

int getCurrentSymbol(ProductionRule *prod);

int getLeftNonTerminal(ProductionRule *prod);

ProductionRule *filterProductions(ProductionRule *sourceProd, int (*condition)(ProductionRule*), int expectedValue);

ProductionRule *cloneProduction(ProductionRule *prod);

ProductionRule *cloneProductionRules(ProductionRule *rule);

bool compareProductionRules(ProductionRule *rule1, ProductionRule *rule2);

ProductionRule *combineProductions(ProductionRule *prod1, ProductionRule *prod2);

ProductionRule *closure(ProductionRule *startProductionRule, ProductionRule *targetProd);

int reviseNonTerminal(int n);
symbol getLhs(ProductionRule *rule);

#endif