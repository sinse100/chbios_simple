#include <stdio.h>
#include "module2.h"

int module2_add(int a, int b)
{
    return a + b;
}

void module2_print(const char *msg)
{
    printf("module2: %s\n", msg);
}
