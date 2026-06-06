#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "conditions.h"
#include "debug.h"

#define IN
#define OUT

char *readUntil(IN bool (*condition)(char), IN char *ipt, OUT char **rest);

char *my_strdup(const char *s);

#endif