#ifndef ITEM_H
#define ITEM_H

#include "syntax.h"

#include "stringMapper.h"
#include "existanceArray.h"
#include "production.h"

typedef struct LR1Item {
    int stateId;
    ProductionRule *production;
    symbol lookahead;
    int numGotoItems;
    int maxGotoItems;
    struct LR1Item **gotoItems;
} LR1Item;

void setStartRule(const ProductionRule *entryRule);
LR1Item *constructInitialItemSet();

int getNumItemSets();

void enable_item_debug();

bool isACC(LR1Item *item, ProductionRule **accProd, symbol *accSym);

#endif