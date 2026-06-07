#include "syntax_engine_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *lr1_xstrdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (!copy) {
        fprintf(stderr, "out of memory\n");
        exit(EXIT_FAILURE);
    }
    memcpy(copy, s, len);
    return copy;
}

void *lr1_xcalloc(size_t count, size_t size) {
    void *ptr = calloc(count, size);
    if (!ptr) {
        fprintf(stderr, "out of memory\n");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *lr1_xrealloc(void *ptr, size_t size) {
    void *resized = realloc(ptr, size);
    if (!resized) {
        fprintf(stderr, "out of memory\n");
        exit(EXIT_FAILURE);
    }
    return resized;
}

char *lr1_trim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }
    return s;
}
