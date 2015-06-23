/*_
 * Copyright (c) 2014-2015 Hirochika Asai <asai@jar.jp>
 * All rights reserved.
 */

#include "buddy.h"
#include "poptrie.h"
#include <stdlib.h>
#include <string.h>

#define BUDDY_EOL 0xffffffffUL

/*
 * Initialize buddy system
 */
int
buddy_init(struct buddy *bs, int sz, int level, int bsz)
{
    int i;
    u8 *b;
    u32 *buddy;
    void *blocks;
    u64 off;

    /* Block size must be >= 32 bits */
    if ( bsz < 4 ) {
        return -1;
    }

    /* Heads */
    buddy = malloc(sizeof(u32) * level);
    if ( NULL == buddy ) {
        return -1;
    }
    /* Pre allocated nodes */
    blocks = malloc(bsz * (1 << sz));
    if ( NULL == blocks ) {
        free(buddy);
        return -1;
    }
    /* Bitmap */
    b = malloc(((1 << (sz)) + 7) / 8);
    if ( NULL == b ) {
        free(blocks);
        free(buddy);
        return -1;
    }
    (void)memset(b, 0, ((1 << (sz)) + 7) / 8);

    /* Initialize buddy system */
    for ( i = 0; i < level; i++ ) {
        buddy[i] = BUDDY_EOL;
    }
    if ( sz < level ) {
        buddy[sz] = 0;
        *(u32 *)blocks = 0;      /* Terminate */
    } else {
        buddy[level - 1] = 0;
        for ( i = 0; i < (1 << (sz - level + 1)); i++ ) {
            off = bsz * (i * (1 << (level - 1)));
            if ( i == (1 << (sz - level + 1)) - 1 ) {
                *(u32 *)(blocks + off) = BUDDY_EOL;
            } else {
                *(u32 *)(blocks + off)
                    = (u32)((i + 1) * (1 << (level - 1)));
            }
        }
    }

    /* Set */
    bs->sz = sz;
    bs->bsz = bsz;
    bs->level = level;
    bs->buddy = buddy;
    bs->blocks = blocks;
    bs->b = b;

    return 0;
}

/*
 * Release the buddy system
 */
void
buddy_release(struct buddy *bs)
{
    free(bs->buddy);
    free(bs->blocks);
    free(bs->b);
}

/*
 * Split the block at the level n
 */
static int
_split_buddy(struct buddy *bs, int lv)
{
    int ret;
    u32 next;

    /* Check the head of the current level */
    if ( BUDDY_EOL != bs->buddy[lv] ) {
        return 0;
    }

    /* Check the size */
    if ( lv + 1 >= bs->level ) {
        return -1;
    }

    /* Check the head of the upper level */
    if ( BUDDY_EOL == bs->buddy[lv + 1] ) {
        ret = _split_buddy(bs, lv + 1);
        if ( ret < 0 ) {
            return ret;
        }
    }

    /* Split */
    bs->buddy[lv] = bs->buddy[lv + 1];
    bs->buddy[lv + 1] = *(u32 *)((u64)bs->blocks + bs->bsz * bs->buddy[lv]);
    next = bs->buddy[lv] + (1<<lv);
    *(u32 *)((u64)bs->blocks + bs->bsz * next) = BUDDY_EOL;
    *(u32 *)((u64)bs->blocks + bs->bsz * bs->buddy[lv]) = next;

    return 0;
}

/*
 * Allocate (2**sz) blocks
 */
void *
buddy_alloc(struct buddy *bs, int n)
{
    int ret;

    ret = buddy_alloc2(bs, n);
    if ( ret < 0 ) {
        return NULL;;
    }

    return (void *)((u64)bs->blocks + bs->bsz * ret);
}
int
buddy_alloc2(struct buddy *bs, int sz)
{
    int ret;
    u32 a;
    u32 b;

    /* Check the argument */
    if ( sz < 0 ) {
        return -1;
    }

    /* Check the size */
    if ( sz >= bs->level ) {
        return -1;
    }

    /* Split first if needed */
    ret = _split_buddy(bs, sz);
    if ( ret < 0 ) {
        return -1;
    }

    /* Obtain from the head */
    a = bs->buddy[sz];
    b = *(u32 *)(bs->blocks + bs->bsz * a);
#if 0
    printf("ALLOC %p %x, %x [%x/%d]\n", bs, a, b, sz, bs->bsz);
#endif
    bs->buddy[sz] = b;

    /* Flag the tail block in bitmap */
    bs->b[(a + (1 << sz) - 1) >> 3] |= 1 << ((a + (1 << sz) - 1) & 0x7);

    return a;
}

/*
 * Merge blocks onto an upper level if possible
 */
static void
_merge(struct buddy *bs, int off, int lv)
{
    int i;
    u32 s;
    u32 *n;

    if ( lv + 1 >= bs->level ) {
        /* Reached maximum */
        return;
    }

    s = off / (1 << (lv + 1)) * (1 << (lv + 1));
    for ( i = 0; i < (1 << (lv + 1)); i++ ) {
        if ( bs->b[(s + i) >> 3] & (1 << ((s + i) & 7)) ) {
            /* Found one */
            return;
        }
    }
    /* All bits were zero, then take the corresponding two blocks from the
       current level */
    n = &bs->buddy[lv];
    while ( BUDDY_EOL != *n ) {
        if ( s == *n || (s + (1 << lv)) == *n ) {
            /* Remove this */
            *n = *(u32 *)(bs->blocks + bs->bsz * (*n));
        } else {
            n = (u32 *)(bs->blocks + bs->bsz * (*n));
        }
    }

    /* Append it to the upper level */
    *(u32 *)(bs->blocks + bs->bsz * s) = bs->buddy[lv + 1];
    bs->buddy[lv + 1] = s;

    /* Try to merge the upper level */
    _merge(bs, s, lv + 1);
}

/*
 * Free
 */
void
buddy_free(struct buddy *bs, void *a)
{
    int off;

    /* Calculate the offset */
    off = ((u64)a - (u64)bs->blocks) / bs->bsz;

    buddy_free2(bs, off);
}
void
buddy_free2(struct buddy *bs, int a)
{
    int sz;
    u32 next;
    u32 *n;

    /* Find the size */
    sz = 0;
    for ( ;; ) {
        if ( bs->b[(a + (1 << sz) - 1) >> 3]
             & (1 << ((a + (1 << sz) - 1) & 0x7)) ) {
            break;
        }
        sz++;
    }

    if ( sz >= bs->level ) {
        /* Something is wrong... */
        return;
    }

    /* Unflag the tail block in bitmap */
    bs->b[(a + (1 << sz) - 1) >> 3] &= ~(1 << ((a + (1 << sz) - 1) & 0x7));

    /* Return to the buddy system */
    n = &bs->buddy[sz];
    while ( BUDDY_EOL != *n ) {
        if ( (u32)a < *n ) {
            next = *n;
            *n = a;
            *(u32 *)(bs->blocks + bs->bsz * a) = next;
            n = NULL;
            break;
        } else {
            n = (u32 *)(bs->blocks + bs->bsz * (*n));
        }
    }
    if ( n != NULL ) {
        *n = a;
        *(u32 *)(bs->blocks + bs->bsz * a) = BUDDY_EOL;
    }


    _merge(bs, a, sz);
}

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
