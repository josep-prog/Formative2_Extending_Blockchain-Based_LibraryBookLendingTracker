#ifndef PENDING_H
#define PENDING_H

#include "blockchain.h"

/*
 * Signed lending blocks waiting for a miner. Borrow, return and overdue actions
 * land here first; nothing reaches the chain (or changes a balance) until mined.
 */
typedef struct {
    Block *blocks;   /* heap array, grows as needed */
    int count;
    int capacity;
} PendingPool;

void pending_init(PendingPool *pool);
void pending_free(PendingPool *pool);

/* Copies the block in; returns 0 if memory ran out. */
int pending_add(PendingPool *pool, const Block *block);

/* Removes the first n blocks (the ones just mined), keeping the order of the rest. */
void pending_remove_front(PendingPool *pool, int n);

#endif
