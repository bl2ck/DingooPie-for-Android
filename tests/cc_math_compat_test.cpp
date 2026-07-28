#include "cc/cc_math_compat.h"

#include <stdio.h>

int main()
{
    uint64_t quotient = 0;
    uint64_t remainder = 0;
    ccDivideUnsigned64LikeCc1800(100u, 7u, &quotient, &remainder);
    if (quotient != 14u || remainder != 2u)
    {
        fprintf(stderr, "CC1800 unsigned divide regression failed\n");
        return 1;
    }

    ccDivideUnsigned64LikeCc1800(0x8000000000000000ull, 1u,
        &quotient, &remainder);
    if (quotient != 1u || remainder != 0x7fffffffffffffffull)
    {
        fprintf(stderr, "CC1800 bit63 divide compatibility failed\n");
        return 2;
    }

    printf("CC math compatibility regression passed.\n");
    return 0;
}
