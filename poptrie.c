/*_
 * Copyright (c) 2014-2015 Hirochika Asai <asai@jar.jp>
 * All rights reserved.
 */

#include "buddy.h"
#include "poptrie.h"
#include <stdlib.h>
#include <string.h>

/*
 * Initialize the poptrie data structure
 */
struct poptrie *
poptrie_init(struct poptrie *poptrie, int sz1, int sz0)
{
    poptrie_node_t *nodes;
    poptrie_leaf_t *leaves;
    int ret;

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

    /* Prepare the alternative direct pointing array for the update procedure */
    poptrie->altdir = malloc(sizeof(u32) << POPTRIE_S);
    if ( NULL == poptrie->altdir ) {
        poptrie_release(poptrie);
        return NULL;
    }

    /* Prepare the FIB mapping table */
    poptrie->fib.entries = malloc(sizeof(void *) * POPTRIE_INIT_FIB_SIZE);
    if ( NULL == poptrie->fib.entries ) {
        poptrie_release(poptrie);
        return NULL;
    }
    poptrie->fib.sz = POPTRIE_INIT_FIB_SIZE;
    poptrie->fib.n = 0;

    return poptrie;
}

/*
 * Release the poptrie data structure
 */
void
poptrie_release(struct poptrie *poptrie)
{
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
poptrie_route_add(struct poptrie *poptrie, u32 prefix, int len, u32 nexthop)
{
    return -1;
}

/*
 * Change a route
 */
int
poptrie_route_change(struct poptrie *poptrie, u32 prefix, int len, u32 nexthop)
{
    return -1;
}

/*
 * Update a route (add if not exists like BGP update)
 */
int
poptrie_route_update(struct poptrie *poptrie, u32 prefix, int len, u32 nexthop)
{
    return -1;
}

/*
 * Delete a route
 */
int
poptrie_route_del(struct poptrie *poptrie, u32 prefix, int len)
{
    return -1;
}

/*
 * Lookup a route by the specified address
 */
u32
poptrie_lookup(struct poptrie *poptrie, u32 addr)
{
    return 0;
}

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
