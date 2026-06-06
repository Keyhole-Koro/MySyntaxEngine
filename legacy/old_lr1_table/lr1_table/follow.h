#ifndef FOLLOW_H
#define FOLLOW_H

#include "first.h"
#include "production.h"

bool **follow(ProductionRule *_entryRule, bool **_firstSetsTable);

#endif