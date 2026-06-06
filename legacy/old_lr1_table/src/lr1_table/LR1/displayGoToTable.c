#include "displayGoToTable.h"

void printCell(const char *str, int width);

const int CELL_WIDTH = 4;

void displayGotoTable(GoToTable *table) {
    GoToHead *head = table->head;
    GoTo **goTo = table->goTo;
    
    int len_column = table->len_head;
    
    for (int i = 0; i < CELL_WIDTH; i++) {
        printf(" ");
    }
    printf("|");
    for (int i = 0; i < len_column; ++i) {
        char symbolStr[CELL_WIDTH];
        if (head[i].kind == _TERMINATE) snprintf(symbolStr, CELL_WIDTH, "$");
        else snprintf(symbolStr, CELL_WIDTH, "%s", exchangeSymbol(head[i].sym));
        printCell(symbolStr, CELL_WIDTH);
    }
    printf("\n");

    for (int i = 0; i < table->len_side; ++i) {
        char stateStr[CELL_WIDTH];
        snprintf(stateStr, CELL_WIDTH, "%i", i);
        printCell(stateStr, CELL_WIDTH);
        for (int j = 0; j < len_column; ++j) {
            char actionStr[CELL_WIDTH];
            switch (goTo[i][j].act) {
            case SHIFT:
                snprintf(actionStr, CELL_WIDTH, "S%d", goTo[i][j].state);
                break;
            case REDUCE:
                snprintf(actionStr, CELL_WIDTH, "R%d", goTo[i][j].state);
                break;
            case ACCEPT:
                snprintf(actionStr, CELL_WIDTH, "acc");
                break;
            case GOTO:
                snprintf(actionStr, CELL_WIDTH, "%d", goTo[i][j].state);
                break;
            default:
                snprintf(actionStr, CELL_WIDTH, "");
                break;
            }
            printCell(actionStr, CELL_WIDTH);
        }
        printf("\n");
    }
}

void printCell(const char *str, int width) {
    printf("%-*s|", width, str);
}