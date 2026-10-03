#ifndef MINING_H
#define MINING_H

#include <openssl/evp.h>

#include "blockchain.h"
#include "pending.h"
#include "ledger.h"

#define BLOCK_REWARD            50   /* coins per mined block */
#define POOL_FEE_PERCENT         2   /* pool operator's cut */
#define POOL_OPERATOR_ACCOUNT   "POOL-OP"
#define MIN_POOL_MINERS          2
#define MAX_POOL_MINERS          8
#define DEFAULT_POOL_MINERS      4
#define POOL_MIN_HASH_RATE      50   /* attempts per round */
#define POOL_MAX_HASH_RATE     500
#define CLOUD_HASHES_PER_ROUND 600   /* hashes a rental buys each round */
#define CLOUD_MAINTENANCE_PCT   10   /* provider's share of each reward */
#define CLOUD_PROVIDER_ACCOUNT "CLOUD-OP"   /* receives the rental fees */
#define DEFAULT_RENTAL_FEE      25   /* coins per round */
#define MAX_RENTAL_ROUNDS        5

/* What the miners need to confirm pending blocks. */
typedef struct {
    Block *chain;            /* heap array of MAX_BLOCKS */
    int count;
    PendingPool pending;
    Ledger ledger;
    EVP_PKEY *public_key;
    int difficulty;
    const char *chain_file;
} LibraryState;

/* Pays the member reward of a confirmed block. */
int apply_block_reward(Ledger *ledger, const Block *block);

void print_pending_pool(const LibraryState *state);

/* Each returns the number of blocks it confirmed. */
int mine_solo(LibraryState *state, const char *miner_id);
int mine_pool(LibraryState *state, int miner_count);
int mine_cloud(LibraryState *state, const char *renter_id, int rounds, long rental_fee_coins);

/* Mines a test block at difficulty 1..4 to compare effort. */
void mining_benchmark(void);

#endif
