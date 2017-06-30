/*_
 * Copyright (c) 2014-2017 Hirochika Asai <asai@jar.jp>
 * All rights reserved.
 */

#include "buddy.h"
#include "poptrie.h"
#include <stdlib.h>
#include <string.h>

#define INDEX(a, s, n) \
    (((u64)(a) << 32 >> (64 - ((s) + (n)))) & ((1 << (n)) - 1))

#define KEYLENGTH       32


/* Prototype declaration */
static void _release_radix(struct radix_node *);

/*
 * Initialize the poptrie data structure
 */
struct poptrie *
poptrie_init(struct poptrie *poptrie, int sz1, int sz0)
{
    int ret;
    int i;

    if ( NULL == poptrie ) {
        /* Allocate new one */
        poptrie = malloc(sizeof(struct poptrie));
        if ( NULL == poptrie ) {
            return NULL;
        }
        (void)memset(poptrie, 0, sizeof(struct poptrie));
        /* Set the flag indicating that this data structure needs free() when
           released. */
        poptrie->_allocated = 1;
    } else {
        /* Write zero's */
        (void)memset(poptrie, 0, sizeof(struct poptrie));
    }

    /* Allocate the nodes and leaves */
    poptrie->nodes = malloc(sizeof(poptrie_node_t) * (1 << sz1));
    if ( NULL == poptrie->nodes ) {
        poptrie_release(poptrie);
        return NULL;
    }
    poptrie->leaves = malloc(sizeof(poptrie_leaf_t) * (1 << sz0));
    if ( NULL == poptrie->leaves ) {
        poptrie_release(poptrie);
        return NULL;
    }

    /* Prepare the buddy system for the internal node array */
    poptrie->cnodes = malloc(sizeof(struct buddy));
    if ( NULL == poptrie->cnodes ) {
        poptrie_release(poptrie);
        return NULL;
    }
    ret = buddy_init(poptrie->cnodes, sz1, sz1, sizeof(u32));
    if ( ret < 0 ) {
        free(poptrie->cnodes);
        poptrie->cnodes = NULL;
        poptrie_release(poptrie);
        return NULL;
    }

    /* Prepare the buddy system for the leaf node array */
    poptrie->cleaves = malloc(sizeof(struct buddy));
    if ( NULL == poptrie->cleaves ) {
        poptrie_release(poptrie);
        return NULL;
    }
    ret = buddy_init(poptrie->cleaves, sz0, sz0, sizeof(u32));
    if ( ret < 0 ) {
        free(poptrie->cnodes);
        poptrie->cnodes = NULL;
        poptrie_release(poptrie);
        return NULL;
    }

    /* Prepare the direct pointing array */
    poptrie->dir = malloc(sizeof(u32) << POPTRIE_S);
    if ( NULL == poptrie->dir ) {
        poptrie_release(poptrie);
        return NULL;
    }
    for ( i = 0; i < (1 << POPTRIE_S); i++ ) {
        poptrie->dir[i] = (u32)1 << 31;
    }

    /* Prepare the alternative direct pointing array for the update procedure */
    poptrie->altdir = malloc(sizeof(u32) << POPTRIE_S);
    if ( NULL == poptrie->altdir ) {
        poptrie_release(poptrie);
        return NULL;
    }

    /* Prepare the FIB mapping table */
    poptrie->fib.entries = malloc(sizeof(struct poptrie_fib_entry)
                                  * POPTRIE_INIT_FIB_SIZE);
    if ( NULL == poptrie->fib.entries ) {
        poptrie_release(poptrie);
        return NULL;
    }
    memset(poptrie->fib.entries, 0, sizeof(struct poptrie_fib_entry)
           * POPTRIE_INIT_FIB_SIZE);
    poptrie->fib.sz = POPTRIE_INIT_FIB_SIZE;
    /* Insert a NULL entry as the default route */
    poptrie->fib.entries[0].entry = NULL;
    poptrie->fib.entries[0].refs = 1;

    return poptrie;
}

/*
 * Release the poptrie data structure
 */
void
poptrie_release(struct poptrie *poptrie)
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
 * Free the allocated memory by the radix tree
 */
static void
_release_radix(struct radix_node *node)
{
    if ( NULL != node ) {
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
