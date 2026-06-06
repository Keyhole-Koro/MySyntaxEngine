#ifndef EXISTANCE_ARRAY_H
#define EXISTANCE_ARRAY_H

#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"

typedef struct {
    bool *array;
    int capacity;
    int (*reviseOffset)(int);
} ExistenceArray;

#include "existanceArray.h"

ExistenceArray* createExistenceArray(int capacity, int (*reviseOffset)(int));

int getCapacity(ExistenceArray *exArray);

void freeExistenceArray(ExistenceArray *exArray);

bool checkAndSetExistence(ExistenceArray *exArray, int value);

void eliminateOverlapsExstanceArray(ExistenceArray *referentArray, ExistenceArray *eliminatedArray);

int noRevise(int n);

#endif