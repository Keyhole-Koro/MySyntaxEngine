#include "tokenize.h"

Token *makeToken(Token* cur, TokenKind kind, char *value);

Token *tokenizeLine(char *ipt, Token *cur) {
    char *rest;

    while (*ipt) {

        if (*ipt == ' ') {
            ipt++;
            continue;
        }

        if (*ipt == '|') {
            cur = makeToken(cur, PIPE, NULL);
            ipt++;
            continue;
        }

        if (*ipt == ':') {
            cur = makeToken(cur, COLON, NULL);
            ipt++;
            continue;
        }

        if (isAlphabet(*ipt)) {
            cur = makeToken(cur, NON_TERMINAL, readUntil(isOtherThanAlphabet, ipt, &rest));
            ipt = rest;
            continue;
        }

        if (*ipt == '\'') {
            cur = makeToken(cur, TERMINAL, readUntil(isSingleQuote, ++ipt, &rest));
            ipt = ++rest;
            continue;
        }

        if (*ipt == '$') {
            cur = makeToken(cur, EOI, NULL);
            ipt++;
            continue;
        }

        if (*ipt == "=") {
            cur = makeToken(cur, EQUAL, NULL);
            ipt++;
            
            cur = makeToken(cur, PATTERN, readUntil(isEnd, ipt, &rest));
            ipt = rest;
            continue;
        }

        ipt++;
    }

    cur = makeToken(cur, NEWLINE, NULL);
    
    return cur;
}

Token *makeToken(Token* prev, TokenKind kind, char *value) {
    Token *new_tk = malloc(sizeof(Token));
    if (new_tk == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    new_tk->kind = kind;
    new_tk->value = value;
    new_tk->next = NULL;
    prev->next = new_tk;
    
    return new_tk;
}

void printTokenList(Token *head) {
    Token *current = head;
    while (current != NULL) {
        printf("Kind: %d, Value: %s\n", current->kind, current->value);
        current = current->next;
    }
}