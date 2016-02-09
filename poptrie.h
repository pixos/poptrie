/*_
 * Copyright (c) 2014-2016 Hirochika Asai <asai@jar.jp>
 * All rights reserved.
 */

#ifndef _POPTRIE_H
#define _POPTRIE_H

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;


#define POPTRIE_S               18
#define POPTRIE_INIT_FIB_SIZE   4096

#define popcnt(v)               __builtin_popcountll(v)


/* Internal node */
typedef struct poptrie_node {
    u64 leafvec;
    u64 vector;
    u32 base0;
    u32 base1;
} poptrie_node_t;

/* Leaf node */
typedef u16 poptrie_leaf_t;

/*
 * Radix tree node for IPv4
 */
struct radix_node {
    int valid;
    struct radix_node *left;
    struct radix_node *right;

    /* Next hop */
    u32 prefix;
    int len;
    poptrie_leaf_t nexthop;

    /* Propagated route */
    struct radix_node *ext;

    /* Mark for update */
    int mark;
};

/*
 * Radix tree node for IPv6
 */
struct radix_node6 {
    int valid;
    struct radix_node6 *left;
    struct radix_node6 *right;

    /* Next hop */
    __uint128_t prefix;
    int len;
    poptrie_leaf_t nexthop;

    /* Propagated route */
    struct radix_node6 *ext;

    /* Mark for update */
    int mark;
};

/*
 * FIB mapping table
 */
struct poptrie_fib {
    void **entries;
    int n;
    int sz;
};

/*
 * Poptrie management data structure for IPv4
 */
struct poptrie {
    /* Root */
    u32 root;

    /* FIB */
    struct poptrie_fib fib;

    /* Memory management */
    poptrie_node_t *nodes;
    poptrie_leaf_t *leaves;
    void *cnodes;
    void *cleaves;

    /* Size */
    int nodesz;
    int leafsz;

    /* Direct pointing */
    u32 *dir;
    u32 *altdir;

    /* RIB */
    struct radix_node *radix;

    /* Control */
    int _allocated;
};

/*
 * Poptrie management data structure for IPv6
 */
struct poptrie6 {
    /* Root */
    u32 root;

    /* FIB */
    struct poptrie_fib fib;

    /* Memory management */
    poptrie_node_t *nodes;
    poptrie_leaf_t *leaves;
    void *cnodes;
    void *cleaves;

    /* Size */
    int nodesz;
    int leafsz;

    /* Direct pointing */
    u32 *dir;
    u32 *altdir;

    /* RIB */
    struct radix_node6 *radix;

    /* Control */
    int _allocated;
};

#ifdef __cplusplus
extern "C" {
#endif

    /* in poptrie.c */
    struct poptrie * poptrie_init(struct poptrie *, int, int);
    void poptrie_release(struct poptrie *);
    int poptrie_route_add(struct poptrie *, u32, int, void *);
    int poptrie_route_change(struct poptrie *, u32, int, void *);
    int poptrie_route_update(struct poptrie *, u32, int, void *);
    int poptrie_route_del(struct poptrie *, u32, int);
    void * poptrie_lookup(struct poptrie *, u32);
    void * poptrie_rib_lookup(struct poptrie *, u32);

    /* in poptrie6.c */
    struct poptrie6 * poptrie6_init(struct poptrie6 *, int, int);
    void poptrie6_release(struct poptrie6 *);
    int poptrie6_route_add(struct poptrie6 *, __uint128_t, int, void *);
    int poptrie6_route_change(struct poptrie6 *, __uint128_t, int, void *);
    int poptrie6_route_update(struct poptrie6 *, __uint128_t, int, void *);
    int poptrie6_route_del(struct poptrie6 *, __uint128_t, int);
    void * poptrie6_lookup(struct poptrie6 *, __uint128_t);
    void * poptrie6_rib_lookup(struct poptrie6 *, __uint128_t);

#ifdef __cplusplus
}
#endif

#endif /* _POPTRIE_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
