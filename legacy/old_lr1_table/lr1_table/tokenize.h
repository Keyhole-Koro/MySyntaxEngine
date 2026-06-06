#ifndef TOKENIZE_H
#define TOKENIZE_H

#include "utils.h"

typedef enum {
    TERMINAL,
    NON_TERMINAL,
    EOI, // $
    COLON,
    PIPE,
    EQUAL,
    PATTERN, // regular expression
    NEWLINE,
} TokenKind;

typedef struct Token Token;
struct Token {
    TokenKind kind;
    char *value;
    Token *next;
};

Token *tokenizeLine(char *ipt, Token *cur);
void printTokenList(Token *head);



#endif