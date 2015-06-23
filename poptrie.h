/*_
 * Copyright (c) 2014-2015 Hirochika Asai <asai@jar.jp>
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

#define POPCNT(v)               __builtin_popcountll(v)
#define ZEROCNT(v)              popcnt(~(v))
#define POPCNT_LS(v, i)         popcnt((v) & (((u64)2 << (i)) - 1))
#define ZEROCNT_LS(v, i)        popcnt((~(v)) & (((u64)2 << (i)) - 1))

/*
 * Radix tree node
 */
struct radix_node {
    int valid;
    struct radix_node *left;
    struct radix_node *right;

    /* Next hop */
    u32 prefix;
    int len;

    /* Propagated route */
    struct radix_node *exit;

    /* Mark for update */
    int mark;
};

/* Internal node */
typedef struct poptrie_node {
    u64 leafvec;
    u64 vector;
    u32 base0;
    u32 base1;
} poptrie_node_t;

/* Leaf node */
typedef u16 poptrie_leaf_t;

struct poptrie_fib {
    void **entries;
    int n;
    int sz;
};

/* Poptrie management data structure */
struct poptrie {
    /* Root */
    u32 root;

    /* FIB */
    struct poptrie_fib fib;

    /* Memory management */
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

#ifdef __cplusplus
extern "C" {
#endif

    /* in poptrie.c */
    struct poptrie * poptrie_init(struct poptrie *, int, int);
    void poptrie_release(struct poptrie *);
    int poptrie_route_add(struct poptrie *, u32, int, u32);
    int poptrie_route_update(struct poptrie *, u32, int, u32);
    int poptrie_route_del(struct poptrie *, u32, int);
    u32 poptrie_lookup(struct poptrie *, u32);
    u32 poptrie_rt_lookup(struct poptrie *, u32);

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
