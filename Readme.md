# Custom GC

---

### Idea:

Keep list of "free" blocks.

When memory is needed, remove block from "free" list.

If no more memory, request from OS.

Memory allocation algorithm: FIRST FIT

Working on a BEST FIT algorithm

---

### header structure

- size of chunk
- pointer to next free block of memory

```c
typedef struct header
{
    unsigned int size;
    struct header *next;
} header_t;
```
