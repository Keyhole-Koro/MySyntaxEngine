#include "conditions.h"

inline bool isColon(char ipt) {
    return ipt == ':';
}

inline bool isEnd(char ipt) {
    return ipt == '\n';
}

inline bool isPipe(char ipt) {
    return ipt == '|';
}

inline bool isSingleQuote(char ipt) {
    return ipt == '\'';
}

inline bool isSpace(char ipt) {
    return ipt == ' ';
}

inline bool isOtherThanSpace(char ipt) {
    return ipt != ' ';
}

inline bool isAlphabet(char letter) {
    return ((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z'));
}

inline bool isOtherThanAlphabet(char letter) {
    return !isAlphabet(letter);
}