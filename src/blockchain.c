#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "blockchain.h"
#include "crypto.h"


/* Signed part: the lending event and its reward; index and previous_hash are not signed. */
void create_transaction_data(const Block *block, unsigned char *data, size_t *data_len)
{
    *data_len = snprintf(
        (char *)data,
        TX_DATA_SIZE,
        "%ld|%s|%s|%s|%s|%s|%s|%d|%s",
        (long)block->timestamp,
        block->book_id,
        block->book_title,
        block->member_id,
        block->member_name,
        block->librarian_id,
        block->action,
        block->token_reward,
        block->tx_id
    );
}

/* Header = all hashed fields except the nonce. */
static size_t build_hash_header(const Block *block, unsigned char *buf)
{
    size_t len = snprintf((char *)buf, 128, "%d|%s|%d|", block->index, block->previous_hash,
                          block->difficulty);
    size_t data_len;

    create_transaction_data(block, buf + len, &data_len);
    len += data_len;

    memcpy(buf + len, &block->signature_length, sizeof(block->signature_length));
    len += sizeof(block->signature_length);

    memcpy(buf + len, block->signature, block->signature_length);
    len += block->signature_length;
    return len;
}

#define HASH_BUF_SIZE (128 + TX_DATA_SIZE + sizeof(unsigned int) + 72 + 32)

static void hash_with_nonce(unsigned char *buf, size_t header_len, unsigned long nonce, char out[65])
{
    size_t len = header_len + snprintf((char *)buf + header_len, 32, "|%lu", nonce);
    sha256_hex(buf, len, out);
}

void calculate_hash(Block *block)
{
    unsigned char buf[HASH_BUF_SIZE];
    size_t header_len = build_hash_header(block, buf);
    hash_with_nonce(buf, header_len, block->nonce, block->hash);
}

int hash_meets_difficulty(const char *hash, int difficulty)
{
    for (int i = 0; i < difficulty; i++) {
        if (hash[i] != '0') {
            return 0;
        }
    }
    return 1;
}

int mine_attempts(Block *block, unsigned long max_attempts, unsigned long *attempts_used)
{
    unsigned char buf[HASH_BUF_SIZE];
    size_t header_len = build_hash_header(block, buf);
    char hash[65];

    *attempts_used = 0;
    while (*attempts_used < max_attempts) {
        hash_with_nonce(buf, header_len, block->nonce, hash);
        (*attempts_used)++;

        if (hash_meets_difficulty(hash, block->difficulty)) {
            strcpy(block->hash, hash);
            return 1;
        }
        block->nonce++;
    }
    return 0;
}

void create_genesis_block(Block *block, int difficulty)
{
    unsigned long attempts;

    memset(block, 0, sizeof(Block));

    block->index = 0;
    block->timestamp = time(NULL);
    strcpy(block->action, "GENESIS");

    memset(block->previous_hash, '0', 64);
    block->previous_hash[64] = '\0';

    block->difficulty = difficulty;
    mine_attempts(block, (unsigned long)-1, &attempts);
}

/* The same input always gives the same id. */
void compute_reward_tx_id(const Block *block, char out_hex[65])
{
    char data[160];
    int len = snprintf(data, sizeof(data), "REWARD|%ld|%s|%s|%s|%d",
                       (long)block->timestamp, block->book_id, block->member_id,
                       block->librarian_id, block->token_reward);
    sha256_hex(data, (size_t)len, out_hex);
}

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
)
{
    memset(block, 0, sizeof(Block));

    block->index = -1;              /* not on the chain yet */
    block->timestamp = time(NULL);

    strcpy(block->book_id, book_id);
    strcpy(block->book_title, book_title);
    strcpy(block->member_id, member_id);
    strcpy(block->member_name, member_name);
    strcpy(block->librarian_id, librarian_id);
    strcpy(block->action, action);

    block->token_reward = token_reward;
    if (token_reward > 0) {
        compute_reward_tx_id(block, block->tx_id);
    }

    unsigned char data[TX_DATA_SIZE];
    size_t data_len;
    size_t signature_len = sizeof(block->signature);

    create_transaction_data(block, data, &data_len);

    if (!sign_data(private_key, data, data_len, block->signature, &signature_len)) {
        return 0;
    }
    block->signature_length = (unsigned int)signature_len;
    return 1;
}

void link_block(Block *block, const Block *previous_block, int difficulty)
{
    block->index = previous_block->index + 1;
    strcpy(block->previous_hash, previous_block->hash);
    block->difficulty = difficulty;
    block->nonce = 0;
    block->hash[0] = '\0';
}

static int is_lending_action(const char *action)
{
    return strcmp(action, "BORROWED") == 0 ||
           strcmp(action, "RETURNED") == 0 ||
           strcmp(action, "OVERDUE") == 0;
}

/* Only RETURNED earns tokens, and its tx_id must match the reward. */
static const char *check_reward(const Block *block)
{
    if (strcmp(block->action, "RETURNED") == 0) {
        if (block->token_reward != REWARD_ON_TIME && block->token_reward != REWARD_LATE) {
            return "a RETURNED block must carry a reward of 10 or 5 coins";
        }
        char expected[65];
        compute_reward_tx_id(block, expected);
        if (strcmp(block->tx_id, expected) != 0) {
            return "reward transaction ID does not match the block";
        }
    } else if (block->token_reward != REWARD_NONE || block->tx_id[0] != '\0') {
        return "only RETURNED blocks may carry a token reward";
    }
    return NULL;
}

int verify_lending_block(const Block *block, EVP_PKEY *public_key, const char **reason)
{
    const char *why = NULL;

    if (!is_lending_action(block->action)) {
        why = "unknown action";
    } else if ((why = check_reward(block)) == NULL) {
        unsigned char data[TX_DATA_SIZE];
        size_t data_len;
        create_transaction_data(block, data, &data_len);

        if (!verify_signature(public_key, data, data_len,
                              block->signature, block->signature_length)) {
            why = "digital signature is invalid";
        }
    }

    if (reason != NULL) *reason = why;
    return why == NULL;
}

static int invalid(int i, const char *why, int *bad_block, const char **reason)
{
    if (bad_block != NULL) *bad_block = i;
    if (reason != NULL) *reason = why;
    return 0;
}

int validate_chain(Block blockchain[], int count, EVP_PKEY *public_key,
                   int *bad_block, const char **reason)
{
    if (count < 1) {
        return invalid(0, "the chain has no genesis block", bad_block, reason);
    }

    for (int i = 0; i < count; i++) {
        Block *block = &blockchain[i];

        if (block->index != i) {
            return invalid(i, "block index is out of order", bad_block, reason);
        }

        Block copy = *block;
        calculate_hash(&copy);
        if (strcmp(block->hash, copy.hash) != 0) {
            return invalid(i, "stored hash does not match the block's contents", bad_block, reason);
        }

        if (block->difficulty < MIN_DIFFICULTY || block->difficulty > MAX_DIFFICULTY ||
            !hash_meets_difficulty(block->hash, block->difficulty)) {
            return invalid(i, "hash does not meet the proof-of-work difficulty", bad_block, reason);
        }

        if (i == 0) {
            if (strspn(block->previous_hash, "0") != 64 || strcmp(block->action, "GENESIS") != 0) {
                return invalid(i, "genesis block is not a GENESIS block with 64 zeros as previous_hash",
                               bad_block, reason);
            }
            continue;
        }

        if (strcmp(block->previous_hash, blockchain[i - 1].hash) != 0) {
            return invalid(i, "previous_hash does not match the previous block's hash", bad_block, reason);
        }

        const char *why;
        if (!verify_lending_block(block, public_key, &why)) {
            return invalid(i, why, bad_block, reason);
        }

        /* A reward transaction may appear only once, so a signed block cannot be replayed. */
        if (block->tx_id[0] != '\0') {
            for (int j = 1; j < i; j++) {
                if (strcmp(blockchain[j].tx_id, block->tx_id) == 0) {
                    return invalid(i, "reward transaction ID was already used by an earlier block",
                                   bad_block, reason);
                }
            }
        }
    }

    return 1;
}

/* One block per line, fields separated by '|'. */
int save_chain(const char *filename, Block blockchain[], int count)
{
    char temp_name[256];
    snprintf(temp_name, sizeof(temp_name), "%s.tmp", filename);

    FILE *file = fopen(temp_name, "w");
    if (file == NULL) {
        return 0;
    }

    for (int i = 0; i < count; i++) {
        Block *b = &blockchain[i];

        fprintf(file, "%d|%ld|%s|%s|%s|%s|%s|%s|%d|%s|%s|",
                b->index, (long)b->timestamp, b->book_id, b->book_title,
                b->member_id, b->member_name, b->librarian_id, b->action,
                b->token_reward, b->tx_id, b->previous_hash);

        for (unsigned int j = 0; j < b->signature_length; j++) {
            fprintf(file, "%02x", b->signature[j]);
        }

        fprintf(file, "|%d|%lu|%s\n", b->difficulty, b->nonce, b->hash);
    }

    if (fclose(file) != 0) {
        return 0;
    }

    /* Write a temp file, then rename, so a crash cannot break the chain file. */
    return rename(temp_name, filename) == 0;
}

static char *next_field(char **cursor)
{
    char *start = *cursor;
    if (start == NULL) {
        return NULL;
    }

    char *bar = strchr(start, '|');
    if (bar != NULL) {
        *bar = '\0';
        *cursor = bar + 1;
    } else {
        *cursor = NULL;
    }
    return start;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Returns 1 if the line is valid. */
static int parse_block_line(char *line, Block *block)
{
    char *cursor = line;
    char *index_text      = next_field(&cursor);
    char *time_text       = next_field(&cursor);
    char *book_id         = next_field(&cursor);
    char *book_title      = next_field(&cursor);
    char *member_id       = next_field(&cursor);
    char *member_name     = next_field(&cursor);
    char *librarian_id    = next_field(&cursor);
    char *action          = next_field(&cursor);
    char *reward_text     = next_field(&cursor);
    char *tx_id           = next_field(&cursor);
    char *previous_hash   = next_field(&cursor);
    char *signature_text  = next_field(&cursor);
    char *difficulty_text = next_field(&cursor);
    char *nonce_text      = next_field(&cursor);
    char *hash            = next_field(&cursor);

    if (hash == NULL || cursor != NULL) {
        return 0;
    }

    if (strlen(book_id) >= sizeof(block->book_id) ||
        strlen(book_title) >= sizeof(block->book_title) ||
        strlen(member_id) >= sizeof(block->member_id) ||
        strlen(member_name) >= sizeof(block->member_name) ||
        strlen(librarian_id) >= sizeof(block->librarian_id) ||
        strlen(action) >= sizeof(block->action) ||
        (strlen(tx_id) != 0 && strlen(tx_id) != 64) ||
        strlen(previous_hash) != 64 || strlen(hash) != 64) {
        return 0;
    }

    size_t hex_len = strlen(signature_text);
    if (hex_len % 2 != 0 || hex_len / 2 > sizeof(block->signature)) {
        return 0;
    }

    memset(block, 0, sizeof(Block));
    block->index = atoi(index_text);
    block->timestamp = (time_t)atol(time_text);
    strcpy(block->book_id, book_id);
    strcpy(block->book_title, book_title);
    strcpy(block->member_id, member_id);
    strcpy(block->member_name, member_name);
    strcpy(block->librarian_id, librarian_id);
    strcpy(block->action, action);
    block->token_reward = atoi(reward_text);
    strcpy(block->tx_id, tx_id);
    strcpy(block->previous_hash, previous_hash);
    block->difficulty = atoi(difficulty_text);
    block->nonce = strtoul(nonce_text, NULL, 10);
    strcpy(block->hash, hash);

    for (size_t i = 0; i < hex_len / 2; i++) {
        int high = hex_value(signature_text[i * 2]);
        int low  = hex_value(signature_text[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return 0;
        }
        block->signature[i] = (unsigned char)(high * 16 + low);
    }
    block->signature_length = (unsigned int)(hex_len / 2);

    return 1;
}

int load_chain(const char *filename, Block blockchain[])
{
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        return -1;
    }

    char line[1024];
    int count = 0;

    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') {
            continue;
        }

        if (count >= MAX_BLOCKS || !parse_block_line(line, &blockchain[count])) {
            fclose(file);
            return 0;
        }
        count++;
    }

    fclose(file);
    return count;
}

/* History = chain, then pending pool; position 0 is genesis. */
static const Block *history_at(const Block chain[], int count, const Block pending[], int i)
{
    return i < count ? &chain[i] : &pending[i - count];
}

/* Newest block for this book, or NULL. */
const Block *find_latest_record(const Block chain[], int count,
                                const Block pending[], int pending_count, const char *book_id)
{
    for (int i = count + pending_count - 1; i > 0; i--) {
        const Block *block = history_at(chain, count, pending, i);
        if (strcmp(block->book_id, book_id) == 0) {
            return block;
        }
    }
    return NULL;
}

/* BORROWED block of the current loan, or NULL. */
const Block *find_active_borrow(const Block chain[], int count,
                                const Block pending[], int pending_count, const char *book_id)
{
    for (int i = count + pending_count - 1; i > 0; i--) {
        const Block *block = history_at(chain, count, pending, i);
        if (strcmp(block->book_id, book_id) != 0) {
            continue;
        }
        if (strcmp(block->action, "BORROWED") == 0) {
            return block;
        }
        if (strcmp(block->action, "RETURNED") == 0) {
            return NULL;
        }
        /* OVERDUE: still on loan, keep looking. */
    }
    return NULL;
}

void print_block(const Block *block, EVP_PKEY *public_key)
{
    printf("\n--------------------------------------------------\n");
    printf("Block #%d  [%s]\n", block->index, block->action);

    if (block->index != 0) {
        printf("  Book    : %s (%s)\n", block->book_title, block->book_id);
        printf("  Member  : %s (%s)\n", block->member_name, block->member_id);
        printf("  By      : %s\n", block->librarian_id);
        printf("  Reward  : %d coin%s\n", block->token_reward, block->token_reward == 1 ? "" : "s");
        printf("  TX ID   : %s\n", block->tx_id[0] != '\0' ? block->tx_id : "(no transaction)");
    }

    printf("  Time    : %s", ctime(&block->timestamp));
    if (block->index >= 0) {
        printf("  PoW     : difficulty %d, nonce %lu\n", block->difficulty, block->nonce);
        printf("  Hash    : %s\n", block->hash);
        printf("  Prev    : %s\n", block->previous_hash);
    }

    if (block->index == 0) {
        printf("  Signature: n/a (genesis block)\n");
        return;
    }

    unsigned char data[TX_DATA_SIZE];
    size_t data_len;
    create_transaction_data(block, data, &data_len);

    int valid = verify_signature(public_key, data, data_len,
                                  block->signature, block->signature_length);

    printf("  Signature: %s (%u bytes)\n",
           valid ? "VALID" : "INVALID", block->signature_length);
}
