/*_
 * Copyright (c) 2016 Hirochika Asai <asai@jar.jp>
 * All rights reserved.
 */

#include "../poptrie.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>


/* Macro for testing */
#define TEST_FUNC(str, func, ret)                \
    do {                                         \
        printf("%s: ", str);                     \
        if ( 0 == func() ) {                     \
            printf("passed");                    \
        } else {                                 \
            printf("failed");                    \
            ret = -1;                            \
        }                                        \
        printf("\n");                            \
    } while ( 0 )

#define TEST_PROGRESS()                              \
    do {                                             \
        printf(".");                                 \
        fflush(stdout);                              \
    } while ( 0 )

#define IPV6ADDR(w0, w1, w2, w3, w4, w5, w6, w7)                        \
    (((((__uint128_t)w0 << 48) | ((__uint128_t)w1 << 32)                \
       | ((__uint128_t)w2 << 16) | ((__uint128_t)w3)) << 64)            \
     | (((__uint128_t)w4 << 48) | ((__uint128_t)w5 << 32)               \
        | ((__uint128_t)w6 << 16) | ((__uint128_t)w7)))

static __inline__ __uint128_t
in6_addr_to_uint128(struct in6_addr *in6)
{
    __uint128_t a;
    int i;

    a = 0;
    for ( i = 0; i < 16; i++ ) {
        a <<= 8;
        a |= in6->s6_addr[i];
    }

    return a;
}


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

    TEST_PROGRESS();

    /* Release */
    poptrie_release(poptrie);

    return 0;
}

static int
test_lookup(void)
{
    struct poptrie *poptrie;
    int ret;
    __uint128_t addr;
    void *nexthop;

    /* Initialize */
    poptrie = poptrie_init(NULL, 19, 22);
    if ( NULL == poptrie ) {
        return -1;
    }

    /* No route must be found */
    addr = IPV6ADDR(0x2001, 0xdb8, 0x1, 0x3, 1, 2, 3, 4);
    if ( NULL != poptrie6_lookup(poptrie, addr) ) {
        return -1;
    }

    /* Route add */
    addr = IPV6ADDR(0x2001, 0xdb8, 0x1, 0x0, 0, 0, 0, 0);
    nexthop = (void *)1234;
    ret = poptrie6_route_add(poptrie, addr, 48, nexthop);
    if ( ret < 0 ) {
        /* Failed to add */
        return -1;
    }
    addr = IPV6ADDR(0x2001, 0xdb8, 0x1, 0x3, 1, 2, 3, 4);
    if ( nexthop != poptrie6_lookup(poptrie, addr) ) {
        return -1;
    }
    TEST_PROGRESS();

    /* Route update */
    addr = IPV6ADDR(0x2001, 0xdb8, 0x1, 0x0, 0, 0, 0, 0);
    nexthop = (void *)5678;
    ret = poptrie6_route_update(poptrie, addr, 48, nexthop);
    if ( ret < 0 ) {
        /* Failed to update */
        return -1;
    }
    addr = IPV6ADDR(0x2001, 0xdb8, 0x1, 0x3, 1, 2, 3, 4);
    if ( nexthop != poptrie6_lookup(poptrie, addr) ) {
        return -1;
    }
    TEST_PROGRESS();

    /* Route delete */
    addr = IPV6ADDR(0x2001, 0xdb8, 0x1, 0x0, 0, 0, 0, 0);
    ret = poptrie6_route_del(poptrie, addr, 48);
    if ( ret < 0 ) {
        /* Failed to update */
        return -1;
    }
    addr = IPV6ADDR(0x2001, 0xdb8, 0x1, 0x3, 1, 2, 3, 4);
    if ( NULL != poptrie6_lookup(poptrie, addr) ) {
        return -1;
    }
    TEST_PROGRESS();

    /* Release */
    poptrie_release(poptrie);

    return 0;
}

static int
test_lookup_linx(void)
{
    struct poptrie *poptrie;
    FILE *fp;
    char buf[4096];
    char v6str1[256];
    char v6str2[256];
    int prefix[8];
    int prefixlen;
    int nexthop[8];
    struct in6_addr v6addr;
    int ret;
    __uint128_t addr1;
    __uint128_t addr2;
    u64 i;

    /* Load from the linx file */
    fp = fopen("tests/linx-rib-ipv6.20141225.0000.p69.txt", "r");
    if ( NULL == fp ) {
        return -1;
    }

    /* Initialize */
    poptrie = poptrie_init(NULL, 19, 22);
    if ( NULL == poptrie ) {
        return -1;
    }

    /* Load the full route */
    i = 0;
    while ( !feof(fp) ) {
        if ( !fgets(buf, sizeof(buf), fp) ) {
            continue;
        }
        memset(prefix, 0, sizeof(int) * 8);
        memset(nexthop, 0, sizeof(int) * 8);

        ret = sscanf(buf, "%255[^'/']/%d %255s", v6str1, &prefixlen, v6str2);
        if ( ret < 0 ) {
            return -1;
        }
        ret = inet_pton(AF_INET6, v6str1, &v6addr);
        if ( 1 != ret ) {
            return -1;
        }
        addr1 = in6_addr_to_uint128(&v6addr);
        ret = inet_pton(AF_INET6, v6str2, &v6addr);
        if ( 1 != ret ) {
            return -1;
        }
        addr2 = in6_addr_to_uint128(&v6addr);

        /* Add an entry (use the least significant 64 bits for testing) */
        ret = poptrie6_route_add(poptrie, addr1, prefixlen, (void *)(u64)addr2);
        if ( ret < 0 ) {
            return -1;
        }
        if ( 0 == i % 10000 ) {
            TEST_PROGRESS();
        }
        i++;
    }

    for ( i = 0; i < 0x100000000ULL; i++ ) {
        if ( 0 == i % 0x10000000ULL ) {
            TEST_PROGRESS();
        }
        addr1 = (((__uint128_t)0x2000) << 112) | (((__uint128_t)i) << 92);
        if ( poptrie6_lookup(poptrie, addr1)
             != poptrie6_rib_lookup(poptrie, addr1) ) {
            return -1;
        }
    }

    /* Release */
    poptrie_release(poptrie);

    /* Close */
    fclose(fp);

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
    TEST_FUNC("init6", test_init, ret);
    TEST_FUNC("lookup6", test_lookup, ret);
    TEST_FUNC("lookup6_fullroute", test_lookup_linx, ret);

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
