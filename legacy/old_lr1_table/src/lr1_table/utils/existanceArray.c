#include "existanceArray.h"

ExistenceArray *createExistenceArray(int capacity, int (*reviseOffset)(int)) {
    if (capacity <= 0) {
        DEBUG_PRINT("Capacity is less than 0 or equal to 0 is invalid");
        exit(EXIT_FAILURE);
    }
    ExistenceArray *exArray = malloc(sizeof(ExistenceArray));
    if (!exArray) {
        DEBUG_PRINT("Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    exArray->array = calloc(capacity, sizeof(bool));
    if (!exArray->array) {
        DEBUG_PRINT("Memory allocation failed\n");
        free(exArray);
        exit(EXIT_FAILURE);
    }
    exArray->capacity = capacity;
    exArray->reviseOffset = reviseOffset;

    for (int i = 0; i < getCapacity(exArray); i++) {
        exArray->array[i] = false;
    }

    return exArray;
}

int getCapacity(ExistenceArray *exArray) {
    return exArray->capacity;
}

void freeExistenceArray(ExistenceArray *exArray) {
    if (exArray) {
        free(exArray->array);
        free(exArray);
    }
}

bool checkAndSetExistence(ExistenceArray *exArray, int value) {
    int offset = exArray->reviseOffset(value);
    if (offset < 0 || offset > exArray->capacity) {
        DEBUG_PRINT("Offset out of bounds\n");
        exit(EXIT_FAILURE);
    }
    if (exArray->array[offset]) return true;
    exArray->array[offset] = true;

    return false;
}

// turn the overlapping part into false
// params are required to have the same property
void eliminateOverlapsExstanceArray(ExistenceArray *referentArray, ExistenceArray *eliminatedArray) {
    if (getCapacity(referentArray) != getCapacity(eliminatedArray)) DEBUG_PRINT("The combination of the params are not allowed");
    for (int i = 0; i < getCapacity(referentArray); i++) {
        if (!referentArray->array[i]) continue;

        eliminatedArray->array[i] = false;
    }
}

int noRevise(int n) {
    return n;
}
