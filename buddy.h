/*_
 * Copyright (c) 2014-2015 Hirochika Asai <asai@jar.jp>
 * All rights reserved.
 */

#include "poptrie.h"
#include <stdlib.h>

#ifndef _POPTRIE_BUDDY_H
#define _POPTRIE_BUDDY_H

/*
 * Buddy system
 */
struct buddy {
    /* Size of buddy system (# of blocks) */
    int sz;
    /* Size of each block */
    int bsz;
    /* Bitmap */
    u8 *b;
    /* Memory blocks */
    void *blocks;
    /* Level */
    int level;
    /* Heads */
    u32 *buddy;
};

#ifdef __cplusplus
extern "C" {
#endif

    /* buddy.c */
    int buddy_init(struct buddy *, int, int, int);
    void buddy_release(struct buddy *);
    void * buddy_alloc(struct buddy *, int);
    int buddy_alloc2(struct buddy *, int);
    void buddy_free(struct buddy *, void *);
    void buddy_free2(struct buddy *, int);

#ifdef __cplusplus
}
#endif

#endif /* _POPTRIE_BUDDY_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
