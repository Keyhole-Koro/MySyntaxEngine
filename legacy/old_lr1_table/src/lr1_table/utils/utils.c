#include "utils.h"

char *readUntil(bool (*condition)(char), char *ipt, char **rest) {
    size_t buffer_size = 8;

    char *buffer = (char *)malloc(sizeof(char) * buffer_size);

    if (buffer == NULL) {
        DEBUG_PRINT("Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    *buffer = '\0';

    size_t buffer_length = 0;

    while (*ipt && !condition(*ipt)) {
        if (*ipt == ' ') {
            ipt++;
            continue;
        }

        if (buffer_length > buffer_size) {
            buffer_size *= 2;
            buffer = (char *)realloc(buffer, buffer_size * sizeof(char));
            
            if (buffer == NULL) {
                DEBUG_PRINT("Memory reallocation failed\n");
                exit(EXIT_FAILURE);
            }
        }

        buffer[buffer_length++] = *ipt++;
    }

    buffer[buffer_length] = '\0';

    *rest = ipt;

    return buffer;
}

char *my_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy == NULL) {
        return NULL;
    }
    strcpy(copy, s);
    return copy;
}
