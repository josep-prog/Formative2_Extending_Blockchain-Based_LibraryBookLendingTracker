#include <stdlib.h>
#include <string.h>

#include "pending.h"

#define INITIAL_CAPACITY 8

void pending_init(PendingPool *pool)
{
    pool->blocks = NULL;
    pool->count = 0;
    pool->capacity = 0;
}

void pending_free(PendingPool *pool)
{
    free(pool->blocks);
    pending_init(pool);
}

int pending_add(PendingPool *pool, const Block *block)
{
    if (pool->count == pool->capacity) {
        int new_capacity = pool->capacity == 0 ? INITIAL_CAPACITY : pool->capacity * 2;
        Block *grown = realloc(pool->blocks, (size_t)new_capacity * sizeof(Block));
        if (grown == NULL) {
            return 0;
        }
        pool->blocks = grown;
        pool->capacity = new_capacity;
    }

    pool->blocks[pool->count++] = *block;
    return 1;
}

void pending_remove_front(PendingPool *pool, int n)
{
    if (n <= 0) {
        return;
    }
    if (n >= pool->count) {
        pool->count = 0;
        return;
    }
    memmove(pool->blocks, pool->blocks + n, (size_t)(pool->count - n) * sizeof(Block));
    pool->count -= n;
}
