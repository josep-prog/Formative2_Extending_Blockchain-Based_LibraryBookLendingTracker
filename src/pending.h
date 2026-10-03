#ifndef PENDING_H
#define PENDING_H

#include "blockchain.h"

/* Signed blocks waiting for a miner; nothing changes until they are mined. */
typedef struct {
    Block *blocks;   /* heap array */
    int count;
    int capacity;
} PendingPool;

void pending_init(PendingPool *pool);
void pending_free(PendingPool *pool);

/* Returns 0 if memory ran out. */
int pending_add(PendingPool *pool, const Block *block);

/* Removes the first n blocks. */
void pending_remove_front(PendingPool *pool, int n);

#endif
