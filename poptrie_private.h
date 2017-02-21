/*_
 * Copyright (c) 2017 Hirochika Asai <asai@jar.jp>
 * All rights reserved.
 */

#ifndef _POPTRIE_PRIVATE_H
#define _POPTRIE_PRIVATE_H

#include "buddy.h"
#include "poptrie.h"
#include <stdlib.h>
#include <string.h>

/* Bit test */
#define BT(a, b)        (((a) >> (b)) & 1)

#define EXT_NH(n)       ((n)->ext ? (n)->ext->nexthop : 0)
#define VEC_INIT(v)     ((v) = 0)
#define BITINDEX(v)     ((v) & ((1 << 6) - 1))
#define NODEINDEX(v)    ((v) >> 6)
#define VEC_CLEAR(v, i) ((v) &= ~((u64)1 << (i)))
#define VEC_BT(v, i)    ((v) & (u64)1 << (i))
#define VEC_SET(v, i)   ((v) |= (u64)1 << (i))
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
poptrie_route_add_propagate(struct radix_node *, struct radix_node *);
static int
poptrie_route_change_propagate(struct radix_node *, struct radix_node *);
static int
poptrie_route_del_propagate(struct radix_node *, struct radix_node *,
                            struct radix_node *);
static int
_update_inode(struct poptrie *, struct radix_node *, int, poptrie_node_t *,
              poptrie_leaf_t *);
static int
_update_inode_chunk(struct poptrie *, struct radix_node *, int,
                    poptrie_node_t *, poptrie_leaf_t *);
static int
_update_inode_chunk_rec(struct poptrie *, struct radix_node *, int,
                        poptrie_node_t *, poptrie_leaf_t *, int, int);
static int
_update_part(struct poptrie *, struct radix_node *, int, struct poptrie_stack *,
             u32 *, int);
static int
_update_part_loop1(struct poptrie *, struct radix_node *, int,
                   struct poptrie_stack *, struct poptrie_node *,
                   poptrie_leaf_t, int *);
static int
_update_part_loop2(struct poptrie *, struct poptrie_stack *,
                   struct poptrie_node *);
static int
_update_part_dp(struct poptrie *, struct radix_node *, int, u32 *, int);
static struct radix_node * _next_block(struct radix_node *, int, int, int);
static void
_parse_triangle(struct radix_node *, u64 *, struct radix_node *, int, int);
static void _update_clean_node(struct poptrie *, poptrie_node_t *, int);
static void _update_clean_inode(struct poptrie *, int, int);
static void _update_clean_root(struct poptrie *, int, int);
static void _update_clean_subtree(struct poptrie *, int);

/*
 * Bit scan of the most significant set bit to calculate 2^n-byte aligned size
 */
static inline int
bsr(u64 x)
{
    if ( !x ) {
        return 0;
    }

    return ((sizeof(u64) << 3) - 1) - __builtin_clzll(x);
}

/*
 * Mark the descendant node to be updated after the route_add operation
 */
static int
poptrie_route_add_propagate(struct radix_node *node, struct radix_node *ext)
{
    if ( NULL != node->ext ) {
        if ( ext->len > node->ext->len ) {
            /* This new node is more specific. */
            if ( ext->nexthop != EXT_NH(node) ) {
                /* Prefix and next hop are updated. */
                node->mark = 1;
            }
            node->mark = 1;
            node->ext = ext;
        } else {
            /* This new node is less specific, then terminate. */
            node->mark = 1;
            return node->mark;
        }
    } else {
        /* The new route is propagated. */
        node->mark = 1;
        node->ext = ext;
    }
    if ( NULL != node->left ) {
        node->mark |= poptrie_route_add_propagate(node->left, ext);
    }
    if ( NULL != node->right ) {
        node->mark |= poptrie_route_add_propagate(node->right, ext);
    }

    return node->mark;
}

/*
 * Mark the descendant node to be updated after the route_change operation
 */
static int
poptrie_route_change_propagate(struct radix_node *node, struct radix_node *ext)
{
    /* Mark if the cache is updated */
    if ( ext == node->ext ) {
        node->mark = 1;
    }

    if ( NULL != node->left ) {
        node->mark |= poptrie_route_change_propagate(node->left, ext);
    }
    if ( NULL != node->right ) {
        node->mark |= poptrie_route_change_propagate(node->right, ext);
    }

    return node->mark;
}

/*
 * Mark the descendant node to be updated after the route_del operation
 */
static int
poptrie_route_del_propagate(struct radix_node *node, struct radix_node *oext,
                            struct radix_node *next)
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
        node->mark |= poptrie_route_del_propagate(node->left, oext, next);
    }
    if ( NULL != node->right ) {
        node->mark |= poptrie_route_del_propagate(node->right, oext, next);
    }

    return node->mark;
}

/*
 * Update an internal node
 */
static int
_update_inode(struct poptrie *poptrie, struct radix_node *node, int inode,
              poptrie_node_t *n, poptrie_leaf_t *leaf)
{
    int i;
    u64 vector;
    u64 leafvec;
    int nvec;
    int nlvec;
    struct radix_node nodes[1 << 6];
    poptrie_node_t children[1 << 6];
    poptrie_leaf_t leaves[1 << 6];
    u64 prev;
    int base0;
    int base1;
    int ret;
    poptrie_leaf_t sleaf;
    int p;
    int ninode;
    int num;

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
    num = 0;
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
 * Update an internal node chunk
 */
static int
_update_inode_chunk(struct poptrie *poptrie, struct radix_node *node,
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
_update_inode_chunk_rec(struct poptrie *poptrie, struct radix_node *node,
                        int inode, poptrie_node_t *nodes, poptrie_leaf_t *leaf,
                        int pos, int r)
{
    int ret;
    int ret0;
    int ret1;
    struct radix_node tmp;
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
 * Update the partial tree
 */
static int
_update_part(struct poptrie *poptrie, struct radix_node *tnode, int inode,
             struct poptrie_stack *stack, u32 *root, int alt)
{
    struct poptrie_node *cnodes;
    int ret;
    poptrie_leaf_t sleaf;
    int vcomp;
    int nroot;
    int oroot;

    /* Pop from the stack */
    stack--;

    /* Build the updated part */
    if ( stack->idx < 0 ) {
        /* Direct pointing entry */
        return _update_part_dp(poptrie, tnode, inode, root, alt);
    }

    /* Allocate descendant nodes */
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
        ret = _update_part_loop1(poptrie, tnode, inode, stack, cnodes, sleaf,
                                 &vcomp);
        if ( ret < 0 ) {
            return -1;
        } else if ( ret > 0 ) {
            return 0;
        }
        stack--;
    }

    while ( stack->idx >= 0 ) {
        ret = _update_part_loop2(poptrie, stack, cnodes);
        if ( ret < 0 ) {
            return -1;
        } else if ( ret > 0 ) {
            return 0;
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
    oroot = *root;
    __sync_lock_test_and_set(root, nroot);

    /* Clean */
    if ( !alt && !(oroot & ((u32)1<<31)) ) {
        _update_clean_root(poptrie, nroot, oroot);
    }

    return 0;
}
static int
_update_part_loop1(struct poptrie *poptrie, struct radix_node *tnode, int inode,
                   struct poptrie_stack *stack, struct poptrie_node *cnodes,
                   poptrie_leaf_t sleaf, int *vcomp)
{
    int i;
    int base0;
    int base1;
    int p;
    int n;
    poptrie_leaf_t leaves[1 << 6];
    u64 prev;
    struct poptrie_node *node;
    u64 vector;
    u64 leafvec;

    /* Perform vertical compresion */
    if ( stack->inode < 0 ) {
        if ( stack->nexthop != sleaf ) {
            /* Compression ends here */
            *vcomp = 0;
            for ( i = 0; i < (1 << (stack->width - 6)); i++ ) {
                VEC_INIT(cnodes[i].vector);
                VEC_INIT(cnodes[i].leafvec);
                if ( i == NODEINDEX(stack->idx) ) {
                    if ( 0 == BITINDEX(stack->idx) ) {
                        /* Insert to the left */
                        base0 = buddy_alloc2(poptrie->cleaves, 1);
                        if ( base0 < 0 ) {
                            return -1;
                        }
                        poptrie->leaves[base0] = sleaf;
                        poptrie->leaves[base0 + 1] = stack->nexthop;
                        VEC_SET(cnodes[i].leafvec, 0);
                        VEC_SET(cnodes[i].leafvec, 1);
                    } else if ( ((1 << 6) - 1) == BITINDEX(stack->idx) ) {
                        /* Insert to the right */
                        base0 = buddy_alloc2(poptrie->cleaves, 1);
                        if ( base0 < 0 ) {
                            return -1;
                        }
                        poptrie->leaves[base0] = stack->nexthop;
                        poptrie->leaves[base0 + 1] = sleaf;
                        VEC_SET(cnodes[i].leafvec, 0);
                        VEC_SET(cnodes[i].leafvec, BITINDEX(stack->idx));
                    } else {
                        /* Insert to the middle */
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
                        if ( poptrie->leaves[node->base0 + p - 1] != prev ) {
                            leaves[n] = poptrie->leaves[node->base0 + p - 1];
                            VEC_SET(leafvec, i);
                            n++;
                        }
                        prev = poptrie->leaves[node->base0 + p - 1];
                    }
                }
            }

            if ( 1 != n || 0 != POPCNT(vector) || (stack - 1)->idx < 0 ) {
                *vcomp = 0;
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
                        if ( poptrie->leaves[node->base0 + p - 1] != prev ) {
                            leaves[n] = poptrie->leaves[node->base0 + p - 1];
                            VEC_SET(leafvec, i);
                            n++;
                        }
                        prev =  poptrie->leaves[node->base0 + p - 1];
                    }
                }
            }

            if ( 1 != n || 0 != POPCNT(vector) || (stack - 1)->idx < 0 ) {
                *vcomp = 0;
                if ( node->leafvec == leafvec ) {
                    /* Nothing has changed */
                    return 1;
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

    return 0;
}
static int
_update_part_loop2(struct poptrie *poptrie, struct poptrie_stack *stack,
                   struct poptrie_node *cnodes)
{
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

            return 1;
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
                        if ( poptrie->leaves[node->base0 + p - 1] != prev ) {
                            leaves[n] = poptrie->leaves[node->base0 + p - 1];
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

    return 0;
}
static int
_update_part_dp(struct poptrie *poptrie, struct radix_node *tnode, int inode,
                u32 *root, int alt)
{
    struct poptrie_node *cnodes;
    int ret;
    poptrie_leaf_t sleaf;
    int nroot;
    int oroot;

    cnodes = alloca(sizeof(struct poptrie_node));
    if ( NULL == cnodes ) {
        return -1;
    }
    ret = _update_inode_chunk_rec(poptrie, tnode, inode, cnodes, &sleaf, 0, 0);
    if ( ret < 0 ) {
        return -1;
    }
    if ( ret > 0 ) {
        /* Clean */
        buddy_free2(poptrie->cleaves, cnodes[0].base0);
        cnodes[0].base0 = -1;

        /* Replace the root with an atomic instruction */
        nroot = ((u32)1 << 31) | sleaf;
        oroot = *root;
        __sync_lock_test_and_set(root, nroot);
        if ( !alt ) {
            _update_clean_subtree(poptrie, oroot);
            if ( (int)oroot >= 0 ) {
                buddy_free2(poptrie->cnodes, oroot);
            }
        }

        return 0;
    } else {
        /* Replace the root */
        nroot = buddy_alloc2(poptrie->cnodes, 0);
        if ( nroot < 0 ) {
            return -1;
        }
        memcpy(poptrie->nodes + nroot, cnodes, sizeof(struct poptrie_node));
        oroot = poptrie->root;
        poptrie->root = nroot;

        /* Replace the root with an atomic instruction */
        oroot = *root;
        __sync_lock_test_and_set(root, nroot);

        /* Clean */
        if ( !alt && !(oroot & ((u32)1 << 31)) ) {
            _update_clean_root(poptrie, nroot, oroot);
        }

        return 0;
    }
}

/*
 * Get the descending block from the index and shift
 */
static struct radix_node *
_next_block(struct radix_node *node, int idx, int shift, int depth)
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
_parse_triangle(struct radix_node *node, u64 *vector, struct radix_node *nodes,
                int pos, int depth)
{
    int i;
    int hlen;

    if ( 6 == depth ) {
        /* Bottom of the triangle */
        memcpy(&nodes[pos], node, sizeof(struct radix_node));
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
            memcpy(&nodes[i], node, sizeof(struct radix_node));
            nodes[i].left = NULL;
            nodes[i].right = NULL;
        }
    }
    /* Right */
    if ( node->right ) {
        _parse_triangle(node->right, vector, nodes, pos + hlen, depth + 1);
    } else {
        for ( i = pos + hlen; i < pos + hlen * 2; i++ ) {
            memcpy(&nodes[i], node, sizeof(struct radix_node));
            nodes[i].left = NULL;
            nodes[i].right = NULL;
        }
    }
}

/*
 * Clear all the marks
 */
static void
_clear_mark(struct radix_node *node)
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
 * Update and clean from the specified node
 */
static void
_update_clean_node(struct poptrie *poptrie, poptrie_node_t *node, int oinode)
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
_update_clean_inode(struct poptrie *poptrie, int ninode, int oinode)
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
_update_clean_subtree(struct poptrie *poptrie, int oinode)
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
 * Update and clean from the root
 */
static void
_update_clean_root(struct poptrie *poptrie, int nroot, int oroot)
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
 * Insert an entry to the FIB mapping table
 */
static int
poptrie_fib_ref(struct poptrie *poptrie, void *nexthop)
{
    int i;
    int n;

    /* Find the FIB entry mapping first */
    for ( i = 0; i < poptrie->fib.sz; i++ ) {
        if ( poptrie->fib.entries[i].entry == nexthop ) {
            /* Found the matched entry */
            poptrie->fib.entries[i].refs++;
            n = i;
            break;
        }
    }
    if ( i == poptrie->fib.sz ) {
        /* No matching FIB entry was found, then find an available slot */
        for ( i = 0; i < poptrie->fib.sz; i++ ) {
            if ( poptrie->fib.entries[i].refs <= 0 ) {
                /* Found */
                n = i;
                break;
            }
        }
        if ( i == poptrie->fib.sz ) {
            /* The FIB mapping table is full */
            return -1;
        }

        /* Append new FIB entry */
        poptrie->fib.entries[n].entry = nexthop;
        poptrie->fib.entries[n].refs = 1;
    }

    return n;
}

/*
 * Dereference an entry from the FIB mapping table
 */
static void
poptrie_fib_deref(struct poptrie *poptrie, void *nexthop)
{
    int i;

    for ( i = 0; i < poptrie->fib.sz; i++ ) {
        if ( poptrie->fib.entries[i].entry == nexthop ) {
            /* Found the matched entry */
            poptrie->fib.entries[i].refs--;
            break;
        }
    }
}

#endif /* _POPTRIE_PRIVATE_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
