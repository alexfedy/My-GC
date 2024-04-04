#include <cstddef>
#include <cstdio>
#include <xkeycheck.h>

typedef struct header
{
    unsigned int size;
    struct header *next;
} header_t;

static header_t base;
static header_t *freep = &base;
static header_t *usedp;
uintptr_t stack_bottom;

// This function looks through the 'free' list and finds a place to put the block pointer

static void
add_to_free_list(header_t *bp)
{
    header_t *p;

    for (p = freep; !(bp > p && bp < p->next); p = p->next)
        if (p >= p->next && (bp > p || bp < p->next))
            break;

    if (bp + bp->size == p->next)
    {
        bp->size += p->next->size;
        bp->next = p->next->next;
    }
    else
        bp->next = p->next;

    if (p + p->size == bp)
    {
        p->size += bp->size;
        p->next = bp->next;
    }
    else
        p->next = bp;

    freep = p;
}

// defines the minimum allocation size
#define MIN_ALLOC_SIZE 4096

// this function requests memory from the OS
static header_t *
request_memory(size_t num_units)
{
    void *vp;
    header_t *up;

    if (num_units > MIN_ALLOC_SIZE)
        num_units = MIN_ALLOC_SIZE / sizeof(header_t);

    if ((vp = sbrk(num_units * sizeof(header_t))) == (void *)-1)
        return NULL;

    up = (header_t *)vp;
    up->size = num_units;
    add_to_free_list(up);
    return freep;
}

// Puts a chuck from the free list into the used list
// currently uses first fit algorithm
// TODO: implement 'best fit' algorithm

void *
GC_malloc(size_t alloc_size)
{
    size_t num_units;
    header_t *p, *prevp;

    num_units = (alloc_size + sizeof(header_t) - 1) / sizeof(header_t) + 1;
    prevp = freep;

    for (p = prevp->next;; prevp = p, p = p->next)
    {
        if (p->size >= num_units)
        {
            if (p->size == num_units)
                prevp->next = p->next;
            else
            {
                p->size -= num_units;
                p += p->size;
                p->size = num_units;
            }

            freep = prevp;

            if (usedp == NULL)
                usedp = p->next = p;
            else
            {
                p->next = usedp->next;
                usedp->next = p;
            }

            return (void *)(p + 1);
        }
        if (p == freep)
        {
            // Out of memory
            p = request_memory(num_units);
            if (p == NULL)
                return NULL;
        }
    }
}

#define UNTAG(p) (((uintptr_t)(p)) & 0xfffffffc)

// this function scans memory and marks the blocks in the used list
static void
scan_region(uintptr_t *sp, uintptr_t *end)
{
    header_t *bp;

    for (; sp < end; sp++)
    {
        uintptr_t v = *sp;
        bp = usedp;
        do
        {
            if (bp + 1 <= v &&
                bp + 1 + bp->size > v)
            {
                bp->next = ((uintptr_t)bp->next) | 1;
                break;
            }
        } while ((bp = UNTAG(bp->next)) != usedp);
    }
}

// scan marked blocks in heap for reference to unmarked blocks
static void
scan_heap(void)
{
    uintptr_t *vp;
    header_t *bp, *up;

    for (bp = UNTAG(usedp->next); bp != usedp; bp = UNTAG(bp->next))
    {
        if (!((uintptr_t)bp->next & 1))
            continue;
        for (vp = (uintptr_t *)(bp + 1);
             vp < (bp + bp->size + 1);
             vp++)
        {
            uintptr_t v = *vp;
            up = UNTAG(bp->next);
            do
            {
                if (up != bp &&
                    up + 1 <= v &&
                    up + 1 + up->size > v)
                {
                    up->next = ((uintptr_t)up->next) | 1;
                    break;
                }
            } while ((up = UNTAG(up->next)) != bp);
        }
    }
}

// find bottom of stack using Linux hack (stores bottom of stack in file)

void GC_init(void)
{
    static int initted;
    FILE *statfp;

    if (initted)
        return;

    initted = 1;

    statfp = fopen("/proc/self/stat", "r");
    assert(statfp != NULL);
    fscanf(statfp,
           "%*d %*s %*c %*d %*d %*d %*d %*d %*u "
           "%*lu %*lu %*lu %*lu %*lu %*lu %*ld %*ld "
           "%*ld %*ld %*ld %*ld %*llu %*lu %*ld "
           "%*lu %*lu %*lu %lu",
           &stack_bottom);
    fclose(statfp);

    usedp = NULL;
    base.next = freep = &base;
    base.size = 0;
}

// function that recognizes memory in use and free memory

void GC_collect(void)
{
    header_t *p, *prevp, *tp;
    uintptr_t stack_top;
    extern char end, etext;

    if (usedp == NULL)
        return;

    // scan the BSS region
    scan_region(&etext, &end);

    // scan the stack using assembly
    asm volatile("movl %%ebp, %0" : "=r"(stack_top));
    scan_region(stack_top, stack_bottom);

    // scan the heap
    scan_heap();

    // collect memory
    for (prevp = usedp, p = UNTAG(usedp->next);; prevp = p, p = UNTAG(p->next))
    {
    next_chunk:
        if (!((unsigned int)p->next & 1))
        {
            // chunck was not marked, so set it free
            tp = p;
            p = UNTAG(p->next);
            add_to_free_list(tp);

            if (usedp == tp)
            {
                usedp = NULL;
                break;
            }

            prevp->next = (uintptr_t)p | ((uintptr_t)prevp->next & 1);
            goto next_chunk;
        }
        p->next = ((uintptr_t)p->next) & ~1;
        if (p == usedp)
            break;
    }
}