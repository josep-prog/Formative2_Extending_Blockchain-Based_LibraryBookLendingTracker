#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include <time.h>
#include <stddef.h>
#include <openssl/evp.h>

#define MAX_BLOCKS   1000
#define TX_DATA_SIZE 512

/* Proof-of-work difficulty = number of leading '0' hex characters in a block hash. */
#define MIN_DIFFICULTY     1
#define MAX_DIFFICULTY     4
#define DEFAULT_DIFFICULTY 2

/* Token reward (in whole coins) written into a lending block. */
#define REWARD_ON_TIME 10
#define REWARD_LATE     5
#define REWARD_NONE     0

typedef struct {
    int index;
    time_t timestamp;

    char book_id[20];
    char book_title[80];

    char member_id[20];
    char member_name[50];

    char librarian_id[20]; /* who recorded the action */

    char action[10];       /* "GENESIS", "BORROWED", "RETURNED" or "OVERDUE" */

    int token_reward;      /* coins earned: 10 on-time return, 5 late return, 0 otherwise */
    char tx_id[65];        /* SHA-256 of the reward transaction, "" when there is none */

    char previous_hash[65]; /* 64 hex chars + '\0' */

    unsigned char signature[72];
    unsigned int signature_length;

    int difficulty;         /* leading zeros this block was mined at */
    unsigned long nonce;    /* proof-of-work counter found by the miner */

    char hash[65];
} Block;


/* Genesis is mined at the given difficulty so every block on the chain carries proof of work. */
void create_genesis_block(Block *block, int difficulty);

/*
 * Builds a signed but UNCONFIRMED lending block for the pending pool.
 * index, previous_hash, nonce and hash are filled in later by the miner.
 * Returns 0 if signing failed.
 */
int create_lending_block(
    Block *block,
    const char *action,
    const char *book_id,
    const char *book_title,
    const char *member_id,
    const char *member_name,
    const char *librarian_id,
    int token_reward,
    EVP_PKEY *private_key
);

/* SHA-256 id of the reward transaction a RETURNED block carries. */
void compute_reward_tx_id(const Block *block, char out_hex[65]);

/* Links a pending block to the tip of the chain, ready to be mined. */
void link_block(Block *block, const Block *previous_block, int difficulty);

void calculate_hash(Block *block);
int hash_meets_difficulty(const char *hash, int difficulty);

/*
 * Tries nonces starting at block->nonce, at most max_attempts of them.
 * Returns 1 and leaves the valid nonce/hash in the block when the target is hit;
 * otherwise block->nonce is left at the next untried value. *attempts_used counts the tries.
 */
int mine_attempts(Block *block, unsigned long max_attempts, unsigned long *attempts_used);

/* Checks a pending block's signature and reward fields before it is mined. */
int verify_lending_block(const Block *block, EVP_PKEY *public_key, const char **reason);

/* On failure, bad_block and reason (may be NULL) say what broke. */
int validate_chain(Block blockchain[], int count, EVP_PKEY *public_key,
                   int *bad_block, const char **reason);

/* load_chain returns the block count, 0 if unreadable, -1 if missing. */
int save_chain(const char *filename, Block blockchain[], int count);
int load_chain(const char *filename, Block blockchain[]);

/*
 * Book status across the confirmed chain AND the pending pool (pending is newer).
 * Both return a pointer to the matching block, or NULL.
 */
const Block *find_latest_record(const Block chain[], int count,
                                const Block pending[], int pending_count, const char *book_id);
const Block *find_active_borrow(const Block chain[], int count,
                                const Block pending[], int pending_count, const char *book_id);

/* Prints one block, checking its signature with the public key. */
void print_block(const Block *block, EVP_PKEY *public_key);

/* The fields covered by the librarian's signature. */
void create_transaction_data(const Block *block, unsigned char *data, size_t *data_len);

#endif
