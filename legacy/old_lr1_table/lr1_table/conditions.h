#ifndef CONDITIONS_H
#define CONDITIONS_H

#include <stdbool.h>

bool isColon(char ipt);
bool isEnd(char ipt);
bool isPipe(char ipt);
bool isSingleQuote(char ipt);
bool isSpace(char ipt);
bool isOtherThanSpace(char ipt);
bool isAlphabet(char letter);
bool isOtherThanAlphabet(char letter);

#endif