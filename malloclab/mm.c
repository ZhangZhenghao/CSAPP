/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"
#include "red_black_tree.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Zhenghao Zhang",
    /* First member's full name */
    "Zhenghao Zhang",
    /* First member's email address */
    "zhangzhenghao@zjut.edu.cn",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* Basic constants and macros */
#define WSIZE               4
#define DSIZE               8
#define QSIZE		        16
#define CHUNKSIZE           (1<<12)
#define BLK_HDFT_SIZE       DSIZE
#define BLK_LINK_SIZE       (sizeof(rbt_node))
#define BLK_MIN_SIZE        ((BLK_HDFT_SIZE+BLK_LINK_SIZE+DSIZE-1)/DSIZE*DSIZE)

#define MAX(a,b)            ((a)>(b)?(a):(b))
#define MIN(a,b)            ((a)<(b)?(a):(b))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)   ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)              (*(unsigned int*)(p))
#define PUT(p, val)         (*(unsigned int*)(p) = (val))

/* Read the size and allocated fields from adress p */
#define GET_SIZE(p)         (GET(p) & ~0x7)
#define GET_ALLOC(p)        (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)            ((char*)(bp) - WSIZE)
#define FTRP(bp)            ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)       ((char*)(bp) + GET_SIZE(((char*)(bp) - WSIZE)))
#define PREV_BLKP(bp)       ((char*)(bp) - GET_SIZE(((char*)(bp) - DSIZE)))

static rbt_tree tree;
static rbt_node root;
static rbt_node nil;

int compare(const void* lhs, const void* rhs)
{
    return lhs < rhs;
}

/*
 * insert_free_block - insert a free block into segregated free list
 */
static void insert_free_block(void *bp)
{
    rbt_node *np = (rbt_node *) bp;
    np->key = (void *) GET_SIZE(HDRP(bp));
    rbt_insert(&tree, np);
}

/*
 * remove_free_block - remove a free block in the segregated free lsit
 */
static void remove_free_block(void *bp)
{
    rbt_node *np = (rbt_node *) bp;
    rbt_remove(&tree, np);
}

/*
 * coalesce - coalesce previous or next free blocks
 */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {        
    	insert_free_block(bp);
        return bp;
    } else if (prev_alloc && !next_alloc) { 
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        remove_free_block(NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        insert_free_block(bp);
    } else if (!prev_alloc && next_alloc) { 
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        remove_free_block(PREV_BLKP(bp));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        insert_free_block(bp);
    } else {                                
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        remove_free_block(PREV_BLKP(bp));
        remove_free_block(NEXT_BLKP(bp));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        insert_free_block(bp);
    }
    return bp;
}

/*
 * extend_heap - extends the heap with a new free block
 */
static void *extend_heap(size_t words)
{
    /* Allocate an even number of words to maintain alignment */
    size_t size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    char *bp = mem_sbrk(size);
    if ((long)(bp) == -1)
        return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));           /* Free block header */
    PUT(FTRP(bp), PACK(size, 0));           /* Free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));   /* New epilogue header*/

    /* Coalesce if the prvious block was free */
    return coalesce(bp);
}

/*
 * find_fit - find suitable free block
 */
static void *find_fit(size_t asize)
{
    /* Available block not found */
    rbt_node *np = rbt_query_ge(&tree, (void *) asize);
    return np;
}

/*
 * place - update information of a free block
 */
static void place(void *bp, size_t asize)
{
    int free_size = GET_SIZE(HDRP(bp));
    int used_size = (free_size - asize >= BLK_MIN_SIZE) ? asize : free_size;
    int left_size = free_size - used_size;

    remove_free_block(bp);

    /* Place block */
    PUT(HDRP(bp), PACK(used_size, 1));
    PUT(FTRP(bp), PACK(used_size, 1));

    /* Place left block */
    if (left_size) {
        PUT(HDRP(NEXT_BLKP(bp)), PACK(left_size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(left_size, 0));
        insert_free_block(NEXT_BLKP(bp));
    }
}

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    /* Create the initial empty heap */
    char *heap_listp;
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0);

    /* Alignment padding */
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); /* Prologue header */
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));

    /* Epilogue header */
    heap_listp += (2*WSIZE);

    rbt_init(&tree, &nil, &root, compare);

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    return 0;	
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    /* Ignore spurious request */
    if (size == 0)
        return NULL;

    /* Adjust size */
    size_t asize = DSIZE * ((size + BLK_HDFT_SIZE + (DSIZE-1)) / DSIZE);
    if (asize <= BLK_MIN_SIZE)
        asize = BLK_MIN_SIZE;

    /* Search for free list for a fit */
    char *bp;
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    size_t extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *bp, size_t size)
{
    /* If ptr equals to NULL, do malloc */
    if (bp == NULL)
        return mm_malloc(size);

    /* If size equals to 0, do free */
    if (size == 0) {
        mm_free(bp);
        return NULL;
    }

    /* Calculate new block size */
    size_t asize = DSIZE * ((size + BLK_HDFT_SIZE + (DSIZE-1)) / DSIZE);
    if (asize <= BLK_MIN_SIZE)
        asize = BLK_MIN_SIZE;

    /* Get information about block size and allocate status */
    size_t next_alloc   = GET_ALLOC (HDRP(NEXT_BLKP(bp)));
    size_t prev_alloc   = GET_ALLOC (HDRP(PREV_BLKP(bp)));
    size_t next_size    = GET_SIZE  (HDRP(NEXT_BLKP(bp)));
    size_t prev_size    = GET_SIZE  (HDRP(PREV_BLKP(bp)));
    size_t old_size     = GET_SIZE  (HDRP(bp));
    size_t copy_size    = MIN       (old_size-BLK_HDFT_SIZE, size);

    /* New block size is equal or smaller than old block */
    if (asize <= old_size) {           
    	size_t used_size = (old_size - asize >= BLK_MIN_SIZE) ? asize : old_size;
    	size_t left_size = old_size - used_size;
    	PUT(HDRP(bp), PACK(used_size, 1));
    	PUT(FTRP(bp), PACK(used_size, 1));
    	if (left_size) {
    		PUT(HDRP(NEXT_BLKP(bp)), PACK(left_size, 0));
    		PUT(FTRP(NEXT_BLKP(bp)), PACK(left_size, 0));
    		insert_free_block(NEXT_BLKP(bp));
    	}
        return bp;
    } 

    /* New block size is bigger than old block */
    if (!next_alloc && prev_alloc && old_size + next_size >= asize) {	
        /* Merge with next free block */
        size_t merged_size = old_size + next_size;
    	size_t used_size = (merged_size - asize >= BLK_MIN_SIZE) ? asize : merged_size;
    	size_t left_size = merged_size - used_size;
        remove_free_block(NEXT_BLKP(bp));
    	PUT(HDRP(bp), PACK(used_size, 1));
    	PUT(FTRP(bp), PACK(used_size, 1));
    	if (left_size) {
    		PUT(HDRP(NEXT_BLKP(bp)), PACK(left_size, 0));
    		PUT(FTRP(NEXT_BLKP(bp)), PACK(left_size, 0));
    		insert_free_block(NEXT_BLKP(bp));
    	}
    	return bp;
    } else if (!prev_alloc && next_alloc && old_size + prev_size >= asize) {	
        /* Merge with previous free block */
        size_t merged_size = old_size + prev_size;
        size_t used_size = (merged_size - asize >= BLK_MIN_SIZE) ? asize : merged_size;
    	size_t left_size = merged_size - used_size;
    	char *nbp = PREV_BLKP(bp);
    	remove_free_block(nbp);
        memmove(nbp, bp, copy_size);
    	PUT(HDRP(nbp), PACK(used_size, 1));
    	PUT(FTRP(nbp), PACK(used_size, 1));
    	if (left_size) {
    		PUT(HDRP(NEXT_BLKP(nbp)), PACK(left_size, 0));
    		PUT(FTRP(NEXT_BLKP(nbp)), PACK(left_size, 0));
    		insert_free_block(NEXT_BLKP(nbp));
    	}
    	return nbp;
    } else if (!prev_alloc && !next_alloc && prev_alloc + old_size + next_size >= asize) {	
        /* Merge with previous and next free block */
        size_t merged_size = old_size + prev_size + next_size;
    	size_t used_size = (merged_size - asize >= BLK_MIN_SIZE) ? asize : merged_size;
    	size_t left_size = merged_size - used_size;
        char *nbp = PREV_BLKP(bp);
        remove_free_block(PREV_BLKP(bp));
    	remove_free_block(NEXT_BLKP(bp));
        memmove(nbp, bp, copy_size);
    	PUT(HDRP(nbp), PACK(used_size, 1));
    	PUT(FTRP(nbp), PACK(used_size, 1));
    	if (left_size) {
    		PUT(HDRP(NEXT_BLKP(nbp)), PACK(left_size, 0));
    		PUT(FTRP(NEXT_BLKP(nbp)), PACK(left_size, 0));
    		insert_free_block(NEXT_BLKP(nbp));
    	}
    	return nbp;
    }

    /* Malloc new space */
    char *nbp = mm_malloc(size);
    if (nbp == NULL)
    	return NULL;
    memmove(nbp, bp, copy_size);
    mm_free(bp);
    return nbp;
}