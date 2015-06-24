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

static int
test_lookup(void)
{
    struct poptrie *poptrie;
    int ret;
    void *nexthop;

    /* Initialize */
    poptrie = poptrie_init(NULL, 19, 22);
    if ( NULL == poptrie ) {
        return -1;
    }

    /* No route must be found */
    if ( NULL != poptrie_lookup(poptrie, 0x1c001203) ) {
        return -1;
    }

    /* Route add */
    nexthop = (void *)1234;
    ret = poptrie_route_add(poptrie, 0x1c001200, 24, nexthop);
    if ( ret < 0 ) {
        /* Failed to add */
        return -1;
    }
    if ( nexthop != poptrie_lookup(poptrie, 0x1c001203) ) {
        return -1;
    }

    /* Route update */
    nexthop = (void *)5678;
    ret = poptrie_route_update(poptrie, 0x1c001200, 24, nexthop);
    if ( ret < 0 ) {
        /* Failed to update */
        return -1;
    }
    if ( nexthop != poptrie_lookup(poptrie, 0x1c001203) ) {
        return -1;
    }

    /* Route delete */
    ret = poptrie_route_del(poptrie, 0x1c001200, 24);
    if ( ret < 0 ) {
        /* Failed to update */
        return -1;
    }
    if ( NULL != poptrie_lookup(poptrie, 0x1c001203) ) {
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
    TEST_FUNC("lookup", test_lookup, ret);

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
