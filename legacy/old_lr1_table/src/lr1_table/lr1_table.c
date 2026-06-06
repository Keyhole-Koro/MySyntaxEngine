#include "lr1_table.h"

#include "syntax.h"
#include "stringMapper.h"
#include "production.h"
#include "item.h"
#include "first.h"
#include "follow.h"
#include "displayGoToTable.h"

GoToTable *lr1_table(char *syntax_path) {

    ProductionRule *prods = processSyntaxTxt(syntax_path);

    showProductionRules();

    bool **firstSets = first(prods);

    bool **followSets = follow(prods, firstSets);    

    //enable_item_debug();
    setStartRule(prods);
    LR1Item *entryItem = constructInitialItemSet();

    GoToTable *gototable = genGoToTable(firstSets, followSets, entryItem);

    displayGotoTable(gototable);

    //printMapping_Non_Terminal();

    //printMapping_Terminal();

    return gototable;
}