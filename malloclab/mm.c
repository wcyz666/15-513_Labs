/* 
 * Name: Wang Cheng
 * CMU ID: chengw1
 * This implementation is a simple segregate list with two buckets;
 * One is less or equal than 72, the other is larger than 72.
 * It applies best fit in a modified way.
 * Footer is removed in this implementation.
 * Immediate coalesce is used in this implementation.
 * The alignment of the heap is as following:
 *  
 * 0x800000000 -------------------------------------> higner
 * seg[0] | seg[1] | prologue | ----payload---- | epilogue |   
 * pointer(16 byte)| 12 bytes |                 | 4 bytes  |
 * 
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mm.h"
#include "memlib.h"

/* DEBUG
 *     When this macro is defined, the program will print hints once
 *     declared functions are called. It also will check the heap when
 *     entering or leaving any declared function.
 *
 * DEBUG_VERBOSE
 *     When this macro is defined, the program will print the details 
 *        of each blocks when checking the heap.
 */
 
//#define DEBUG
//#define DEBUG_VERBOSE

/* Basic constants and macros */

/* Here WSIZE may be a little bit wired, because size of a word should
 * be 8 instead of 4. I use 4 here just because all pointers used in this
 * implementation are compressed to 4.
 */

#define WSIZE       4      
#define DSIZE       8      

/* It has been under many experiment and I concluded that chunk size of 64
 * and two lists give the best performance as a whole.
 */

#define CHUNKSIZE  (1<<6)  
#define BUCKET_COUNT 2

/* In this implementation, there are two kinds of blocks:
 *
 *        1. Free block: 4 byte header + 4 byte prev pointer + 4 byte next
 *        pointer + 4 byte footer, minimum size: 16 bytes
 *        
 *        2. Allocated block: 4 byte header + payload. minimum payload size: 
 *        12 bytes. minimum size: 16 bytes.
 *
 * The start address of the heap is 0x800000000. Pointers pointing to this addr
 * will be regarded as pointing to NULL.
 *
 */

#define MINBLOCK 16
#define START 0x800000000
#define NIL (void *)START

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc)) 


/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p)) 
#define PUT(p, val)  (*(unsigned int *)(p) = (val))      

/* Read and Write the prev or next pointer of a block
 * It should be mentioned that only the lower 4 bytes are saved in the block,
 * Since the size of the heap is less than 2^32 bytes. Lower 4 bytes is enough
 *
 * However, segregate list pointer are not compressed.
 *
 */
#define COMPRESS(p) ((unsigned int)((size_t)p))

#define PREV(p)     ((void *)((*(unsigned int *)p) | START))
#define SUCC(p)     ((void *)((*(unsigned int *)((char*)(p) + WSIZE)) | START))
#define PUT_PREV(p, val) (*(unsigned int*)(p) = COMPRESS(val))
#define PUT_SUCC(p, val) (*(unsigned int*)((char*)(p) + WSIZE) = COMPRESS(val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)                   
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Since footer is removed in the allocated list, we have to store related info
 * in the following block. Here the last second bit is used to store the info
 */

#define GET_PREV_ALLOC(p) ((GET(p) >> 1) & 0x1)
#define PUT_PREV_ALLOC(p, val) (PUT(p, PACK(GET(p) & (~0x2), val << 1)))

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)                      
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) 

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) 
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) 


/* Global variables */

/* Pointer to first block */ 
static char *heap_listp = 0; 

/* Pointer to the segregate list */   
static char** seg; 

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void printblock(void *bp); 
static void checkblock(void *bp);
static void checkFreeList(int);
void printList();
static int getPtrBySize(unsigned int);


/* 
 * mm_init - Initialize the memory manager 
 */

int mm_init(void) 
{
    /* Create the initial empty heap */
    int i, size;
#ifdef DEBUG
	checkFreeList(1);
    printf("init called\n");
#endif
    
    size = 4 * WSIZE + BUCKET_COUNT * DSIZE;
    if ((heap_listp = mem_sbrk(size)) == (void *)-1) 
        return -1;
    
    /* Initialize segregate list.*/
    seg = (char **)heap_listp;
    for (i = 0; i < BUCKET_COUNT; i++)
        seg[i] = NIL;
    
    heap_listp += (BUCKET_COUNT * DSIZE);
    PUT(heap_listp, 0);                          /* Alignment padding */
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); /* Prologue header */ 
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); /* Prologue footer */ 
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     /* Epilogue header */
    heap_listp += (2 * WSIZE);                     

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) 
        return -1;
    return 0;
}


/* 
 * mm_malloc - Allocate a block with at least size bytes of payload 
 */

void *mm_malloc(size_t size) 
{
    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;    
    
#ifdef DEBUG
    printf("malloc called\n");
#endif

    if (heap_listp == 0){
        mm_init();
    }

    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment requirement.
     *      1. require less than 8 bytes -> adjust to MINBLOCK size
     *      2. require more than 8 bytes -> first, if size mod 8 is between
     *      0 and 4, then we will omit the remainder. Because under this
     *      situation, we can use the footer to hold the payload.
     *      Otherwise, we just add header and footer to satisfy alignment
     *      requirement.
     *
     */
     
    if (size <= 2 * WSIZE)                                          
        asize = MINBLOCK;                                        
    else {
        if ((size & 7) <= 4)
            size = size & (~7);
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE); 
    }

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {  
        place(bp, asize);                  
#ifdef DEBUG
    printf("malloc ended\n\n\n");
#endif
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);                 
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)  
        return NULL;                                  
    
    place(bp, asize);    
    
#ifdef DEBUG
    printf("malloc ended\n\n\n");
#endif
    return bp;
} 


/* 
 * mm_free - Free a block 
 */

void mm_free(void *bp)
{

    size_t alloc, size;
    
#ifdef DEBUG
    printf("free called\n");
#endif

    /* Ignore spurious requests */
    if(bp == 0) 
        return;

    if (heap_listp == 0){
        mm_init();
    }
    /* When an allocated block is freed, we preserve the prev_alloc
     * flag and set it to a free block. We also change the prev_alloc
     * flag of the next block. Finally we do immediately coalesce.
     */
     
    size = GET_SIZE(HDRP(bp));
    alloc = GET_PREV_ALLOC(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT_PREV_ALLOC(HDRP(bp), alloc);
    PUT_PREV_ALLOC(HDRP(NEXT_BLKP(bp)), 0);
    
    coalesce(bp);
    
#ifdef DEBUG
    printf("free ended\n\n\n");
#endif
}

/* getPtrBySize - return the index of list header pointer according to
 * 		to input size.
 *
 * Why 72 is the fastest number?
 * I don't know...
 */

static int getPtrBySize(unsigned int size) {
    if (size <= 72) {
        return 0;
    }
    return 1;
}

/*
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block
 */
static void *coalesce(void *bp) 
{
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp)), alloc;
    void * next, *prev;
    int result;
    
    result = getPtrBySize(size);    

    if (NEXT_BLKP(heap_listp) == bp)
        prev_alloc = 1;
#ifdef DEBUG
    printf("coalesce called\n");    
#endif

    if (prev_alloc && next_alloc) {            
#ifdef DEBUG
        printf("case 1\n");
#endif
        /* Case 1: No need to coalesce
         *     In the situation, we just put the coalesced block into the
         *     proper free list.
         */
         
        PUT_PREV(bp, 0);
        PUT_SUCC(bp, seg[result]);  
        if (SUCC(bp) != NIL)
            PUT_PREV(SUCC(bp), bp);
        seg[result] = bp;    
    }

    else if (prev_alloc && !next_alloc) {      
#ifdef DEBUG
        printf("case 2\n");
#endif
        /* Case 2: need to coalesce with the next block.*/
        
        /* Step 0: Extract next from its current list
         * If next is the first block of the list, do more work
         */

        next = NEXT_BLKP(bp);
        if (SUCC(next) != NIL)
            PUT_PREV(SUCC(next), PREV(next));
        if (PREV(next) != NIL)
            PUT_SUCC(PREV(next), SUCC(next));
        else {
            result = getPtrBySize(GET_SIZE(HDRP(next)));
            seg[result] = SUCC(next);
            if (seg[result] != NIL)
                PUT_PREV(seg[result], 0);
        }    
    
        size += GET_SIZE(HDRP(next));
        result = getPtrBySize(size);
        
        /* Step 1: preserve the prev_alloc flag and change the size of bp.*/

        alloc = GET_PREV_ALLOC(HDRP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        PUT_PREV_ALLOC(HDRP(bp), alloc);
        
        /* Step 2: we put bp into the proper free list.*/
    
        PUT_PREV(bp, 0);
        PUT_SUCC(bp, seg[result]);
        if (SUCC(bp) != NIL)
            PUT_PREV(SUCC(bp), bp);
        seg[result] = bp;

    }

    else if (!prev_alloc && next_alloc) {      

#ifdef DEBUG
        printf("case 3\n");
#endif

        /* Case 3: need to coalesce with the previous block.*/
        
        /* Step 0: Extract prev from its current list
         * If prev is the first block of the list, do more work
         */
        prev = PREV_BLKP(bp);
        alloc = GET_PREV_ALLOC(HDRP(prev));
        if (SUCC(prev) != NIL)
            PUT_PREV(SUCC(prev), PREV(prev));
        if (PREV(prev) != NIL)
            PUT_SUCC(PREV(prev), SUCC(prev));
        else {
            result = getPtrBySize(GET_SIZE(HDRP(prev)));
            seg[result] = SUCC(prev);
            if (seg[result] != NIL)
                PUT_PREV(seg[result], 0);
        }
        
        /* Step 1: preserve the prev_alloc flag and change the size of prev.*/
        size += GET_SIZE(HDRP(prev));
        result = getPtrBySize(size);
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(prev), PACK(size, 0));
        PUT_PREV_ALLOC(HDRP(prev), alloc);
        
        /* Step 2: we put prev into the proper free list.*/        
        PUT_PREV(prev, 0);
        PUT_SUCC(prev, seg[result]);
        if (SUCC(prev) != NIL)
            PUT_PREV(SUCC(prev), prev);
        bp = prev;
        seg[result] = bp;

    }

    else {                                    
#ifdef DEBUG
        printf("case 4\n");
#endif

        /* Case 3: need to coalesce with both the previous block 
         * and the next block.
         */
         
        /* Step 0: Extract next from its current list
         * If next is the first block of the list, do more work
         */ 
        
        next = NEXT_BLKP(bp);
        
        if (SUCC(next) != NIL)
            PUT_PREV(SUCC(next), PREV(next));
        if (PREV(next) != NIL)
            PUT_SUCC(PREV(next), SUCC(next));
        else {
             result = getPtrBySize(GET_SIZE(HDRP(next)));
             seg[result] = SUCC(next);
             if (seg[result] != NIL)
                 PUT_PREV(seg[result], 0);
        }
        
        /* Step 1: Extract prev from its current list
         * If prev is the first block of the list, do more work.
         */         
        prev = PREV_BLKP(bp);
        alloc = GET_PREV_ALLOC(HDRP(prev));
        if (SUCC(prev) != NIL)
            PUT_PREV(SUCC(prev), PREV(prev));
        if (PREV(prev) != NIL)
            PUT_SUCC(PREV(prev), SUCC(prev));
        else {
            result = getPtrBySize(GET_SIZE(HDRP(prev)));
            seg[result] = SUCC(prev);
            if (seg[result] != NIL)
                PUT_PREV(seg[result], 0);
        }
        
        /* Step 2: preserve the prev_alloc flag and change the size of prev.*/ 
        
        size += GET_SIZE(HDRP(prev)) + GET_SIZE(FTRP(next));
        result = getPtrBySize(size);       
        PUT(HDRP(prev), PACK(size, 0));
        PUT(FTRP(next), PACK(size, 0));     
        PUT_PREV_ALLOC(HDRP(prev), alloc);
        
        /* Step 3: we put prev into the proper free list.*/     
        
        PUT_PREV(prev, 0);
        PUT_SUCC(prev, seg[result]);
        if (SUCC(prev) != NIL)
            PUT_PREV(SUCC(prev), prev);
        bp = prev;
        seg[result] = bp;
    }
#ifdef DEBUG
        printf("After Coalesce\n");
        printList();
        printf("===========================\n");    
#endif
    return bp;
}


/*
 * mm_realloc - Naive implementation of realloc
 * Naive method is enough...
 * My optimization cannot improve it...So keep it naive
 */
 
void *mm_realloc(void *ptr, size_t size)
{
    size_t  oldsize;
    void *newptr;

#ifdef DEBUG
    printf("realloc called\n");    
#endif
    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0) {
        mm_free(ptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if(ptr == NULL) {
        return mm_malloc(size);
    }

    newptr = mm_malloc(size);

    /* If realloc() fails the original block is left untouched  */
    if(!newptr) {
        return 0;
    }

    /* Copy the old data. */
    oldsize = GET_SIZE(HDRP(ptr));
    if(size < oldsize) oldsize = size;
        memcpy(newptr, ptr, oldsize);

    /* Free the old block. */
    mm_free(ptr);

#ifdef DEBUG
    printf("realloc ended\n\n\n");
#endif
    return newptr;
}

/* 
 * calloc - Allocate a block with at least nmemb * size bytes of payload 
 *     and fill it with 0
 *     Very simple implementation.
 */
 
void *mm_calloc(size_t nmemb, size_t size)
{
	void* bp = NULL;
	size_t asize = nmemb * size;
	if (asize <= 0) {
		return NULL;
	}
	bp = mm_malloc(asize);
	if (bp == NULL)
		return NULL;
	
	memset(bp, 0, asize);
	return bp;
}

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */

static void *extend_heap(size_t words) 
{
    char *bp;
    size_t size, alloc;
#ifdef DEBUG
    printf("extend heap called\n");    
#endif
    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE; 
    if ((long)(bp = mem_sbrk(size)) == -1)  
        return NULL;   
    
    /* Initialize free block header/footer and the epilogue header */
    alloc = GET_PREV_ALLOC(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT_PREV_ALLOC(HDRP(bp), alloc);
    
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */ 

    /* Coalesce if the previous block was free */
    return coalesce(bp);                                          
}


/* 
 * place - Place block of asize bytes at start of free block bp 
 *         and split if remainder would be at least minimum block size
 */

static void place(void *bp, size_t asize) 
{
    size_t rsize, alloc, result;
    void *next, *prev, *succ;
     
#ifdef DEBUG
    printf("place called\n");    
#endif
    rsize = GET_SIZE(HDRP(bp)) - asize;
    prev = PREV(bp);
    succ = SUCC(bp);
    
    /* Step 0: Extract bp from its current list
     * If bp is the first block of the list, do more work
     */
    
    result = getPtrBySize(GET_SIZE(HDRP(bp)));  
    if (succ != NIL)    
        PUT_PREV(succ, prev);
    if (prev != NIL)
        PUT_SUCC(prev, succ);
    else {
         seg[result] = succ;
         if (seg[result] != NIL)
             PUT_PREV(seg[result], 0);
    }
    
    if (rsize >= MINBLOCK) {
#ifdef DEBUG
    printf("place part block\n");    
#endif
        /* Step 1: mark bp as allocated and preserve the 
         * prev_alloc flag.
         */
        
        alloc = GET_PREV_ALLOC(HDRP(bp));
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        PUT_PREV_ALLOC(HDRP(bp), alloc);

        /* Step 2: mark the remaining block as free and 
         * change the prev_alloc flag.
         */        
        next = NEXT_BLKP(bp);
        PUT(HDRP(next), PACK(rsize, 0));
        PUT(FTRP(next), PACK(rsize, 0));
        PUT_PREV_ALLOC(HDRP(next), 1);
        
        /* Step 3: put the remaining block into the front of 
         * the proper free list.
         */
        
        result = getPtrBySize(GET_SIZE(HDRP(next)));
        PUT_PREV(next, 0);
        PUT_SUCC(next, seg[result]);
        if (SUCC(next) != NIL)
            PUT_PREV(SUCC(next), next);
        seg[result] = next;
        
    }
    else {
#ifdef DEBUG
    printf("place whole block\n");    
#endif      
        /* Step 1: mark bp as allocated and preserve the 
         * prev_alloc flag.
         */
        
        alloc = GET_PREV_ALLOC(HDRP(bp));
        PUT(HDRP(bp), PACK(GET_SIZE(HDRP(bp)), 1));
        PUT(FTRP(bp), PACK(GET_SIZE(HDRP(bp)), 1));
        PUT_PREV_ALLOC(HDRP(bp), alloc);
        PUT_PREV_ALLOC(HDRP(NEXT_BLKP(bp)), 1);
    }
#ifdef DEBUG
        printf("place end\n");
#endif
}


/* 
 * find_fit - Find a fit for a block with asize bytes 
 *     This implementation applies best-fit algorithm in a slightly different
 *     way.
 *         1. When the program finds a block that can satisfy the requirement
 *         and the size of the block is at most 1.5 times of the requirement,
 *         it will return immediately.
 *         2. Otherwise, it will execute best-fit search in the current list.
 *         3. If it cannot find an available block, it will continue to search
 *         in the other list if possible.
 *     
 */

static void *find_fit(size_t asize)

{
    void* ptr, *bptr = NULL;
    size_t i;
    size_t bsize = 1 << 31, result, size = 0;
#ifdef DEBUG
    printf("find_fit called\n");    
#endif
    result = getPtrBySize(asize);
    for (i = result; i < BUCKET_COUNT; i++) {
        for (ptr = seg[i]; ptr != NIL; ptr = SUCC(ptr)) {
            size = GET_SIZE(HDRP(ptr));
            if (size == asize)  {
                return ptr;
            }
            else {
                if (size > asize && size - asize < bsize) {
                    bsize = size - asize;
                    bptr = ptr;
                    if (bsize <= asize / 2)
                        return bptr;
                }    
            }
        }
        if (bptr != NULL)
            return bptr;
    }
    return bptr;
}

/* 
 * printblock - print the detailed information of the free list.
 */

void printList() {
#ifdef DEBUG_VERBOSE
    void *x;
    int i;
    size_t hsize, halloc, fsize, falloc, nalloc;
    
    for (i = 0; i < BUCKET_COUNT; i++) {
        printf("seg list No.[%d]=========================\n", i);
        for (x = (void *)(seg[i]); x != NIL; x = SUCC(x)) {
            hsize = GET_SIZE(HDRP(x));
                halloc = GET_ALLOC(HDRP(x));  
            nalloc = GET_PREV_ALLOC(HDRP(x));
            fsize = GET_SIZE(FTRP(x));
            falloc = GET_ALLOC(FTRP(x));  

            printf("%p: header: [%p:%c:%c] footer: [%p:%c]\
                     prev: [%p] succ:[%p]\n", x, 
            (void *)hsize, (halloc ? 'a' : 'f'), (nalloc ? 'a' : 'f'), 
            (void *)fsize, (falloc ? 'a' : 'f'), PREV(x), SUCC(x)); 
        }
        printf("======================================\n");
    }
#endif
}

/* 
 * printblock - print the detailed information of the specified block.
 */

static void printblock(void *bp) 
{
    size_t hsize, halloc, fsize, falloc, nalloc;

    hsize = GET_SIZE(HDRP(bp));
    halloc = GET_ALLOC(HDRP(bp)); 
    nalloc = GET_PREV_ALLOC(HDRP(bp));
    fsize = GET_SIZE(FTRP(bp));
    falloc = GET_ALLOC(FTRP(bp));  
    if (hsize == 0) {
        printf("%p: header: [%p:%c:%c]\n",bp, 
        (void *)hsize, (halloc ? 'a' : 'f'), (nalloc ? 'a' : 'f')); 
        return;    
    }
    else if (!halloc) {
        printf("%p: header: [%p:%c:%c] footer: [%p:%c]\
             prev: [%p:%u] succ:[%p:%u]\n", bp, 
        (void *)hsize, (halloc ? 'a' : 'f'), (nalloc ? 'a' : 'f'),
        (void *)fsize, (falloc ? 'a' : 'f'), PREV(bp), GET(bp), 
        SUCC(bp), GET((char *)bp + 4)); 
    }
    else { 

        printf("%p: header: [%p:%c:%c] footer: [%p:%c]\n", bp, 
        (void *)hsize, (halloc ? 'a' : 'f'), (nalloc ? 'a' : 'f'),
        (void *)fsize, (falloc ? 'a' : 'f')); 
    }
    bp = bp;
}

/* 
 * checkblock - Maximal check of one block for consistency 
 */

static void checkblock(void *bp) 
{

    if ((size_t)bp % 8)
    	printf("Error: %p is not doubleword aligned\n", bp);
    
	if (!GET_ALLOC(HDRP(bp)) &&
             (GET(HDRP(bp)) | 0x2) != (GET(FTRP(bp)) | 0x2)) {
        printblock(bp);
        printf("Error: header does not match footer\n");
    }
	
    if (GET_SIZE(HDRP(bp)) > 8 && 
			GET_PREV_ALLOC(HDRP(NEXT_BLKP(bp))) != GET_ALLOC(HDRP(bp))) {
        printblock(bp);
        printf("Error: prev_alloc flag doesn't match\n");
    }
	
    if (!GET_ALLOC(HDRP(bp)) && GET_SIZE(HDRP(bp)) > 8 &&
			!GET_ALLOC(HDRP(NEXT_BLKP(bp)))) {

        printblock(bp);
        printf("Error: Continuous free blocks\n");
    }
}

/* 
 * checkheap - Maximal check of the heap for consistency 
 */
void mm_checkheap(int lineno) 
{
    char *bp = heap_listp;
	int count = 0;
    /* Step 1: check the correctness of the prologue block. */
    
    if ((GET_SIZE(HDRP(heap_listp)) != DSIZE) || !GET_ALLOC(HDRP(heap_listp)))
        printf("Bad prologue header, lineNo: %d\n", lineno);


    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
		if (!GET_ALLOC(HDRP(bp)))
			++count;
#ifdef DEBUG_VERBOSE	
        printblock(bp);
#endif
        checkblock(bp);
    }

    /* Step 3: check the correctness of the epilogue block. */
    
#ifdef DEBUG_VERBOSE	
	printblock(bp);
#endif
    if ((GET_SIZE(HDRP(bp)) != 0) || !(GET_ALLOC(HDRP(bp))))
        printf("Bad epilogue header, lineNo: %d\n", lineno);

	checkFreeList(count);

#ifdef DEBUG_VERBOSE	
    printf("Free List: \n");
    printf("============================================\n");
    printList();
    printf("END CHECK  =================================\n");
#endif
}

/* 
 * checkFreeList - Maximal check of the free list for consistency 
 */

static void checkFreeList(int count) {
	
    void *x, *succ, *prev;
    int i, _count = 0;
    
    for (i = 0; i < BUCKET_COUNT; i++) {
        for (x = (void *)(seg[i]); x != NIL && x != NULL; x = SUCC(x)) {
            _count++;
			prev = PREV(x);
			succ = SUCC(x);
			if (succ >= mem_heap_hi() || succ < mem_heap_lo()) {
        		printblock(x);
        		printf("Error: succ points putside the heap\n");
			}
			if (prev >= mem_heap_hi() || prev < mem_heap_lo()) {
        		printblock(x);
        		printf("Error: prev points putside the heap\n");
			}
			if (prev != NIL && SUCC(prev) != x) {
        		printblock(x);
        		printf("Error: prev pointer not match\n");
			}
			if (succ != NIL && PREV(succ) != x) {
        		printblock(x);
        		printf("Error: succ pointer not match\n");
			}
			if (i == 0 && GET_SIZE(HDRP(x)) > 72) {
        		printblock(x);
        		printf("Error: block falls to wrong list\n");
			}
			
        }
    }
	if (count != _count) {
        printf("Error: free block count not match. %d %d\n", count, _count);
	}
}
