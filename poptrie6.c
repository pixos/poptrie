/*_
 * Copyright (c) 2016-2017 Hirochika Asai <asai@jar.jp>
 * All rights reserved.
 */

#include "buddy.h"
#include "poptrie.h"
#include "poptrie_private.h"
#include <stdlib.h>
#include <string.h>

static inline __uint128_t
INDEX(__uint128_t a, int s, int n)
{
    if ( 0 == ((s) + (n)) ) {
        return 0;
    } else {
        return ((a) >> (128 - ((s) + (n)))) & ((1ULL << (n)) - 1);
    }
}


#define KEYLENGTH       128


/* Prototype declarations */
static int
_route_add(struct poptrie *, struct radix_node **, __uint128_t, int,
           poptrie_leaf_t, int, struct radix_node *);
static int
_update_subtree(struct poptrie *, struct radix_node *, __uint128_t, int);
static int
_descend_and_update(struct poptrie *, struct radix_node *, int,
                    struct poptrie_stack *, __uint128_t, int, int, u32 *);
static int
_update_dp1(struct poptrie *, struct radix_node *, int, __uint128_t, int, int);
static int
_update_dp2(struct poptrie *, struct radix_node *, int, __uint128_t, int, int);
static void _clear_mark(struct radix_node *);
static int
_route_change(struct poptrie *, struct radix_node **, __uint128_t, int,
              poptrie_leaf_t, int);
static int
_route_update(struct poptrie *, struct radix_node **, __uint128_t, int,
              poptrie_leaf_t, int, struct radix_node *);
static int
_route_del(struct poptrie *, struct radix_node **, __uint128_t, int, int,
           struct radix_node *);
static poptrie_fib_index_t
_rib_lookup(struct radix_node *, __uint128_t, int, struct radix_node *);

/*
 * Add a route
 */
int
poptrie6_route_add(struct poptrie *poptrie, __uint128_t prefix, int len,
                   void *nexthop)
{
    int ret;
    int n;

    /* Find the FIB entry mapping first */
    n = poptrie_fib_ref(poptrie, nexthop);

    /* Insert the prefix to the radix tree, then incrementally update the
       poptrie data structure */
    ret = _route_add(poptrie, &poptrie->radix, prefix, len, n, 0, NULL);
    if ( ret < 0 ) {
        poptrie_fib_deref(poptrie, nexthop);
        return ret;
    }

    return 0;
}

/*
 * Change a route
 */
int
poptrie6_route_change(struct poptrie *poptrie, __uint128_t prefix, int len,
                     void *nexthop)
{
    int n;
    int ret;

    /* Find the FIB entry mapping first */
    n = poptrie_fib_ref(poptrie, nexthop);

    /* Try to route change */
    ret = _route_change(poptrie, &poptrie->radix, prefix, len, n, 0);

    return ret;
}

/*
 * Update a route (add if not exists like BGP update)
 */
int
poptrie6_route_update(struct poptrie *poptrie, __uint128_t prefix, int len,
                      void *nexthop)
{
    int ret;
    int n;

    /* Find the FIB entry mapping first */
    n = poptrie_fib_ref(poptrie, nexthop);

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
poptrie6_route_del(struct poptrie *poptrie, __uint128_t prefix, int len)
{
    /* Search and delete the corresponding entry */
    return _route_del(poptrie, &poptrie->radix, prefix, len, 0, NULL);
}

/*
 * Lookup a route by the specified address
 */
void *
poptrie6_lookup(struct poptrie *poptrie, __uint128_t addr)
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
        return poptrie->fib.entries[poptrie->dir[idx] & (((u32)1 << 31) - 1)].entry;
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
            return poptrie->fib.entries[poptrie->leaves[base + idx - 1]].entry;
        }
    }

    /* Not to be reached here, but put this to dismiss a compiler warning. */
    return 0;
}

/*
 * Lookup the next hop from the radix tree (RIB table)
 */
void *
poptrie6_rib_lookup(struct poptrie *poptrie, __uint128_t addr)
{
    poptrie_fib_index_t idx;

    idx = _rib_lookup(poptrie->radix, addr, 0, NULL);
    return poptrie->fib.entries[idx].entry;
}

/*
 * Updated the marked subtree
 */
static int
_update_subtree(struct poptrie *poptrie, struct radix_node *node,
                __uint128_t prefix, int depth)
{
    int ret;
    struct poptrie_stack stack[KEYLENGTH / 6 + 1];
    struct radix_node *ntnode;
    int idx;
    int i;
    u32 *tmpdir;
    int inode;

    /* Sentinel */
    stack[0].inode = -1;
    stack[0].idx = -1;
    stack[0].width = -1;

    if ( depth < POPTRIE_S ) {
        /* The update is performed from more than one entries in the direct
           pointing array. */

        /* Copy the direct pointing array from the current one */
        memcpy(poptrie->altdir, poptrie->dir, sizeof(u32) << POPTRIE_S);

        /* Perform the update from the direct pointing at altdir */
        ret = _update_dp1(poptrie, poptrie->radix, 1, prefix, depth, 0);

        /* Replace the root */
        tmpdir = poptrie->dir;
        poptrie->dir = poptrie->altdir;
        poptrie->altdir = tmpdir;

        /* Get the starting index at the direct pointing corresponding to the
           depth */
        idx = INDEX(prefix, 0, POPTRIE_S)
            >> (POPTRIE_S - depth)
            << (POPTRIE_S - depth);
        /* Clean the old trie */
        for ( i = 0; i < (1 << (POPTRIE_S - depth)); i++ ) {
            if ( poptrie->dir[idx + i] != poptrie->altdir[idx + i] ) {
                /* This entry is updated then clean up the subtree */
                if ( (poptrie->dir[idx + i] & ((u32)1 << 31))
                     && !(poptrie->altdir[idx + i] & ((u32)1 << 31)) ) {
                    /* Updated from internal node to leaf */
                    _update_clean_subtree(poptrie, poptrie->altdir[idx + i]);
                    buddy_free2(poptrie->cnodes, poptrie->altdir[idx + i]);
                } else if ( !(poptrie->altdir[idx + i] & ((u32)1 << 31)) ) {
                    /* Updated from internal node to internal node */
                    _update_clean_root(poptrie, poptrie->dir[idx + i],
                                       poptrie->altdir[idx + i]);
                }
            }
        }
    } else if ( depth == POPTRIE_S ) {
        /* The update is performed from an entry in the direct pointing
           array. */
        ret = _update_dp1(poptrie, poptrie->radix, 0, prefix, depth, 0);
    } else {
        /* The update is performed at some triangles under the direct pointing
           array. */

        /* Get the index at direct pointing */
        idx = INDEX(prefix, 0, POPTRIE_S);
        /* Get the corresponding node in the radix tree */
        ntnode = _next_block(poptrie->radix, idx, 0, POPTRIE_S);
        /* Get the corresponding node */
        if ( poptrie->dir[idx] & ((u32)1 << 31) ) {
            /* If the entry points to a leaf */
            inode = -1;
        } else {
            /* If the entry points to an internal node */
            inode = poptrie->dir[idx];
        }
        /* Perform the update procedure by descending the trie */
        ret = _descend_and_update(poptrie, ntnode, inode, &stack[1], prefix,
                                  depth, POPTRIE_S, &poptrie->dir[idx]);
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
_descend_and_update(struct poptrie *poptrie, struct radix_node *tnode,
                    int inode, struct poptrie_stack *stack, __uint128_t prefix,
                    int len, int depth, u32 *root)
{
    int idx;
    int p;
    int n;
    struct poptrie_node *node;
    struct radix_node *ntnode;
    int width;
    int ninode;

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
            /* If the current node was leaf, then update the partial tree. */
            return _update_part(poptrie, tnode, inode, stack, root, 0);
        }

        /* Get the corresponding node */
        node = poptrie->nodes + inode + NODEINDEX(idx);

        /* The root of the next block */
        ntnode = _next_block(tnode, idx, 0, width);
        if ( NULL == ntnode ) {
            return _update_part(poptrie, tnode, inode, stack, root, 0);
        }

        /* Check the vector */
        if ( VEC_BT(node->vector, BITINDEX(idx)) ) {
            /* Internal node, then traverse to the child */
            p = POPCNT_LS(node->vector, BITINDEX(idx));
            n = (p - 1);
            /* The root of the next block */
            ninode = node->base1 + n;
        } else {
            /* Leaf node, then update from this node */
            ninode = -1;
        }
        stack->inode = inode;
        stack->idx = idx;
        stack->width = width;
        stack++;
        return _descend_and_update(poptrie, ntnode, ninode, stack, prefix, len,
                                   depth + width, root);
    }
}

/*
 * Update a partial tree (direct pointing)
 */
static int
_update_dp1(struct poptrie *poptrie, struct radix_node *tnode, int alt,
            __uint128_t prefix, int len, int depth)
{
    int i;
    int idx;

    if ( depth == len ) {
        return _update_dp2(poptrie, tnode, alt, prefix, len, depth);
    }

    if ( BT(prefix, KEYLENGTH - depth - 1) ) {
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
_update_dp2(struct poptrie *poptrie, struct radix_node *tnode, int alt,
            __uint128_t prefix, int len, int depth)
{
    int i;
    int idx;
    int ret;
    struct poptrie_stack stack[KEYLENGTH / 6 + 1];

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
        prefix |= 1 << (KEYLENGTH - depth - 1);
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
 * Recursive function to add a route to the poptrie data structure while
 * inserting the route to the RIB (radix tree)
 */
static int
_route_add(struct poptrie *poptrie, struct radix_node **node,
           __uint128_t prefix, int len, poptrie_leaf_t nexthop, int depth,
           struct radix_node *ext)
{
    if ( NULL == *node ) {
        *node = malloc(sizeof(struct radix_node));
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
        (*node)->mark = poptrie_route_add_propagate(*node, *node);

        /* Update the poptrie subtree */
        return _update_subtree(poptrie, *node, prefix, depth);
    } else {
        if ( (*node)->valid ) {
            ext = *node;
        }
        if ( BT(prefix, KEYLENGTH - depth - 1) ) {
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

/*
 * Change a route
 */
static int
_route_change(struct poptrie *poptrie, struct radix_node **node,
              __uint128_t prefix, int len, poptrie_leaf_t nexthop, int depth)
{
    int ret;
    int n;

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
            n = (*node)->nexthop;
            (*node)->nexthop = nexthop;
            (*node)->mark = poptrie_route_change_propagate(*node, *node);

            /* Marked root */
            ret = _update_subtree(poptrie, *node, prefix, depth);

            /* Dereference this entry */
            poptrie->fib.entries[n].refs--;

            return ret;
        } else {
            n = nexthop;
            /* Dereference this entry */
            poptrie->fib.entries[n].refs--;

            return 0;
        }
    } else {
        if ( BT(prefix, KEYLENGTH - depth - 1) ) {
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

/*
 * Update a route
 */
static int
_route_update(struct poptrie *poptrie, struct radix_node **node,
              __uint128_t prefix, int len, poptrie_leaf_t nexthop, int depth,
              struct radix_node *ext)
{
    int ret;
    int n;

    if ( NULL == *node ) {
        *node = malloc(sizeof(struct radix_node));
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
                n = (*node)->nexthop;
                (*node)->nexthop = nexthop;
                (*node)->mark = poptrie_route_change_propagate(*node, *node);

                /* Marked root */
                ret = _update_subtree(poptrie, *node, prefix, depth);

                /* Dereference this entry */
                poptrie->fib.entries[n].refs--;

                return ret;
            } else {
                n = nexthop;
                /* Dereference this entry */
                poptrie->fib.entries[n].refs--;

                return 0;
            }
        } else {
            (*node)->valid = 1;
            (*node)->nexthop = nexthop;
            (*node)->len = len;

            /* Propagate this route to children */
            (*node)->mark = poptrie_route_add_propagate(*node, *node);

            /* Update MBT */
            return _update_subtree(poptrie, *node, prefix, depth);
        }
    } else {
        if ( (*node)->valid ) {
            ext = *node;
        }
        if ( BT(prefix, KEYLENGTH - depth - 1) ) {
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
_route_del(struct poptrie *poptrie, struct radix_node **node,
           __uint128_t prefix, int len, int depth, struct radix_node *ext)
{
    int ret;
    int n;

    if ( NULL == *node ) {
        return -1;
    }

    if ( len == depth ) {
        if ( !(*node)->valid ) {
            /* No entry found */
            return -1;
        }

        /* Propagate first */
        (*node)->mark = poptrie_route_del_propagate(*node, *node, ext);

        /* Invalidate the node */
        n = (*node)->nexthop;
        (*node)->valid = 0;
        (*node)->nexthop = 0;

        /* Marked root */
        ret = _update_subtree(poptrie, *node, prefix, depth);
        if ( ret < 0 ) {
            return -1;
        }

        /* May need to delete this node if both children are empty, but we do
           not care in this implementation because we have a large amount
           memory and the unused memory does not affect the performance. */

        /* Dereference this entry */
        poptrie->fib.entries[n].refs--;

        return 0;
    } else {
        /* Update the propagate node if valid */
        if ( (*node)->valid ) {
            ext = *node;
        }
        /* Traverse a child node */
        if ( BT(prefix, KEYLENGTH - depth - 1) ) {
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

/*
 * Lookup from the RIB table
 */
static poptrie_fib_index_t
_rib_lookup(struct radix_node *node, __uint128_t addr, int depth,
            struct radix_node *en)
{
    if ( NULL == node ) {
        return 0;
    }
    if ( node->valid ) {
        en = node;
    }

    if ( BT(addr, KEYLENGTH - depth - 1) ) {
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
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
