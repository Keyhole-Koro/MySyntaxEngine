#ifndef DEBUG_H
#define DEBUG_H

#define DEBUG_PRINT(fmt, ...) \
    do { \
        fprintf(stderr, "[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)
    
#endif