#include <stdio.h>
#include "mojocrash.h"

void f4() { printf("crashing!\n"); fflush(stdout); *((int *)0) = 0; }
void f3() { f4(); }
void f2() { f3(); }
void f1() { f2(); }
   
int
main()
{
    if (!MOJOCRASH_install("myapp", "1.0", NULL))
        printf("failed to install!\n");
    else
        f1();
    return 0;
}

