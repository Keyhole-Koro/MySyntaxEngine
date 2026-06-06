#ifndef FIRST_H
#define FIRST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "production.h"
#include "existanceArray.h"
#include "syntax.h"

bool **first(ProductionRule *entryRule);

#endif