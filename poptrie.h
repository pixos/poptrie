/*_
 * Copyright (c) 2014-2017 Hirochika Asai <asai@jar.jp>
 * All rights reserved.
 */

#ifndef _POPTRIE_H
#define _POPTRIE_H

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;


/* The bit length used for direct pointing.  The most significant POPTRIE_S bits
   of keys will be tested at the first stage of the trie search in O(1). */
#define POPTRIE_S               18
/* The initial size of forwarding information base (FIB).  In the current
   version of this software, new entries exceeding this size will result in an
   error.  This parameter must be less than 65535. */
#define POPTRIE_INIT_FIB_SIZE   4096


/* 64-bit popcnt intrinsic.  To use popcnt instruction in x86-64, the "-mpopcnt"
   option must be specified in CFLAGS. */
#define popcnt(v)               __builtin_popcountll(v)


/* Internal node; 24-byte data structure */
typedef struct poptrie_node {
    /* Leafvec */
    u64 leafvec;
    /* Vector */
    u64 vector;
    /* Base for leaf nodes */
    u32 base0;
    /* Base for descendant internal nodes */
    u32 base1;
} poptrie_node_t;

/* Leaf node; 16-bit value */
typedef u16 poptrie_leaf_t;

/* FIB index */
typedef u16 poptrie_fib_index_t;

/*
 * Radix tree node
 */
struct radix_node {
    int valid;
    struct radix_node *left;
    struct radix_node *right;

    /* Next hop */
    int len;
    poptrie_leaf_t nexthop;

    /* Propagated route for invalid intermediate nodes from a valid parent */
    struct radix_node *ext;

    /* Mark for update */
    int mark;
};

/*
 * FIB mapping table
 */
struct poptrie_fib_entry {
    void *entry;
    int refs;
};
struct poptrie_fib {
    struct poptrie_fib_entry *entries;
    int sz;
};

/*
 * Poptrie management data structure
 */
struct poptrie {
    /* Root */
    u32 root;

    /* FIB */
    struct poptrie_fib fib;

    /* Memory management data structure for buddy system */
    poptrie_node_t *nodes;
    poptrie_leaf_t *leaves;
    void *cnodes;
    void *cleaves;

    /* Allocated sizes for internal nodes and leaves */
    int nodesz;
    int leafsz;

    /* Array for direct pointing */
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
    int poptrie_route_add(struct poptrie *, u32, int, void *);
    int poptrie_route_change(struct poptrie *, u32, int, void *);
    int poptrie_route_update(struct poptrie *, u32, int, void *);
    int poptrie_route_del(struct poptrie *, u32, int);
    void * poptrie_lookup(struct poptrie *, u32);
    void * poptrie_rib_lookup(struct poptrie *, u32);

    /* in poptrie6.c */
    int poptrie6_route_add(struct poptrie *, __uint128_t, int, void *);
    int poptrie6_route_change(struct poptrie *, __uint128_t, int, void *);
    int poptrie6_route_update(struct poptrie *, __uint128_t, int, void *);
    int poptrie6_route_del(struct poptrie *, __uint128_t, int);
    void * poptrie6_lookup(struct poptrie *, __uint128_t);
    void * poptrie6_rib_lookup(struct poptrie *, __uint128_t);

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
