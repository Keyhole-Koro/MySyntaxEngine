#include "GoToTable.h"

int len_column = 0;
int len_row = 0;

GoTo **gotoTable = NULL;
bool **_firstSetsTable = NULL;
bool **_followSetsTable = NULL;

GoToHead *table_head = NULL;
GoToSide *table_side = NULL;

ExistenceArray *isDoneItem;

void traverseLR1Item(LR1Item *item);
void setShift_Goto(LR1Item *item);
void setReduce(LR1Item *item);
void setAcc(LR1Item *item);
int getGoToColumn(symbol target);
void setGoToHead();
void setGotoSide();

GoToTable *genGoToTable(bool **first, bool **follow, LR1Item *entryItem) {
    _firstSetsTable = first;
    _followSetsTable = follow;

    len_column = getNumNonTerminal() + getNumTerminal();
    len_row = getNumItemSets() + 1; // +1 includes state 0
    
    setGoToHead();
    setGotoSide();

    isDoneItem = createExistenceArray(getNumItemSets(), noRevise);

    gotoTable = (GoTo**)malloc(len_row * sizeof(GoTo*));
    for (int i = 0; i < len_row; ++i) {
        gotoTable[i] = (GoTo*)calloc(len_column, sizeof(GoTo));
    }

    traverseLR1Item(entryItem);

    GoToTable *table = malloc(sizeof(GoToTable));
    table->len_head = len_column;
    table->len_side = len_row;
    table->head = table_head;
    table->side = table_side;
    table->goTo = gotoTable;

    return table;
}

void traverseLR1Item(LR1Item *item) {
    setShift_Goto(item);
    setReduce(item);
    setAcc(item);
    if (checkAndSetExistence(isDoneItem, item->stateId)) return;
    for (int i = 0; i < item->numGotoItems; i++) {
        traverseLR1Item(item->gotoItems[i]);
    }
}

void setShift_Goto(LR1Item *item) {
    for (int i = 0; i < item->numGotoItems; i++) {
        LR1Item *gtItem = item->gotoItems[i];

        GoTo *gt = &gotoTable[item->stateId][getGoToColumn(gtItem->lookahead)];
        gt->act = isTerminal(gtItem->lookahead) ? SHIFT : GOTO;
        gt->state = gtItem->stateId;
    }
}

void setReduce(LR1Item *item) {
    for (ProductionRule *prod = item->production; prod; prod = prod->next) {
        if (prod->numSymbols != prod->dotPos) continue;

        symbol lastSym = prod->rhs[prod->dotPos - 1];
        if (isTerminal(lastSym)) lastSym = prod->lhs;

        for (int i = 0; i < arrayLengthTerminal(); i++) {
            if (!_followSetsTable[abs(lastSym)][i]) continue;
            GoTo *gt = &gotoTable[item->stateId][i-1];
            gt->act = (table_head[i-1].kind == _TERMINAL) ? REDUCE : GOTO;
            gt->state = prod->id;
        }
    }
}

void setAcc(LR1Item *item) {
    ProductionRule *accProd = NULL;
    symbol accSym = 0;
    if (isACC(item, &accProd, &accSym)) {
        GoTo *gt = &gotoTable[item->stateId][getGoToColumn(accSym)]; // prod->rhs[i] supposed to be $
        gt->act = ACCEPT;
    }
}

int getGoToColumn(symbol target) {
    if (isTerminal(target)) return target - 1;
    else if (isNonTerminal(target)) return getNumTerminal() + abs(target) - 1;

    exit(EXIT_FAILURE);
}

void setGoToHead() {
    table_head = (GoToHead*)calloc(len_column, sizeof(GoToHead));

    int cur = -1;
    for (int i = 1; i < getNumTerminal() + 1; i++) {
        GoToHead *terminal = &table_head[++cur];
        terminal->kind = _TERMINAL;
        terminal->sym = i;
    }

    for (int i = 1; i < getNumNonTerminal() + 1; i++) {
        GoToHead *nonTerminal = &table_head[++cur];
        nonTerminal->kind = _NONTERMINAL;
        nonTerminal->sym = -1 * i;
    }
}

void setGotoSide() {
    table_side = (GoToSide*)malloc(len_row * sizeof(GoToSide));
    for (int i = 0; i < len_row; i++) {
        table_side[i] = i;
    }
}
