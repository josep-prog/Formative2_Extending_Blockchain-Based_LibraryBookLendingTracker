#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include <time.h>
#include <stddef.h>
#include <openssl/evp.h>

#define MAX_BLOCKS   1000
#define TX_DATA_SIZE 512

/* Difficulty = number of leading '0' in the block hash. */
#define MIN_DIFFICULTY     1
#define MAX_DIFFICULTY     4
#define DEFAULT_DIFFICULTY 2

/* Token rewards, in whole coins. */
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

    char librarian_id[20];

    char action[10];       /* "GENESIS", "BORROWED", "RETURNED" or "OVERDUE" */

    int token_reward;      /* 10 on time, 5 late, 0 otherwise */
    char tx_id[65];        /* "" when there is no reward */

    char previous_hash[65]; /* 64 hex chars + '\0' */

    unsigned char signature[72];
    unsigned int signature_length;

    int difficulty;         /* leading zeros */
    unsigned long nonce;    /* proof-of-work counter */

    char hash[65];
} Block;


/* Genesis is mined too. */
void create_genesis_block(Block *block, int difficulty);

/* Builds a signed pending block; returns 0 if signing failed. */
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

/* Id of the reward transaction. */
void compute_reward_tx_id(const Block *block, char out_hex[65]);

/* Links a pending block to the chain tip. */
void link_block(Block *block, const Block *previous_block, int difficulty);

void calculate_hash(Block *block);
int hash_meets_difficulty(const char *hash, int difficulty);

/* Tries up to max_attempts nonces; returns 1 when a valid hash is found. */
int mine_attempts(Block *block, unsigned long max_attempts, unsigned long *attempts_used);

/* Checks signature and reward before mining. */
int verify_lending_block(const Block *block, EVP_PKEY *public_key, const char **reason);

/* On failure, bad_block and reason say what broke. */
int validate_chain(Block blockchain[], int count, EVP_PKEY *public_key,
                   int *bad_block, const char **reason);

/* load_chain returns the block count, 0 if unreadable, -1 if missing. */
int save_chain(const char *filename, Block blockchain[], int count);
int load_chain(const char *filename, Block blockchain[]);

/* Book status over chain and pending pool; both return a block or NULL. */
const Block *find_latest_record(const Block chain[], int count,
                                const Block pending[], int pending_count, const char *book_id);
const Block *find_active_borrow(const Block chain[], int count,
                                const Block pending[], int pending_count, const char *book_id);

/* Prints one block and checks its signature. */
void print_block(const Block *block, EVP_PKEY *public_key);

/* The signed fields. */
void create_transaction_data(const Block *block, unsigned char *data, size_t *data_len);

#endif
