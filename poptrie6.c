/*_
 * Copyright (c) 2016 Hirochika Asai <asai@jar.jp>
 * All rights reserved.
 */

#include "buddy.h"
#include "poptrie.h"
#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#define EXT_NH(n)       ((n)->ext ? (n)->ext->nexthop : 0)
static __inline__ __uint128_t
INDEX(__uint128_t a, int s, int n)
{
    if ( 0 == ((s) + (n)) ) {
        return 0;
    } else {
        return ((a) >> (128 - ((s) + (n)))) & ((1ULL << (n)) - 1);
    }
}
#define VEC_INIT(v)     ((v) = 0)
#define VEC_BT(v, i)    ((v) & (__uint128_t)1 << (i))
#define BITINDEX(v)     ((v) & ((1 << 6) - 1))
#define NODEINDEX(v)    ((v) >> 6)
#define VEC_SET(v, i)   ((v) |= (__uint128_t)1 << (i))
#define VEC_CLEAR(v, i) ((v) &= ~((__uint128_t)1 << (i)))
#define POPCNT(v)       popcnt(v)
#define ZEROCNT(v)      popcnt(~(v))
#define POPCNT_LS(v, i) popcnt((v) & (((u64)2 << (i)) - 1))
#define ZEROCNT_LS(v, i) popcnt((~(v)) & (((u64)2 << (i)) - 1))

struct poptrie_stack {
    int inode;
    int idx;
    int width;
    poptrie_leaf_t nexthop;
};


/* Prototype declarations */
static int
_route_add(struct poptrie6 *, struct radix_node6 **, __uint128_t, int,
           poptrie_leaf_t, int, struct radix_node6 *);
static int _route_add_propagate(struct radix_node6 *, struct radix_node6 *);
static int
_update_part(struct poptrie6 *, struct radix_node6 *, int,
             struct poptrie_stack *, u32 *, int);
static int
_update_subtree(struct poptrie6 *, struct radix_node6 *, __uint128_t, int);
static int
_descend_and_update(struct poptrie6 *, struct radix_node6 *, int,
                    struct poptrie_stack *, __uint128_t, int, int, u32 *);
static int
_update_inode_chunk(struct poptrie6 *, struct radix_node6 *, int,
                    poptrie_node_t *, poptrie_leaf_t *);
static int
_update_inode_chunk_rec(struct poptrie6 *, struct radix_node6 *, int,
                        poptrie_node_t *, poptrie_leaf_t *, int, int);
static int
_update_inode(struct poptrie6 *, struct radix_node6 *, int, poptrie_node_t *,
              poptrie_leaf_t *);
static int
_update_dp1(struct poptrie6 *, struct radix_node6 *, int, __uint128_t, int,
            int);
static int
_update_dp2(struct poptrie6 *, struct radix_node6 *, int, __uint128_t, int,
            int);
static void _update_clean_root(struct poptrie6 *, int, int);
static void _update_clean_node(struct poptrie6 *, poptrie_node_t *, int);
static void _update_clean_inode(struct poptrie6 *, int, int);
static void _update_clean_subtree(struct poptrie6 *, int);
static struct radix_node6 * _next_block(struct radix_node6 *, int, int, int);
static void
_parse_triangle(struct radix_node6 *, u64 *, struct radix_node6 *, int, int);
static void _clear_mark(struct radix_node6 *);
static int _route_change_propagate(struct radix_node6 *, struct radix_node6 *);
static int
_route_change(struct poptrie6 *, struct radix_node6 **, __uint128_t, int,
              poptrie_leaf_t, int);
static int
_route_update(struct poptrie6 *, struct radix_node6 **, __uint128_t, int,
              poptrie_leaf_t, int, struct radix_node6 *);
static int
_route_del(struct poptrie6 *, struct radix_node6 **, __uint128_t, int, int,
           struct radix_node6 *);
static int
_route_del_propagate(struct radix_node6 *, struct radix_node6 *,
                     struct radix_node6 *);
static u32
_rib_lookup(struct radix_node6 *, __uint128_t, int, struct radix_node6 *);
static void _release_radix(struct radix_node6 *);

/*
 * Bit scan
 */
static __inline__ int
bsr(u64 x)
{
    u64 r;

    if ( !x ) {
        return 0;
    }
    __asm__ __volatile__ ( " bsrq %1,%0 " : "=r"(r) : "r"(x) );

    return r;
}


/*
 * Initialize the poptrie data structure
 */
struct poptrie6 *
poptrie6_init(struct poptrie6 *poptrie, int sz1, int sz0)
{
    int ret;
    int i;

    if ( NULL == poptrie ) {
        /* Allocate new one */
        poptrie = malloc(sizeof(struct poptrie6));
        if ( NULL == poptrie ) {
            return NULL;
        }
        (void)memset(poptrie, 0, sizeof(struct poptrie6));
        /* Set the flag indicating that this data structure needs free() when
           released. */
        poptrie->_allocated = 1;
    } else {
        /* Write zero's */
        (void)memset(poptrie, 0, sizeof(struct poptrie6));
    }

    /* Allocate the nodes and leaves */
    poptrie->nodes = malloc(sizeof(poptrie_node_t) * (1 << sz1));
    if ( NULL == poptrie->nodes ) {
        poptrie6_release(poptrie);
        return NULL;
    }
    poptrie->leaves = malloc(sizeof(poptrie_leaf_t) * (1 << sz0));
    if ( NULL == poptrie->leaves ) {
        poptrie6_release(poptrie);
        return NULL;
    }

    /* Prepare the buddy system for the internal node array */
    poptrie->cnodes = malloc(sizeof(struct buddy));
    if ( NULL == poptrie->cnodes ) {
        poptrie6_release(poptrie);
        return NULL;
    }
    ret = buddy_init(poptrie->cnodes, sz1, sz1, sizeof(u32));
    if ( ret < 0 ) {
        free(poptrie->cnodes);
        poptrie->cnodes = NULL;
        poptrie6_release(poptrie);
        return NULL;
    }

    /* Prepare the buddy system for the leaf node array */
    poptrie->cleaves = malloc(sizeof(struct buddy));
    if ( NULL == poptrie->cleaves ) {
        poptrie6_release(poptrie);
        return NULL;
    }
    ret = buddy_init(poptrie->cleaves, sz0, sz0, sizeof(u32));
    if ( ret < 0 ) {
        free(poptrie->cnodes);
        poptrie->cnodes = NULL;
        poptrie6_release(poptrie);
        return NULL;
    }

    /* Prepare the direct pointing array */
    poptrie->dir = malloc(sizeof(u32) << POPTRIE_S);
    if ( NULL == poptrie->dir ) {
        poptrie6_release(poptrie);
        return NULL;
    }
    for ( i = 0; i < (1 << POPTRIE_S); i++ ) {
        poptrie->dir[i] = (u32)1 << 31;
    }

    /* Prepare the alternative direct pointing array for the update procedure */
    poptrie->altdir = malloc(sizeof(u32) << POPTRIE_S);
    if ( NULL == poptrie->altdir ) {
        poptrie6_release(poptrie);
        return NULL;
    }

    /* Prepare the FIB mapping table */
    poptrie->fib.entries = malloc(sizeof(void *) * POPTRIE_INIT_FIB_SIZE);
    if ( NULL == poptrie->fib.entries ) {
        poptrie6_release(poptrie);
        return NULL;
    }
    poptrie->fib.sz = POPTRIE_INIT_FIB_SIZE;
    poptrie->fib.n = 0;
    /* Insert a NULL entry */
    poptrie->fib.entries[poptrie->fib.n++] = NULL;

    return poptrie;
}

/*
 * Release the poptrie data structure
 */
void
poptrie6_release(struct poptrie6 *poptrie)
{
    /* Release the radix tree */
    _release_radix(poptrie->radix);

    if ( poptrie->nodes ) {
        free(poptrie->nodes);
    }
    if ( poptrie->leaves ) {
        free(poptrie->leaves);
    }
    if ( poptrie->cnodes ) {
        buddy_release(poptrie->cnodes);
        free(poptrie->cnodes);
    }
    if ( poptrie->cleaves ) {
        buddy_release(poptrie->cleaves);
        free(poptrie->cleaves);
    }
    if ( poptrie->dir ) {
        free(poptrie->dir);
    }
    if ( poptrie->altdir ) {
        free(poptrie->altdir);
    }
    if ( poptrie->fib.entries ) {
        free(poptrie->fib.entries);
    }
    if ( poptrie->_allocated ) {
        free(poptrie);
    }
}

/*
 * Add a route
 */
int
poptrie6_route_add(struct poptrie6 *poptrie, __uint128_t prefix, int len,
                   void *nexthop)
{
    int ret;
    int i;
    int n;

    /* Find the FIB entry mapping first */
    for ( i = 0; i < poptrie->fib.n; i++ ) {
        if ( poptrie->fib.entries[i] == nexthop ) {
            /* Found the matched entry */
            n = i;
            break;
        }
    }
    if ( i == poptrie->fib.n ) {
        /* No matching FIB entry was found */
        if ( poptrie->fib.n >= poptrie->fib.sz ) {
            /* The FIB mapping table is full */
            return -1;
        }
        /* Append new FIB entry */
        n = poptrie->fib.n;
        poptrie->fib.entries[n] = nexthop;
        poptrie->fib.n++;
    }

    /* Insert the prefix to the radix tree, then incrementally update the
       poptrie data structure */
    ret = _route_add(poptrie, &poptrie->radix, prefix, len, n, 0, NULL);
    if ( ret < 0 ) {
        return ret;
    }

    return 0;
}

/*
 * Change a route
 */
int
poptrie6_route_change(struct poptrie6 *poptrie, __uint128_t prefix, int len,
                     void *nexthop)
{
    int i;
    int n;

    for ( i = 0; i < poptrie->fib.n; i++ ) {
        if ( poptrie->fib.entries[i] == nexthop ) {
            n = i;
            break;
        }
    }
    if ( i == poptrie->fib.n ) {
        n = poptrie->fib.n;
        poptrie->fib.entries[n] = nexthop;
        poptrie->fib.n++;
    }

    return _route_change(poptrie, &poptrie->radix, prefix, len, n, 0);
}

/*
 * Update a route (add if not exists like BGP update)
 */
int
poptrie6_route_update(struct poptrie6 *poptrie, __uint128_t prefix, int len,
                      void *nexthop)
{
    int ret;
    int i;
    int n;

    for ( i = 0; i < poptrie->fib.n; i++ ) {
        if ( poptrie->fib.entries[i] == nexthop ) {
            n = i;
            break;
        }
    }
    if ( i == poptrie->fib.n ) {
        n = poptrie->fib.n;
        poptrie->fib.entries[n] = nexthop;
        poptrie->fib.n++;
    }

    /* Insert to the radix tree */
    ret = _route_update(poptrie, &poptrie->radix, prefix, len, n, 0, NULL);
    if ( ret < 0 ) {
        return ret;
    }

    return 0;
}

/*
 * Delete a route
 */
int
poptrie6_route_del(struct poptrie6 *poptrie, __uint128_t prefix, int len)
{
    /* Search and delete the corresponding entry */
    return _route_del(poptrie, &poptrie->radix, prefix, len, 0, NULL);
}

/*
 * Lookup a route by the specified address
 */
void *
poptrie6_lookup(struct poptrie6 *poptrie, __uint128_t addr)
{
    int inode;
    int base;
    int idx;
    int pos;

    /* Top tier */
    idx = INDEX(addr, 0, POPTRIE_S);
    pos = POPTRIE_S;
    base = poptrie->root;

    /* Direct pointing */
    if ( poptrie->dir[idx] & ((u32)1 << 31) ) {
        return poptrie->fib.entries[poptrie->dir[idx] & (((u32)1 << 31) - 1)];
    } else {
        base = poptrie->dir[idx];
        idx = INDEX(addr, pos, 6);
        pos += 6;
    }

    for ( ;; ) {
        inode = base;
        if ( VEC_BT(poptrie->nodes[inode].vector, idx) ) {
            /* Internal node */
            base = poptrie->nodes[inode].base1;
            idx = POPCNT_LS(poptrie->nodes[inode].vector, idx);
            /* Next internal node index */
            base = base + (idx - 1);
            /* Next node vector */
            idx = INDEX(addr, pos, 6);
            pos += 6;
        } else {
            /* Leaf */
            base = poptrie->nodes[inode].base0;
            idx = POPCNT_LS(poptrie->nodes[inode].leafvec, idx);
            return poptrie->fib.entries[poptrie->leaves[base + idx - 1]];
        }
    }

    /* Not to be reached here, but put this to dismiss a compiler warning. */
    return 0;
}

/*
 * Lookup the next hop from the radix tree (RIB table)
 */
void *
poptrie6_rib_lookup(struct poptrie6 *poptrie, __uint128_t addr)
{
    return poptrie->fib.entries[_rib_lookup(poptrie->radix, addr, 0, NULL)];
}

/*
 * Update the partial tree
 */
static int
_update_part(struct poptrie6 *poptrie, struct radix_node6 *tnode, int inode,
             struct poptrie_stack *stack, u32 *root, int alt)
{
    struct poptrie_node *cnodes;
    int ret;
    poptrie_leaf_t sleaf;
    int vcomp;
    int nroot;
    int oroot;
    int p;
    int n;
    int base1;
    int base0;
    int i;
    int j;
    poptrie_leaf_t leaves[1 << 6];
    u64 prev;
    struct poptrie_node *node;
    u64 vector;
    u64 leafvec;

    stack--;

    /* Build the updated part */
    if ( stack->idx < 0 ) {
        cnodes = alloca(sizeof(struct poptrie_node));
        if ( NULL == cnodes ) {
            return -1;
        }
        ret = _update_inode_chunk_rec(poptrie, tnode, inode, cnodes, &sleaf, 0,
                                      0);
        if ( ret < 0 ) {
            return -1;
        }
        if ( ret > 0 ) {
            /* Clean */
            buddy_free2(poptrie->cleaves, cnodes[0].base0);
            cnodes[0].base0 = -1;

            /* Replace the root with an atomic instruction */
            nroot = ((u32)1 << 31) | sleaf;
            __asm__ __volatile__ ("lock xchgl %%eax,%0"
                                  : "=m"(*root), "=a"(oroot) : "a"(nroot));
            if ( !alt ) {
                _update_clean_subtree(poptrie, oroot);
                if ( (int)oroot >= 0 ) {
                    buddy_free2(poptrie->cnodes, oroot);
                }
            }

            return 0;
        }

        /* Replace the root */
        nroot = buddy_alloc2(poptrie->cnodes, 0);
        if ( nroot < 0 ) {
            return -1;
        }
        memcpy(poptrie->nodes + nroot, cnodes, sizeof(struct poptrie_node));
        oroot = poptrie->root;
        poptrie->root = nroot;

        /* Replace the root with an atomic instruction */
        __asm__ __volatile__ ("lock xchgl %%eax,%0"
                              : "=m"(*root), "=a"(oroot) : "a"(nroot));

        /* Clean */
        if ( !alt && !(oroot & ((u32)1 << 31)) ) {
            _update_clean_root(poptrie, nroot, oroot);
        }

        return 0;
    }

    /* Allocate */
#if POPTRIE_S < 6
    cnodes = alloca(sizeof(struct poptrie_node));
#else
    cnodes = alloca(sizeof(struct poptrie_node) << (POPTRIE_S - 6));
#endif
    if ( NULL == cnodes ) {
        return -1;
    }

    /* Not the root */
    ret = _update_inode_chunk_rec(poptrie, tnode, inode, cnodes, &sleaf, 0, 0);
    if ( ret < 0 ) {
        return -1;
    }
    if ( ret > 0 ) {
        vcomp = 1;
        buddy_free2(poptrie->cleaves, cnodes[0].base0);
        cnodes[0].base0 = -1;
    } else {
        vcomp = 0;
    }

    while ( vcomp && stack->idx >= 0 ) {
        /* Perform vertical compresion */
        if ( stack->inode < 0 ) {
            if ( stack->nexthop != sleaf ) {
                /* Compression ends here */
                vcomp = 0;
                for ( i = 0; i < (1 << (stack->width - 6)); i++ ) {
                    VEC_INIT(cnodes[i].vector);
                    VEC_INIT(cnodes[i].leafvec);
                    if ( i == NODEINDEX(stack->idx) ) {
                        if ( 0 == BITINDEX(stack->idx) ) {
                            base0 = buddy_alloc2(poptrie->cleaves, 1);
                            if ( base0 < 0 ) {
                                return -1;
                            }
                            poptrie->leaves[base0] = sleaf;
                            poptrie->leaves[base0 + 1] = stack->nexthop;
                            VEC_SET(cnodes[i].leafvec, 0);
                            VEC_SET(cnodes[i].leafvec, 1);
                        } else if ( ((1 << 6) - 1) == BITINDEX(stack->idx) ) {
                            base0 = buddy_alloc2(poptrie->cleaves, 1);
                            if ( base0 < 0 ) {
                                return -1;
                            }
                            poptrie->leaves[base0] = stack->nexthop;
                            poptrie->leaves[base0 + 1] = sleaf;
                            VEC_SET(cnodes[i].leafvec, 0);
                            VEC_SET(cnodes[i].leafvec, BITINDEX(stack->idx));
                        } else {
                            base0 = buddy_alloc2(poptrie->cleaves, 2);
                            if ( base0 < 0 ) {
                                return -1;
                            }
                            poptrie->leaves[base0] = stack->nexthop;
                            poptrie->leaves[base0 + 1] = sleaf;
                            poptrie->leaves[base0 + 2] = stack->nexthop;
                            VEC_SET(cnodes[i].leafvec, 0);
                            VEC_SET(cnodes[i].leafvec, BITINDEX(stack->idx));
                            VEC_SET(cnodes[i].leafvec,
                                    BITINDEX(stack->idx) + 1);
                        }
                    } else {
                        base0 = buddy_alloc2(poptrie->cleaves, 0);
                        if ( base0 < 0 ) {
                            return -1;
                        }
                        poptrie->leaves[base0] = stack->nexthop;
                        VEC_SET(cnodes[i].leafvec, 0);
                    }
                    cnodes[i].base0 = base0;
                    cnodes[i].base1 = -1;
                }
            }
        } else {
            node = &poptrie->nodes[stack->inode + NODEINDEX(stack->idx)];
            vector = node->vector;
            if ( VEC_BT(node->vector, BITINDEX(stack->idx)) ) {
                /* Internal node to leaf */
                VEC_CLEAR(vector, BITINDEX(stack->idx));
                VEC_INIT(leafvec);
                n = 0;
                prev = (u64)-1;
                for ( i = 0; i < (1 << 6); i++ ) {
                    if ( !VEC_BT(vector, i) ) {
                        if ( i == BITINDEX(stack->idx) ) {
                            if ( sleaf != prev ) {
                                leaves[n] = sleaf;
                                VEC_SET(leafvec, i);
                                n++;
                            }
                            prev = sleaf;
                        } else {
                            p = POPCNT_LS(node->leafvec, i);
                            if ( poptrie->leaves[node->base0 + p - 1]
                                 != prev ) {
                                leaves[n]
                                    = poptrie->leaves[node->base0 + p - 1];
                                VEC_SET(leafvec, i);
                                n++;
                            }
                            prev = poptrie->leaves[node->base0 + p - 1];
                        }
                    }
                }

                if ( 1 != n || 0 != POPCNT(vector) || (stack - 1)->idx < 0 ) {
                    vcomp = 0;
                    base0 = buddy_alloc2(poptrie->cleaves, bsr(n - 1) + 1);
                    if ( base0 < 0 ) {
                        return -1;
                    }
                    memcpy(poptrie->leaves + base0, leaves,
                           sizeof(poptrie_leaf_t) * n);

                    p = POPCNT(vector);
                    n = p;
                    if ( n > 0 ) {
                        base1 = buddy_alloc2(poptrie->cnodes, bsr(n - 1) + 1);
                        if ( base1 < 0 ) {
                            return -1;
                        }
                    } else {
                        base1 = -1;
                    }

                    /* Copy all */
                    n = 0;
                    for ( i = 0; i < (1 << 6); i++ ) {
                        if ( VEC_BT(vector, i) ) {
                            p = POPCNT_LS(node->vector, i);
                            p = (p - 1);
                            memcpy(&poptrie->nodes[base1 + n],
                                   &poptrie->nodes[node->base1 + p],
                                   sizeof(poptrie_node_t));
                            n += 1;
                        }
                    }

                    memcpy(cnodes, poptrie->nodes + stack->inode,
                           sizeof(poptrie_node_t) << (stack->width - 6));
                    cnodes[NODEINDEX(stack->idx)].vector = vector;
                    cnodes[NODEINDEX(stack->idx)].leafvec = leafvec;
                    cnodes[NODEINDEX(stack->idx)].base0 = base0;
                    cnodes[NODEINDEX(stack->idx)].base1 = base1;
                }
            } else {
                /* Leaf node is changed */
                VEC_INIT(leafvec);
                n = 0;
                prev = (u64)-1;
                for ( i = 0; i < (1 << 6); i++ ) {
                    if ( !VEC_BT(vector, i) ) {
                        if ( i == BITINDEX(stack->idx) ) {
                            if ( sleaf != prev ) {
                                leaves[n] = sleaf;
                                VEC_SET(leafvec, i);
                                n++;
                            }
                            prev = sleaf;
                        } else {
                            p = POPCNT_LS(node->leafvec, i);
                            if ( poptrie->leaves[node->base0 + p - 1]
                                 != prev ) {
                                leaves[n]
                                    = poptrie->leaves[node->base0 + p - 1];
                                VEC_SET(leafvec, i);
                                n++;
                            }
                            prev =  poptrie->leaves[node->base0 + p - 1];
                        }
                    }
                }

                if ( 1 != n || 0 != POPCNT(vector) || (stack - 1)->idx < 0 ) {
                    vcomp = 0;
                    if ( node->leafvec == leafvec ) {
                        /* Nothing has changed */
                        return 0;
                    }

                    base0 = buddy_alloc2(poptrie->cleaves, bsr(n - 1) + 1);
                    if ( base0 < 0 ) {
                        return -1;
                    }
                    memcpy(poptrie->leaves + base0, leaves,
                           sizeof(poptrie_leaf_t) * n);

                    memcpy(cnodes, poptrie->nodes + stack->inode,
                           sizeof(poptrie_node_t) << (stack->width - 6));
                    cnodes[NODEINDEX(stack->idx)].vector = vector;
                    cnodes[NODEINDEX(stack->idx)].leafvec = leafvec;
                    cnodes[NODEINDEX(stack->idx)].base0 = base0;
                }
            }
        }

        stack--;
    }

    while ( stack->idx >= 0 ) {
        if ( stack->inode < 0 ) {
            /* Create a new node */
            base1 = buddy_alloc2(poptrie->cnodes, 0);
            if ( base1 < 0 ) {
                return -1;
            }
            memcpy(poptrie->nodes + base1, cnodes, sizeof(poptrie_node_t));
            /* Build the next one */
            for ( i = 0; i < (1 << (stack->width - 6)); i++ ) {
                VEC_INIT(cnodes[i].vector);
                VEC_INIT(cnodes[i].leafvec);
                cnodes[i].base1 = -1;
                cnodes[i].base0 = -1;
            }
            VEC_SET(cnodes[NODEINDEX(stack->idx)].vector, BITINDEX(stack->idx));
            cnodes[NODEINDEX(stack->idx)].base1 = base1;

            for ( i = 0; i < (1 << (stack->width - 6)); i++ ) {
                base0 = buddy_alloc2(poptrie->cleaves, 0);
                if ( base0 < 0 ) {
                    return -1;
                }
                poptrie->leaves[base0] = stack->nexthop;
                if ( VEC_BT(cnodes[i].vector, 0) ) {
                    VEC_SET(cnodes[i].leafvec, 1);
                } else {
                    VEC_SET(cnodes[i].leafvec, 0);
                }
                cnodes[i].base0 = base0;
            }
        } else {
            /* Parent internal node is specified */
            node = &poptrie->nodes[stack->inode + NODEINDEX(stack->idx)];
            if ( VEC_BT(node->vector, BITINDEX(stack->idx)) ) {
                /* Same vector, then allocate and replace */
                p = POPCNT(node->vector);
                n = p;
                base1 = buddy_alloc2(poptrie->cnodes, bsr(n - 1) + 1);
                if ( base1 < 0 ) {
                    return -1;
                }
                /* Copy all */
                n = 0;
                for ( i = 0; i < (1 << 6); i++ ) {
                    if ( VEC_BT(node->vector, i) ) {
                        if ( i == BITINDEX(stack->idx) ) {
                            memcpy(&poptrie->nodes[base1 + n], cnodes,
                                   sizeof(poptrie_node_t));
                        } else {
                            memcpy(&poptrie->nodes[base1 + n],
                                   &poptrie->nodes[node->base1 + n],
                                   sizeof(poptrie_node_t));
                        }
                        n += 1;
                    }
                }
                oroot = node->base1;
                node->base1 = base1;

                _update_clean_node(poptrie, node, oroot);

                return 0;
            } else {
                /* Different vector, then allocate and go up */
                vector = node->vector;
                VEC_SET(vector, BITINDEX(stack->idx));

                p = POPCNT(vector);
                n = p;
                base1 = buddy_alloc2(poptrie->cnodes, bsr(n - 1) + 1);
                if ( base1 < 0 ) {
                    return -1;
                }

                VEC_INIT(leafvec);
                n = ZEROCNT(vector);
                if ( n > 0 ) {
                    n = 0;
                    prev = (u64)-1;
                    for ( i = 0; i < (1 << 6); i++ ) {
                        if ( !VEC_BT(vector, i) ) {
                            p = POPCNT_LS(node->leafvec, i);
                            if ( poptrie->leaves[node->base0 + p - 1]
                                 != prev ) {
                                leaves[n]
                                    = poptrie->leaves[node->base0 + p - 1];
                                VEC_SET(leafvec, i);
                                prev = poptrie->leaves[node->base0 + p - 1];
                                n++;
                            }
                        }
                    }
                    base0 = buddy_alloc2(poptrie->cleaves, bsr(n - 1) + 1);
                    if ( base0 < 0 ) {
                        return -1;
                    }
                    memcpy(poptrie->leaves + base0, leaves,
                           sizeof(poptrie_leaf_t) * n);
                } else {
                    base0 = -1;
                }

                /* Copy all */
                n = 0;
                j = 0;
                for ( i = 0; i < (1 << 6); i++ ) {
                    if ( VEC_BT(node->vector, i) ) {
                        memcpy(&poptrie->nodes[base1 + n],
                               &poptrie->nodes[node->base1 + j],
                               sizeof(poptrie_node_t));
                        n += 1;
                        j += 1;
                    } else if ( i == BITINDEX(stack->idx) ) {
                        memcpy(&poptrie->nodes[base1 + n], cnodes,
                               sizeof(poptrie_node_t));
                        n += 1;
                    }
                }

                memcpy(cnodes, poptrie->nodes + stack->inode,
                       sizeof(poptrie_node_t) << (stack->width - 6));
                cnodes[NODEINDEX(stack->idx)].base1 = base1;
                cnodes[NODEINDEX(stack->idx)].base0 = base0;
                cnodes[NODEINDEX(stack->idx)].vector = vector;
                cnodes[NODEINDEX(stack->idx)].leafvec = leafvec;
            }
        }
        stack--;
    }

    /* Replace the root */
    nroot = buddy_alloc2(poptrie->cnodes, 0);
    if ( nroot < 0 ) {
        return -1;
    }
    memcpy(poptrie->nodes + nroot, cnodes, sizeof(poptrie_node_t));
    oroot = poptrie->root;
    poptrie->root = nroot;

    /* Swap */
    __asm__ __volatile__ ("lock xchgl %%eax,%0"
                          : "=m"(*root), "=a"(oroot) : "a"(nroot));

    /* Clean */
    if ( !alt && !(oroot & ((u32)1<<31)) ) {
        _update_clean_root(poptrie, nroot, oroot);
    }

    return 0;
}

/*
 * Updated the marked subtree
 */
static int
_update_subtree(struct poptrie6 *poptrie, struct radix_node6 *node,
                __uint128_t prefix, int depth)
{
    int ret;
    struct poptrie_stack stack[128 / 6 + 1];
    struct radix_node6 *ntnode;
    int idx;
    int i;
    u32 *tmpdir;

    stack[0].inode = -1;
    stack[0].idx = -1;
    stack[0].width = -1;

    if ( depth < POPTRIE_S ) {
        /* Copy first */
        memcpy(poptrie->altdir, poptrie->dir, sizeof(u32) << POPTRIE_S);
        ret = _update_dp1(poptrie, poptrie->radix, 1, prefix, depth, 0);

        /* Replace the root */
        tmpdir = poptrie->dir;
        poptrie->dir = poptrie->altdir;
        poptrie->altdir = tmpdir;

        /* Clean */
        idx = INDEX(prefix, 0, POPTRIE_S)
            >> (POPTRIE_S - depth)
            << (POPTRIE_S - depth);
        for ( i = 0; i < (1 << (POPTRIE_S - depth)); i++ ) {
            if ( poptrie->dir[idx + i] != poptrie->altdir[idx + i] ) {
                if ( (poptrie->dir[idx + i] & ((u32)1 << 31))
                     && !(poptrie->altdir[idx + i] & ((u32)1 << 31)) ) {
                    _update_clean_subtree(poptrie, poptrie->altdir[idx + i]);
                    buddy_free2(poptrie->cnodes, poptrie->altdir[idx + i]);
                } else if ( !(poptrie->altdir[idx + i] & ((u32)1 << 31)) ) {
                    _update_clean_root(poptrie, poptrie->dir[idx + i],
                                       poptrie->altdir[idx + i]);
                }
            }
        }
    } else if ( depth == POPTRIE_S ) {
        ret = _update_dp1(poptrie, poptrie->radix, 0, prefix, depth, 0);
    } else {
        idx = INDEX(prefix, 0, POPTRIE_S);
        ntnode = _next_block(poptrie->radix, idx, 0, POPTRIE_S);
        /* Get the corresponding node */
        if ( poptrie->dir[idx] & ((u32)1 << 31) ) {
            /* Leaf */
            ret = _descend_and_update(poptrie, ntnode, -1, &stack[1], prefix,
                                      depth, POPTRIE_S, &poptrie->dir[idx]);
        } else {
            /* Node */
            ret = _descend_and_update(poptrie, ntnode, poptrie->dir[idx],
                                      &stack[1], prefix, depth, POPTRIE_S,
                                      &poptrie->dir[idx]);
        }
    }
    if ( ret < 0 ) {
        return -1;
    }

    /* Clear marks */
    _clear_mark(node);

    return 0;
}

/*
 * Update the marked parts while traversing from the root to the marked bottom
 */
static int
_descend_and_update(struct poptrie6 *poptrie, struct radix_node6 *tnode,
                    int inode, struct poptrie_stack *stack, __uint128_t prefix,
                    int len, int depth, u32 *root)
{
    int idx;
    int p;
    int n;
    struct poptrie_node *node;
    struct radix_node6 *ntnode;
    int width;

    /* Get the corresponding child */
    if ( 0 == depth ) {
        width = POPTRIE_S;
    } else {
        width = 6;
    }

    if ( len <= depth + width ) {
        /* This is the top of the marked part */
        return _update_part(poptrie, tnode, inode, stack, root, 0);
    } else {
        /* This is not the top of the marked part, then traverse to a child */
        idx = INDEX(prefix, depth, width);

        if ( inode < 0 ) {
            return _update_part(poptrie, tnode, inode, stack, root, 0);
            /* The root of the next block */
            ntnode = _next_block(tnode, idx, 0, width);
            if ( NULL == ntnode ) {
                return _update_part(poptrie, tnode, inode, stack, root, 0);
            } else {
                stack->inode = inode;
                stack->idx = idx;
                stack->width = width;
                stack->nexthop = EXT_NH(tnode);
                stack++;
                return _descend_and_update(poptrie, ntnode, -1, stack, prefix,
                                           len, depth + width, root);
            }
        }

        /* Get the corresponding node */
        node = poptrie->nodes + inode + NODEINDEX(idx);

        /* Check the vector */
        if ( VEC_BT(node->vector, BITINDEX(idx)) ) {
            /* Internal node, then traverse to the child */
            p = POPCNT_LS(node->vector, BITINDEX(idx));
            n = (p - 1);
            /* The root of the next block */
            ntnode = _next_block(tnode, idx, 0, width);
            if ( NULL == ntnode ) {
                return _update_part(poptrie, tnode, inode, stack, root, 0);
            } else {
                stack->inode = inode;
                stack->idx = idx;
                stack->width = width;
                stack++;
                return _descend_and_update(poptrie, ntnode, node->base1 + n,
                                           stack, prefix, len, depth + width,
                                           root);
            }
        } else {
            /* Leaf node, then update from this node */
            /* The root of the next block */
            ntnode = _next_block(tnode, idx, 0, width);
            if ( NULL == ntnode ) {
                return _update_part(poptrie, tnode, inode, stack, root, 0);
            } else {
                stack->inode = inode;
                stack->idx = idx;
                stack->width = width;
                stack++;
                return _descend_and_update(poptrie, ntnode, -1, stack, prefix,
                                           len, depth + width, root);
            }
        }
    }

    return 0;
}

/*
 * Update an internal node chunk
 */
static int
_update_inode_chunk(struct poptrie6 *poptrie, struct radix_node6 *node,
                    int inode, poptrie_node_t *nodes, poptrie_leaf_t *leaf)
{
    int ret;

    ret = _update_inode_chunk_rec(poptrie, node, inode, nodes, leaf, 0, 0);
    if ( ret > 0 ) {
        /* Clean */
        buddy_free2(poptrie->cleaves, nodes[0].base0);
    }

    return ret;
}
static int
_update_inode_chunk_rec(struct poptrie6 *poptrie, struct radix_node6 *node,
                        int inode, poptrie_node_t *nodes, poptrie_leaf_t *leaf,
                        int pos, int r)
{
    int ret;
    int ret0;
    int ret1;
    struct radix_node6 tmp;
    poptrie_leaf_t sleaf0;
    poptrie_leaf_t sleaf1;

    if ( 0 == r ) {
        if ( NULL != leaf ) {
            ret = _update_inode(poptrie, node, inode + pos, nodes + pos,
                                &sleaf0);
            if ( ret < 0 ) {
                return -1;
            }
            if ( ret > 0 ) {
                *leaf = sleaf0;
            }
        } else {
            ret = _update_inode(poptrie, node, inode + pos, nodes + pos, NULL);
            if ( ret < 0 ) {
                return -1;
            }
        }
        return ret;
    }

    /* Decrement */
    r--;

    /* Left */
    if ( node->left ) {
        ret0 = _update_inode_chunk_rec(poptrie, node->left, inode, nodes,
                                       leaf ? &sleaf0 : NULL, pos, r);
        if ( ret0 < 0 ) {
            return -1;
        }
    } else {
        tmp.left = NULL;
        tmp.right = NULL;
        tmp.ext = node->ext;
        ret0 = _update_inode_chunk_rec(poptrie, &tmp, inode, nodes,
                                       leaf ? &sleaf0 : NULL, pos, r);
        if ( ret0 < 0 ) {
            return -1;
        }
    }

    /* Right */
    if ( node->right ) {
        ret1 = _update_inode_chunk_rec(poptrie, node->right, inode, nodes,
                                       leaf ? &sleaf1 : NULL,
                                       pos + (1 << r), r);
        if ( ret1 < 0 ) {
            return -1;
        }
    } else {
        tmp.left = NULL;
        tmp.right = NULL;
        tmp.ext = node->ext;
        ret1 = _update_inode_chunk_rec(poptrie, &tmp, inode, nodes,
                                       leaf ? &sleaf1 : NULL,
                                       pos + (1 << r), r);
        if ( ret1 < 0 ) {
            return -1;
        }
    }
    if ( ret0 > 0 && ret1 > 0 && NULL != leaf && sleaf0 == sleaf1 ) {
        *leaf = sleaf0;
        return 1;
    }

    return 0;
}

/*
 * Update an internal node
 */
static int
_update_inode(struct poptrie6 *poptrie, struct radix_node6 *node, int inode,
              poptrie_node_t *n, poptrie_leaf_t *leaf)
{
    int i;
    u64 vector;
    u64 leafvec;
    int nvec;
    int nlvec;
    struct radix_node6 nodes[1 << 6];
    poptrie_node_t children[1 << 6];
    poptrie_leaf_t leaves[1 << 6];
    u64 prev;
    int base0;
    int base1;
    int ret;
    poptrie_leaf_t sleaf;
    int p;
    int ninode;

    /* Parse triangle */
    VEC_INIT(vector);
    _parse_triangle(node, &vector, nodes, 0, 0);

    /* Traverse children first */
    VEC_INIT(leafvec);
    prev = (u64)-1;
    nvec = 0;
    nlvec = 0;
    for ( i = 0; i < (1 << 6); i++ ) {
        if ( VEC_BT(vector, i) ) {
            /* Internal node */
            if ( (nodes[i].left && nodes[i].left->mark)
                 || (nodes[i].right && nodes[i].right->mark)
                 || inode < 0 ) {
                /* One or more child is marked */
                if ( inode >= 0 ) {
                    if ( VEC_BT(poptrie->nodes[inode].vector, i) ) {
                        p = POPCNT_LS(poptrie->nodes[inode].vector, i);
                        ninode = poptrie->nodes[inode].base1 + (p - 1);
                    } else {
                        ninode = -1;
                    }
                } else {
                    ninode = -1;
                }
                ret = _update_inode_chunk(poptrie, &nodes[i], ninode,
                                          children + i, &sleaf);
                if ( ret < 0 ) {
                    return -1;
                }
                if ( ret > 0 ) {
                    /* The vertical compression is performed then check the
                       horizontal compression */
                    VEC_CLEAR(vector, i);
                    if ( prev != sleaf ) {
                        VEC_SET(leafvec, i);
                        leaves[nlvec] = sleaf;
                        nlvec++;
                    }
                    prev = sleaf;
                } else {
                    /* Not compressed */
                    nvec++;
                }
            } else {
                /* None of children is marked, then copy */
                if ( VEC_BT(poptrie->nodes[inode].vector, i) ) {
                    /* Connect to the working internal node */
                    p = POPCNT_LS(poptrie->nodes[inode].vector, i);
                    memcpy(children + i,
                           poptrie->nodes + poptrie->nodes[inode].base1
                           + (p - 1), sizeof(poptrie_node_t));
                    nvec++;
                } else {
                    /* The working child is a leaf node */
                    VEC_CLEAR(vector, i);
                    p = POPCNT_LS(poptrie->nodes[inode].leafvec, i);
                    sleaf
                        = poptrie->leaves[poptrie->nodes[inode].base0 + p - 1];
                    if ( prev != sleaf ) {
                        VEC_SET(leafvec, i);
                        leaves[nlvec] = sleaf;
                        nlvec++;
                    }
                    prev = sleaf;
                }
            }
        } else {
            /* Leaf compression */
            if ( prev != EXT_NH(&nodes[i]) ) {
                VEC_SET(leafvec, i);
                leaves[nlvec] = EXT_NH(&nodes[i]);
                nlvec++;
            }
            prev = EXT_NH(&nodes[i]);
        }
    }

    /* Internal nodes */
    base1 = -1;
    if ( nvec > 0 ) {
        p = nvec;
        base1 = buddy_alloc2(poptrie->cnodes, bsr(p - 1) + 1);
        if ( base1 < 0 ) {
            return -1;
        }
    }
    /* Leaves */
    base0 = -1;
    if ( nlvec > 0 ) {
        p = nlvec;
        base0 = buddy_alloc2(poptrie->cleaves, bsr(p - 1) + 1);
        if ( base0 < 0 ) {
            if ( base1 >= 0 ) {
                buddy_free2(poptrie->cnodes, base1);
            }
            return -1;
        }
    }

    /* Internal nodes */
    int num = 0;
    for ( i = 0; i < (1 << 6); i++ ) {
        if ( VEC_BT(vector, i) ) {
            memcpy(&poptrie->nodes[base1 + num], &children[i],
                   sizeof(poptrie_node_t));
            num++;
        }
    }
    /* Leaves */
    for ( i = 0; i < nlvec; i++ ) {
        poptrie->leaves[base0 + i] = leaves[i];
    }
    n->vector = vector;
    n->leafvec = leafvec;
    n->base0 = base0;
    n->base1 = base1;

    if ( 0 == nvec && 1 == nlvec && NULL != leaf ) {
        /* Only one leaf belonging to this internal node, then compress
           this (but can't do this for the top tier when leaf is NULL) */
        *leaf = leaves[0];
        return 1;
    }

    return 0;
}

/*
 * Update a partial tree (direct pointing)
 */
static int
_update_dp1(struct poptrie6 *poptrie, struct radix_node6 *tnode, int alt,
            __uint128_t prefix, int len, int depth)
{
    int i;
    int idx;

    if ( depth == len ) {
        return _update_dp2(poptrie, tnode, alt, prefix, len, depth);
    }

    if ( (prefix >> (128 - depth - 1)) & 1 ) {
        /* Right */
        if ( tnode->right ) {
            return _update_dp1(poptrie, tnode->right, alt, prefix, len,
                               depth + 1);
        } else {
            idx = INDEX(prefix, 0, POPTRIE_S)
                >> (POPTRIE_S - len)
                << (POPTRIE_S - len);
            for ( i = 0; i < (1 << (POPTRIE_S - len)); i++ ) {
                if ( alt ) {
                    poptrie->altdir[idx + i] = ((u32)1 << 31) | EXT_NH(tnode);
                } else {
                    poptrie->dir[idx + i] = ((u32)1 << 31) | EXT_NH(tnode);
                    _update_clean_subtree(poptrie, poptrie->dir[idx + i]);
                    if ( (int)poptrie->dir[idx + i] >= 0 ) {
                        buddy_free2(poptrie->cnodes, poptrie->dir[idx + i]);
                    }
                }
            }
            return 0;
        }
    } else {
        /* Left */
        if ( tnode->left ) {
            return _update_dp1(poptrie, tnode->left, alt, prefix, len,
                               depth + 1);
        } else {
            idx = INDEX(prefix, 0, POPTRIE_S)
                >> (POPTRIE_S - len)
                << (POPTRIE_S - len);
            for ( i = 0; i < (1 << (POPTRIE_S - len)); i++ ) {
                if ( alt ) {
                    poptrie->altdir[idx + i] = ((u32)1 << 31) | EXT_NH(tnode);
                } else {
                    poptrie->dir[idx + i] = ((u32)1 << 31) | EXT_NH(tnode);
                    _update_clean_subtree(poptrie, poptrie->dir[idx + i]);
                    if ( (int)poptrie->dir[idx + i] >= 0 ) {
                        buddy_free2(poptrie->cnodes, poptrie->dir[idx + i]);
                    }
                }
            }
            return 0;
        }
    }
}
static int
_update_dp2(struct poptrie6 *poptrie, struct radix_node6 *tnode, int alt,
            __uint128_t prefix, int len, int depth)
{
    int i;
    int idx;
    int ret;
    struct poptrie_stack stack[128 / 6 + 1];

    if ( depth == POPTRIE_S ) {
        idx = INDEX(prefix, 0, POPTRIE_S);
        stack[0].inode = -1;
        stack[0].idx = -1;
        stack[0].width = -1;

        if ( poptrie->dir[idx] & ((u32)1 << 31) ) {
            if ( alt ) {
                ret = _update_part(poptrie, tnode, -1, &stack[1],
                                   &poptrie->altdir[idx], alt);
            } else {
                ret = _update_part(poptrie, tnode, -1, &stack[1],
                                   &poptrie->dir[idx], alt);
            }
        } else {
            if ( alt ) {
                ret = _update_part(poptrie, tnode, poptrie->dir[idx], &stack[1],
                                   &poptrie->altdir[idx], alt);
            } else {
                ret = _update_part(poptrie, tnode, poptrie->dir[idx], &stack[1],
                                   &poptrie->dir[idx], alt);
            }
        }
        return ret;
    }

    if ( tnode->left ) {
        _update_dp2(poptrie, tnode->left, alt, prefix, len, depth + 1);
    } else {
        idx = INDEX(prefix, 0, POPTRIE_S)
            >> (POPTRIE_S - depth) << (POPTRIE_S - depth);
        for ( i = 0; i < (1 << (POPTRIE_S - depth - 1)); i++ ) {
            if ( alt ) {
                poptrie->altdir[idx + i] = ((u32)1 << 31) | EXT_NH(tnode);
            } else {
                poptrie->dir[idx + i] = ((u32)1 << 31) | EXT_NH(tnode);
                _update_clean_subtree(poptrie, poptrie->dir[idx + i]);
                if ( (int)poptrie->dir[idx + i] >= 0 ) {
                    buddy_free2(poptrie->cnodes, poptrie->dir[idx + i]);
                }
            }
        }
    }
    if ( tnode->right ) {
        prefix |= 1 << (128 - depth - 1);
        return _update_dp2(poptrie, tnode->right, alt, prefix, len, depth + 1);
    } else {
        idx = INDEX(prefix, 0, POPTRIE_S)
            >> (POPTRIE_S - depth)
            << (POPTRIE_S - depth);
        idx += 1 << (POPTRIE_S - depth - 1);
        for ( i = 0; i < (1 << (POPTRIE_S - depth - 1)); i++ ) {
            if ( alt ) {
                poptrie->altdir[idx + i] = ((u32)1 << 31) | EXT_NH(tnode);
            } else {
                poptrie->dir[idx + i] = ((u32)1 << 31) | EXT_NH(tnode);
                _update_clean_subtree(poptrie, poptrie->dir[idx + i]);
                if ( (int)poptrie->dir[idx + i] >= 0 ) {
                    buddy_free2(poptrie->cnodes, poptrie->dir[idx + i]);
                }
            }
        }
    }

    return 0;
}

/*
 * Update and clean from the root
 */
static void
_update_clean_root(struct poptrie6 *poptrie, int nroot, int oroot)
{
    int i;
    int nn;
    int on;
    int nbase;

    nn = 0;
    on = 0;
    for ( i = 0; i < (1 << 6); i++ ) {
        if ( VEC_BT(poptrie->nodes[nroot].vector, i) ) {
            nbase = poptrie->nodes[nroot].base1 + nn;
            nn++;
        } else {
            nbase = -1;
        }
        if ( VEC_BT(poptrie->nodes[oroot].vector, i) ) {
            _update_clean_inode(poptrie, nbase,
                                poptrie->nodes[oroot].base1 + on);
            on++;
        }
    }

    if ( poptrie->nodes[nroot].base1 != poptrie->nodes[oroot].base1
         && (u32)-1 != poptrie->nodes[oroot].base1 ) {
        buddy_free2(poptrie->cnodes, poptrie->nodes[oroot].base1);
    }
    if ( poptrie->nodes[nroot].base0 != poptrie->nodes[oroot].base0
         && (u32)-1 != poptrie->nodes[oroot].base0 ) {
        buddy_free2(poptrie->cleaves, poptrie->nodes[oroot].base0);
    }
    /* Clear */
    if ( oroot != nroot ) {
        buddy_free2(poptrie->cnodes, oroot);
    }
}

/*
 * Update and clean from the specified node
 */
static void
_update_clean_node(struct poptrie6 *poptrie, poptrie_node_t *node, int oinode)
{
    int i;
    int n;

    if ( oinode < 0 ) {
        return;
    }

    n = 0;
    for ( i = 0; i < (1 << 6); i++ ) {
        if ( VEC_BT(node->vector, i) ) {
            _update_clean_inode(poptrie, node->base1 + n, oinode + n);
            n++;
        }
    }

    /* Clear */
    if ( (int)node->base1 != oinode ) {
         buddy_free2(poptrie->cnodes, oinode);
    }
}
static void
_update_clean_inode(struct poptrie6 *poptrie, int ninode, int oinode)
{
    int i;
    int obase;
    int nbase;

    if ( ninode == oinode ) {
        /* Identical node, then immediately quit from the procedure */
        return;
    }

    if ( ninode >= 0 ) {
        obase = poptrie->nodes[oinode].base1;
        nbase = poptrie->nodes[ninode].base1;
        for ( i = 0; i < (1 << 6); i++ ) {
            if ( VEC_BT(poptrie->nodes[oinode].vector, i) ) {
                if ( VEC_BT(poptrie->nodes[ninode].vector, i) ) {
                    _update_clean_inode(poptrie, nbase, obase);
                } else {
                    _update_clean_inode(poptrie, -1, obase);
                }
            }
            if ( VEC_BT(poptrie->nodes[oinode].vector, i) ) {
                obase += 1;
            }
            if ( VEC_BT(poptrie->nodes[ninode].vector, i) ) {
                nbase += 1;
            }
        }

        if ( (u32)-1 != poptrie->nodes[oinode].base1
             && poptrie->nodes[oinode].base1 != poptrie->nodes[ninode].base1 ) {
            buddy_free2(poptrie->cnodes, poptrie->nodes[oinode].base1);
        }
        if ( (u32)-1 != poptrie->nodes[oinode].base0
             && poptrie->nodes[oinode].base0 != poptrie->nodes[ninode].base0 ) {
            buddy_free2(poptrie->cleaves, poptrie->nodes[oinode].base0);
        }
    } else {
        obase = poptrie->nodes[oinode].base1;
        for ( i = 0; i < (1 << 6); i++ ) {
            if ( VEC_BT(poptrie->nodes[oinode].vector, i) ) {
                _update_clean_inode(poptrie, -1, obase);
                obase += 1;
            }
        }

        if ( (u32)-1 != poptrie->nodes[oinode].base1 ) {
            buddy_free2(poptrie->cnodes, poptrie->nodes[oinode].base1);
        }
        if ( (u32)-1 != poptrie->nodes[oinode].base0 ) {
            buddy_free2(poptrie->cleaves, poptrie->nodes[oinode].base0);
        }
    }
}

/*
 * Update and clean a subtree
 */
static void
_update_clean_subtree(struct poptrie6 *poptrie, int oinode)
{
    int i;
    int n;
    struct poptrie_node *node;

    if ( oinode < 0 ) {
        return;
    }

    node = &poptrie->nodes[oinode];

    n = 0;
    for ( i = 0; i < (1 << 6); i++ ) {
        if ( VEC_BT(node->vector, i) ) {
            _update_clean_subtree(poptrie, node->base1 + n);
            n++;
        }
    }

    /* Clear */
    if ( (int)node->base1 >= 0 ) {
        buddy_free2(poptrie->cnodes, node->base1);
    }

    if ( (int)node->base0 >= 0 ) {
        buddy_free2(poptrie->cleaves, node->base0);
    }
}

/*
 * Get the descending block from the index and shift
 */
static struct radix_node6 *
_next_block(struct radix_node6 *node, int idx, int shift, int depth)
{
    if ( NULL == node ) {
        return NULL;
    }

    if ( shift == depth ) {
        return node;
    }

    if ( (idx >> (depth - shift - 1)) & 0x1 ) {
        /* Right */
        return _next_block(node->right, idx, shift + 1, depth);
    } else {
        /* Left */
        return _next_block(node->left, idx, shift + 1, depth);
    }
}

/*
 * Parse triangle (k-bit subtree)
 */
static void
_parse_triangle(struct radix_node6 *node, u64 *vector,
                struct radix_node6 *nodes, int pos, int depth)
{
    int i;
    int hlen;

    if ( 6 == depth ) {
        /* Bottom of the triangle */
        memcpy(&nodes[pos], node, sizeof(struct radix_node6));
        if ( node->left || node->right ) {
            /* Child internal nodes exist */
            VEC_SET(*vector, pos);
        }
        return;
    }

    /* Calculate half length */
    hlen = (1 << (6 - depth - 1));

    /* Left */
    if ( node->left ) {
        _parse_triangle(node->left, vector, nodes, pos, depth + 1);
    } else {
        for ( i = pos; i < pos + hlen; i++ ) {
            memcpy(&nodes[i], node, sizeof(struct radix_node6));
            nodes[i].left = NULL;
            nodes[i].right = NULL;
        }
    }
    /* Right */
    if ( node->right ) {
        _parse_triangle(node->right, vector, nodes, pos + hlen, depth + 1);
    } else {
        for ( i = pos + hlen; i < pos + hlen * 2; i++ ) {
            memcpy(&nodes[i], node, sizeof(struct radix_node6));
            nodes[i].left = NULL;
            nodes[i].right = NULL;
        }
    }
}

/*
 * Clear all the marks
 */
static void
_clear_mark(struct radix_node6 *node)
{
    if ( !node->mark ) {
        return;
    }
    node->mark = 0;
    if ( node->left ) {
        _clear_mark(node->left);
    }
    if ( node->right ) {
        _clear_mark(node->right);
    }
}

/*
 * Recursive function to add a route to the poptrie data structure while
 * inserting the route to the RIB (radix tree)
 */
static int
_route_add(struct poptrie6 *poptrie, struct radix_node6 **node,
           __uint128_t prefix, int len, poptrie_leaf_t nexthop, int depth,
           struct radix_node6 *ext)
{
    if ( NULL == *node ) {
        *node = malloc(sizeof(struct radix_node6));
        if ( NULL == *node ) {
            /* Memory error */
            return -1;
        }
        (*node)->valid = 0;
        (*node)->left = NULL;
        (*node)->right = NULL;
        (*node)->ext = ext;
        (*node)->mark = 0;
    }

    if ( len == depth ) {
        /* Matched */
        if ( (*node)->valid ) {
            /* Already exists */
            return -1;
        }
        (*node)->valid = 1;
        (*node)->nexthop = nexthop;
        (*node)->len = len;

        /* Propagate this route to children */
        (*node)->mark = _route_add_propagate(*node, *node);

        /* Update the poptrie subtree */
        return _update_subtree(poptrie, *node, prefix, depth);
    } else {
        if ( (*node)->valid ) {
            ext = *node;
        }
        if ( (prefix >> (128 - depth - 1)) & 1 ) {
            /* Search to the right */
            return _route_add(poptrie, &((*node)->right), prefix, len, nexthop,
                              depth + 1, ext);
        } else {
            /* Search to the left */
            return _route_add(poptrie, &((*node)->left), prefix, len, nexthop,
                              depth + 1, ext);
        }
    }
}
static int
_route_add_propagate(struct radix_node6 *node, struct radix_node6 *ext)
{
    if ( NULL != node->ext ) {
        if ( ext->len > node->ext->len ) {
            /* This new node is more specific */
            if ( ext->nexthop != EXT_NH(node) ) {
                /* Prefix and next hop are updated */
                node->mark = 1;
            }
            node->mark = 1;
            node->ext = ext;
        } else {
            /* This new node is less specific, then terminate */
            node->mark = 1;
            return node->mark;
        }
    } else {
        /* The new route is propagated */
        node->mark = 1;
        node->ext = ext;
    }
    if ( NULL != node->left ) {
        node->mark |= _route_add_propagate(node->left, ext);
    }
    if ( NULL != node->right ) {
        node->mark |= _route_add_propagate(node->right, ext);
    }

    return node->mark;
}

/*
 * Change a route
 */
static int
_route_change(struct poptrie6 *poptrie, struct radix_node6 **node,
              __uint128_t prefix, int len, poptrie_leaf_t nexthop, int depth)
{
    if ( NULL == *node ) {
        /* Must have the entry for route_change() */
        return -1;
    }

    if ( len == depth ) {
        /* Matched */
        if ( !(*node)->valid ) {
            /* Not exists */
            return -1;
        }
        /* Update the entry */
        if ( (*node)->nexthop != nexthop ) {
            (*node)->nexthop = nexthop;
            (*node)->mark = _route_change_propagate(*node, *node);

            /* Marked root */
            return _update_subtree(poptrie, *node, prefix, depth);
        }

        return 0;
    } else {
        if ( (prefix >> (128 - depth - 1)) & 1 ) {
            /* Right */
            return _route_change(poptrie, &((*node)->right), prefix, len,
                                 nexthop, depth + 1);
        } else {
            /* Left */
            return _route_change(poptrie, &((*node)->left), prefix, len,
                                 nexthop, depth + 1);
        }
    }
}
static int
_route_change_propagate(struct radix_node6 *node, struct radix_node6 *ext)
{
    /* Mark if the cache is updated */
    if ( ext == node->ext ) {
        node->mark = 1;
    }

    if ( NULL != node->left ) {
        node->mark |= _route_change_propagate(node->left, ext);
    }
    if ( NULL != node->right ) {
        node->mark |= _route_change_propagate(node->right, ext);
    }

    return node->mark;
}

/*
 * Update a route
 */
static int
_route_update(struct poptrie6 *poptrie, struct radix_node6 **node,
              __uint128_t prefix, int len, poptrie_leaf_t nexthop, int depth,
              struct radix_node6 *ext)
{
    if ( NULL == *node ) {
        *node = malloc(sizeof(struct radix_node6));
        if ( NULL == *node ) {
            /* Memory error */
            return -1;
        }
        (*node)->valid = 0;
        (*node)->left = NULL;
        (*node)->right = NULL;
        (*node)->ext = ext;
        (*node)->mark = 0;
    }

    if ( len == depth ) {
        /* Matched */
        if ( (*node)->valid ) {
            /* Already exists */
            if ( (*node)->nexthop != nexthop ) {
                (*node)->nexthop = nexthop;
                (*node)->mark = _route_change_propagate(*node, *node);

                /* Marked root */
                return _update_subtree(poptrie, *node, prefix, depth);
            }
            return 0;
        } else {
            (*node)->valid = 1;
            (*node)->nexthop = nexthop;
            (*node)->len = len;

            /* Propagate this route to children */
            (*node)->mark = _route_add_propagate(*node, *node);

            /* Update MBT */
            return _update_subtree(poptrie, *node, prefix, depth);
        }
    } else {
        if ( (*node)->valid ) {
            ext = *node;
        }
        if ( (prefix >> (128 - depth - 1)) & 1 ) {
            /* Right */
            return _route_update(poptrie, &((*node)->right), prefix, len,
                                 nexthop, depth + 1, ext);
        } else {
            /* Left */
            return _route_update(poptrie, &((*node)->left), prefix, len,
                                 nexthop, depth + 1, ext);
        }
    }
}

/*
 * Delete a route
 */
static int
_route_del(struct poptrie6 *poptrie, struct radix_node6 **node,
           __uint128_t prefix, int len, int depth, struct radix_node6 *ext)
{
    int ret;

    if ( NULL == *node ) {
        return -1;
    }

    if ( len == depth ) {
        if ( !(*node)->valid ) {
            /* No entry found */
            return -1;
        }

        /* Propagate first */
        (*node)->mark = _route_del_propagate(*node, *node, ext);

        /* Invalidate the node */
        (*node)->valid = 0;
        (*node)->nexthop = 0;

        /* Marked root */
        ret = _update_subtree(poptrie, *node, prefix, depth);
        if ( ret < 0 ) {
            return -1;
        }

        /* May need to delete this node if both children are empty, but we
           not care in this implementation because we have a large amount
           memory and the unused memory do not affect the performance. */

        return 0;
    } else {
        /* Update the propagate node if valid */
        if ( (*node)->valid ) {
            ext = *node;
        }
        /* Traverse a child node */
        if ( (prefix >> (128 - depth - 1)) & 1 ) {
            /* Right */
            ret = _route_del(poptrie, &((*node)->right), prefix, len, depth + 1,
                             ext);
        } else {
            /* Left */
            ret = _route_del(poptrie, &((*node)->left), prefix, len, depth + 1,
                             ext);
        }
        if ( ret < 0 ) {
            return ret;
        }
        /* Delete this node if both children are empty */
        if ( NULL == (*node)->left && NULL == (*node)->right ) {
            free(*node);
            *node = NULL;
        }
        return ret;
    }

    return -1;
}
static int
_route_del_propagate(struct radix_node6 *node, struct radix_node6 *oext,
                     struct radix_node6 *next)
{
    if ( oext == node->ext ) {
        if ( oext->nexthop != EXT_NH(node) ) {
            /* Next hop will change */
            node->mark = 1;
        }
        /* Replace the extracted node */
        node->ext = next;
        node->mark = 1;
    }
    if ( NULL != node->left ) {
        node->mark |= _route_del_propagate(node->left, oext, next);
    }
    if ( NULL != node->right ) {
        node->mark |= _route_del_propagate(node->right, oext, next);
    }

    return node->mark;
}

/*
 * Lookup from the RIB table
 */
static u32
_rib_lookup(struct radix_node6 *node, __uint128_t addr, int depth,
            struct radix_node6 *en)
{
    if ( NULL == node ) {
        return 0;
    }
    if ( node->valid ) {
        en = node;
    }

    if ( (addr >> (128 - depth - 1)) & 1 ) {
        /* Right */
        if ( NULL == node->right ) {
            if ( NULL != en ) {
                return en->nexthop;
            } else {
                return 0;
            }
        } else {
            return _rib_lookup(node->right, addr, depth + 1, en);
        }
    } else {
        /* Left */
        if ( NULL == node->left ) {
            if ( NULL != en ) {
                return en->nexthop;
            } else {
                return 0;
            }
        } else {
            return _rib_lookup(node->left, addr, depth + 1, en);
        }
    }
}

/*
 * Free the allocated memory by the radix tree
 */
static void
_release_radix(struct radix_node6 *node)
{
    if ( NULL != node  ) {
        _release_radix(node->left);
        _release_radix(node->right);
        free(node);
    }
}

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
