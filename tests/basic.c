/*_
 * Copyright (c) 2014-2015 Hirochika Asai <asai@jar.jp>
 * All rights reserved.
 */

#include "../poptrie.h"
#include <stdio.h>
#include <stdlib.h>

/* Macro for testing */
#define TEST_FUNC(str, func, ret)               \
    do {                                        \
        printf("%s: ", str);                    \
        if ( 0 == func() ) {                    \
            printf("passed");                   \
        } else {                                \
            printf("failed");                   \
            ret = -1;                           \
        }                                       \
        printf("\n");                           \
    } while ( 0 )

/*
 * Initialization test
 */
static int
test_init(void)
{
    struct poptrie *poptrie;

    /* Initialize */
    poptrie = poptrie_init(NULL, 19, 22);
    if ( NULL == poptrie ) {
        return -1;
    }

    /* Release */
    poptrie_release(poptrie);

    return 0;
}

/*
 * Main routine for the basic test
 */
int
main(int argc, const char *const argv[])
{
    int ret;

    ret = 0;

    /* Run tests */
    TEST_FUNC("init", test_init, ret);

    return ret;
}

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
