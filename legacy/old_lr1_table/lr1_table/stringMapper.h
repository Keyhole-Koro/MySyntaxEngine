#ifndef STRING_MAPPER_H
#define STRING_MAPPER_H

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "symbol.h"
#include "utils.h"

extern int id_nonTerminal;
extern int id_Terminal;

/**
    @brief This method enables to find string efficiently,
    by using number but string, calculation of comparing is improved 
*/

typedef struct StringMapping {
    char *string;
    symbol number;
    struct StringMapping *next;
} StringMapping;

/**
    @param isTerminal if false, non terminal
*/
symbol mapString(char *str, bool isTerminal);

int getNumNonTerminal();
int getNumTerminal();

int arrayLengthNonTerminal();
int arrayLengthTerminal();

void printMapping_Termianl();
void printMapping_Non_Termianl();

void setStringExchange();
char *exchangeSymbol(symbol sym);

bool isTerminal(symbol sym);

bool isNonTerminal(symbol sym);

bool isEOI(symbol sym);

#endif
