
#include <stdio.h>
#include "module1.h"
#include "module2.h"

void module1_run(void)
{
    module2_print("called from module1_run()");
}

int module1_compute(int x)
{
    int result = module2_add(x, 10);
    printf("module1: result = %d\n", result);
    return result;
}
