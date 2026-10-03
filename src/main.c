#define _POSIX_C_SOURCE 200809L   /* termios, isatty, getenv */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "registry.h"
#include "blockchain.h"
#include "crypto.h"
#include "pending.h"
#include "ledger.h"
#include "mining.h"

#define BOOKS_FILE      "data/books.txt"
#define MEMBERS_FILE    "data/members.txt"
#define LIBRARIANS_FILE "data/librarians.txt"
#define CHAIN_FILE      "data/chain.txt"
#define KEY_FILE        "data/key.pem"
#define PUBLIC_KEY_FILE "data/pub.pem"

#define MAX_LOGIN_ATTEMPTS          3
#define DEFAULT_LOAN_PERIOD_SECONDS 120

/* Reads one line without its newline; returns 0 when input is closed. */
static int read_line(char *buf, size_t size)
{
    if (fgets(buf, (int)size, stdin) == NULL) {
        return 0;
    }

    size_t len = strcspn(buf, "\r\n");
    if (buf[len] == '\0' && len == size - 1) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
    }
    buf[len] = '\0';
    return 1;
}

static int prompt_line(const char *prompt, char *buf, size_t size)
{
    printf("%s", prompt);
    fflush(stdout);
    return read_line(buf, size);
}

/* Like prompt_line, but hides what is typed when reading from a terminal. */
static int prompt_secret(const char *prompt, char *buf, size_t size)
{
    struct termios old_attr, new_attr;
    int hide = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &old_attr) == 0;

    if (hide) {
        new_attr = old_attr;
        new_attr.c_lflag &= ~(tcflag_t)ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &new_attr);
    }

    int ok = prompt_line(prompt, buf, size);

    if (hide) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_attr);
        printf("\n");
    }
    return ok;
}

/* Returns the librarian's index, or -1 after too many failed attempts. */
static int login(Librarian librarians[], int count)
{
    char id[64], pin[64];

    for (int attempt = 1; attempt <= MAX_LOGIN_ATTEMPTS; attempt++) {
        if (!prompt_line("Librarian ID: ", id, sizeof(id)) ||
            !prompt_secret("PIN: ", pin, sizeof(pin))) {
            return -1;
        }

        int index = find_librarian(librarians, count, id);
        int ok = index != -1 && verify_pin(id, pin, librarians[index].pin_hash);
        memset(pin, 0, sizeof(pin));

        if (ok) {
            return index;
        }
        printf("ERROR: Invalid librarian ID or PIN (%d attempt(s) left).\n",
               MAX_LOGIN_ATTEMPTS - attempt);
    }
    return -1;
}

/* Passphrase from LIBRARY_KEY_PASSPHRASE, or asked for. */
static int get_passphrase(char *buf, size_t size)
{
    const char *env = getenv("LIBRARY_KEY_PASSPHRASE");
    if (env != NULL) {
        snprintf(buf, size, "%s", env);
    } else if (!prompt_secret("Key passphrase: ", buf, size)) {
        return 0;
    }

    if (strlen(buf) < MIN_PASSPHRASE_LENGTH) {
        printf("ERROR: The key passphrase must be at least %d characters.\n", MIN_PASSPHRASE_LENGTH);
        return 0;
    }
    return 1;
}

static EVP_PKEY *load_or_create_key(const char *passphrase)
{
    EVP_PKEY *key_pair;
    int status = load_key(KEY_FILE, passphrase, &key_pair);

    if (status == KEY_BAD) {
        printf("ERROR: Could not open %s - wrong passphrase or damaged key file.\n", KEY_FILE);
        return NULL;
    }

    if (status == KEY_LOADED) {
        printf("Digital signing key loaded from %s.\n", KEY_FILE);
        return key_pair;
    }

    if (status == KEY_MISSING) {
        key_pair = generate_key_pair();
        if (key_pair == NULL) {
            printf("ERROR: Could not generate key pair.\n");
            return NULL;
        }
    }

    /* New key, or an old unencrypted one: save it encrypted. */
    if (save_key(key_pair, KEY_FILE, passphrase)) {
        printf("Digital signing key %s and saved encrypted to %s.\n",
               status == KEY_MISSING ? "generated" : "loaded", KEY_FILE);
    } else {
        printf("WARNING: could not save the signing key to %s.\n", KEY_FILE);
    }
    return key_pair;
}

/* Loads data/pub.pem, writing it from the signing key the first time. */
static EVP_PKEY *load_or_create_public_key(EVP_PKEY *key_pair)
{
    EVP_PKEY *public_key = load_public_key(PUBLIC_KEY_FILE);

    if (public_key == NULL) {
        if (!save_public_key(key_pair, PUBLIC_KEY_FILE) ||
            (public_key = load_public_key(PUBLIC_KEY_FILE)) == NULL) {
            printf("ERROR: Could not write the public key to %s.\n", PUBLIC_KEY_FILE);
            return NULL;
        }
        printf("Public verification key saved to %s.\n", PUBLIC_KEY_FILE);
        return public_key;
    }

    if (EVP_PKEY_eq(public_key, key_pair) != 1) {
        printf("ERROR: %s does not belong to the signing key in %s.\n", PUBLIC_KEY_FILE, KEY_FILE);
        EVP_PKEY_free(public_key);
        return NULL;
    }
    return public_key;
}

static long loan_period_seconds(void)
{
    const char *env = getenv("LOAN_PERIOD_SECONDS");   /* override for demos */
    if (env != NULL) {
        char *end;
        long seconds = strtol(env, &end, 10);
        if (*end == '\0' && seconds >= 0) {
            return seconds;
        }
        printf("WARNING: ignoring invalid LOAN_PERIOD_SECONDS '%s'.\n", env);
    }
    return DEFAULT_LOAN_PERIOD_SECONDS;
}

/* Returns 1 if the chain is valid. */
static int report_validation(Block blockchain[], int count, EVP_PKEY *public_key)
{
    int bad_block;
    const char *reason;

    if (validate_chain(blockchain, count, public_key, &bad_block, &reason)) {
        printf("Blockchain is VALID - hashes, links, proof of work and signatures all check out.\n");
        return 1;
    }
    printf("Blockchain is INVALID - tampering detected!\n");
    printf("  Block #%d: %s.\n", bad_block, reason);
    return 0;
}

/* Only a valid chain is written to disk, so the tamper demo can never end up in the file. */
static void save_or_warn(LibraryState *state)
{
    if (!validate_chain(state->chain, state->count, state->public_key, NULL, NULL)) {
        printf("WARNING: chain is invalid, so it was NOT saved to %s.\n", state->chain_file);
        return;
    }
    if (!save_chain(state->chain_file, state->chain, state->count)) {
        printf("WARNING: could not save the chain to %s.\n", state->chain_file);
    }
}

/* Whole number in [min, max]; returns 1 on success. */
static int parse_int(const char *text, int min, int max, int *out)
{
    char *end;
    long value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < min || value > max) {
        return 0;
    }
    *out = (int)value;
    return 1;
}

/* "12", "12.5" or "12.50" coins -> hundredths of a coin. Returns 1 on success. */
static int parse_coins(const char *text, long *out)
{
    long whole = 0, cents = 0;
    int digits = 0, decimals = 0;
    const char *p = text;

    for (; *p >= '0' && *p <= '9'; p++, digits++) {
        if (whole > 1000000) return 0;
        whole = whole * 10 + (*p - '0');
    }
    if (*p == '.') {
        for (p++; *p >= '0' && *p <= '9' && decimals < 2; p++, decimals++) {
            cents = cents * 10 + (*p - '0');
        }
        if (decimals == 1) cents *= 10;
    }
    if (*p != '\0' || (digits == 0 && decimals == 0)) {
        return 0;
    }
    *out = whole * COIN + cents;
    return *out > 0;
}

/*
 * Asks for a number in [min, max]; an empty answer picks default_value.
 * Returns 1 on success, 0 on bad input, -1 when input is closed.
 */
static int prompt_int(const char *prompt, int min, int max, int default_value, int *out)
{
    char input[64];
    if (!prompt_line(prompt, input, sizeof(input))) {
        return -1;
    }
    if (input[0] == '\0') {
        *out = default_value;
        return 1;
    }
    if (!parse_int(input, min, max, out)) {
        printf("ERROR: Enter a whole number from %d to %d.\n", min, max);
        return 0;
    }
    return 1;
}

/* Signs a lending event and parks it in the pending pool; the chain is untouched until mined. */
static int queue_block(LibraryState *state, const char *action, const Block *details,
                       int reward, const char *librarian_id, EVP_PKEY *key_pair)
{
    Block block;

    if (state->count + state->pending.count >= MAX_BLOCKS) {
        printf("ERROR: Blockchain is full.\n");
        return 0;
    }

    if (!create_lending_block(&block, action, details->book_id, details->book_title,
                              details->member_id, details->member_name,
                              librarian_id, reward, key_pair)) {
        printf("ERROR: Could not sign %s transaction.\n", action);
        return 0;
    }

    if (!pending_add(&state->pending, &block)) {
        printf("ERROR: out of memory - block not queued.\n");
        return 0;
    }

    if (reward > 0) {
        printf("Reward transaction created: %d coins to %s (TX %.16s...).\n",
               reward, block.member_id, block.tx_id);
    }
    printf("%s block is PENDING (%d in the pool) - mine the pool to confirm it.\n",
           action, state->pending.count);
    return 1;
}

static int parse_model(const char *text, LedgerModel *model)
{
    if (strcmp(text, "utxo") == 0 || strcmp(text, "UTXO") == 0 || strcmp(text, "1") == 0) {
        *model = MODEL_UTXO;
        return 1;
    }
    if (strcmp(text, "account") == 0 || strcmp(text, "ACCOUNT") == 0 || strcmp(text, "2") == 0) {
        *model = MODEL_ACCOUNT;
        return 1;
    }
    return 0;
}

/* Members start at 0 tokens; balances are rebuilt from rewards already confirmed on the chain. */
static int setup_ledger(LibraryState *state, Member members[], int member_count)
{
    int replayed = 0;

    for (int i = 0; i < member_count; i++) {
        if (!ledger_open_account(&state->ledger, members[i].member_id)) {
            return 0;
        }
    }
    if (!ledger_open_account(&state->ledger, FEE_ACCOUNT)) {
        return 0;
    }

    /* A chain that fails validation proves nothing, so none of its rewards are credited. */
    if (!validate_chain(state->chain, state->count, state->public_key, NULL, NULL)) {
        printf("WARNING: the chain is INVALID, so no rewards were credited from it. "
               "All balances start at 0.\n");
        printf("Transaction model: %s.\n", ledger_model_name(state->ledger.model));
        return 1;
    }

    for (int i = 1; i < state->count; i++) {
        if (state->chain[i].token_reward > 0 && apply_block_reward(&state->ledger, &state->chain[i])) {
            replayed++;
        }
    }

    printf("Transaction model: %s. %d confirmed reward%s replayed from the chain.\n",
           ledger_model_name(state->ledger.model), replayed, replayed == 1 ? "" : "s");
    return 1;
}

static void print_menu(const LibraryState *state)
{
    printf("\n LIBRARY MENU  [model: %s | difficulty: %d | pending: %d | chain: %d blocks]\n",
           ledger_model_name(state->ledger.model), state->difficulty,
           state->pending.count, state->count);
    printf(" -- Lending --\n");
    printf(" 1. Borrow a book\n");
    printf(" 2. Return a book\n");
    printf(" 3. Mark overdue loans\n");
    printf(" 4. View pending pool\n");
    printf(" -- Mining --\n");
    printf(" 5. Mine pending blocks: solo\n");
    printf(" 6. Mine pending blocks: pool\n");
    printf(" 7. Mine pending blocks: cloud rental\n");
    printf(" -- Tokens --\n");
    printf(" 8. View confirmed lending records\n");
    printf(" 9. View token balances%s\n", state->ledger.model == MODEL_UTXO ? " and UTXO set" : "");
    printf("10. Transfer tokens\n");
    printf("11. Replay last transfer (double-spend / nonce test)\n");
    printf("12. Transaction history for a member\n");
    printf(" -- Chain --\n");
    printf("13. Set mining difficulty (1-4)\n");
    printf("14. Proof-of-work difficulty benchmark\n");
    printf("15. Validate the blockchain\n");
    printf("16. Tamper-detection demo (ADMIN)\n");
    printf(" 0. Exit\n");
}

static void usage(const char *program)
{
    printf("Usage: %s [--model utxo|account] [--difficulty 1-4]\n", program);
    printf("       %s --verify\n", program);
    printf("       %s --hash-pin <librarian_id> <pin>\n", program);
}

int main(int argc, char *argv[])
{
    /* Helper for creating librarians.txt entries. */
    if (argc == 4 && strcmp(argv[1], "--hash-pin") == 0) {
        char hex[65];
        if (!hash_pin(argv[2], argv[3], hex)) {
            printf("ERROR: Could not hash PIN.\n");
            return 1;
        }
        printf("%s\n", hex);
        return 0;
    }

    /* Checks data/chain.txt with only the public key - no passphrase or login needed. */
    if (argc == 2 && strcmp(argv[1], "--verify") == 0) {
        EVP_PKEY *public_key = load_public_key(PUBLIC_KEY_FILE);
        if (public_key == NULL) {
            printf("ERROR: Could not read the public key from %s.\n", PUBLIC_KEY_FILE);
            return 1;
        }

        Block *blockchain = malloc(MAX_BLOCKS * sizeof(Block));
        if (blockchain == NULL) {
            EVP_PKEY_free(public_key);
            return 1;
        }
        int count = load_chain(CHAIN_FILE, blockchain);
        int valid = 0;

        if (count < 0) {
            printf("ERROR: %s does not exist.\n", CHAIN_FILE);
        } else if (count == 0) {
            printf("ERROR: '%s' exists but could not be read.\n", CHAIN_FILE);
        } else {
            printf("Checked %d blocks from %s with %s.\n", count, CHAIN_FILE, PUBLIC_KEY_FILE);
            valid = report_validation(blockchain, count, public_key);
        }

        free(blockchain);
        EVP_PKEY_free(public_key);
        return valid ? 0 : 1;
    }

    /* Session options: transaction model and mining difficulty. */
    int model_chosen = 0;
    LedgerModel model = MODEL_UTXO;
    int difficulty = DEFAULT_DIFFICULTY;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--model") == 0 && i + 1 < argc && parse_model(argv[i + 1], &model)) {
            model_chosen = 1;
            i++;
        } else if (strcmp(argv[i], "--difficulty") == 0 && i + 1 < argc &&
                   parse_int(argv[i + 1], MIN_DIFFICULTY, MAX_DIFFICULTY, &difficulty)) {
            i++;
        } else {
            printf("ERROR: unrecognised or invalid option '%s'.\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    Book books[MAX_BOOKS];
    Member members[MAX_MEMBERS];
    Librarian librarians[MAX_LIBRARIANS];

    int book_count = load_books(BOOKS_FILE, books);
    int member_count = load_members(MEMBERS_FILE, members);
    int librarian_count = load_librarians(LIBRARIANS_FILE, librarians);

    if (book_count == 0 || member_count == 0 || librarian_count == 0) {
        return 1;
    }

    printf("Loaded %d books, %d members and %d librarians.\n",
           book_count, member_count, librarian_count);

    char passphrase[128];
    if (!get_passphrase(passphrase, sizeof(passphrase))) {
        return 1;
    }
    EVP_PKEY *key_pair = load_or_create_key(passphrase);
    memset(passphrase, 0, sizeof(passphrase));
    if (key_pair == NULL) {
        return 1;
    }

    EVP_PKEY *public_key = load_or_create_public_key(key_pair);
    if (public_key == NULL) {
        EVP_PKEY_free(key_pair);
        return 1;
    }

    int exit_code = 1;
    LibraryState state;
    memset(&state, 0, sizeof(state));
    pending_init(&state.pending);
    ledger_init(&state.ledger, model);
    state.public_key = public_key;
    state.difficulty = difficulty;
    state.chain_file = CHAIN_FILE;
    state.chain = malloc(MAX_BLOCKS * sizeof(Block));

    if (state.chain == NULL) {
        printf("ERROR: out of memory.\n");
        goto cleanup;
    }

    state.count = load_chain(CHAIN_FILE, state.chain);

    if (state.count == 0) {
        printf("ERROR: '%s' exists but could not be read. Fix or delete it to continue.\n", CHAIN_FILE);
        printf("(A chain saved by the Formative 1 version has no token/PoW fields - delete it to start fresh.)\n");
        goto cleanup;
    } else if (state.count < 0) {
        create_genesis_block(&state.chain[0], state.difficulty);
        state.count = 1;
        save_or_warn(&state);
        printf("Started a new blockchain (genesis mined at difficulty %d).\n", state.difficulty);
    } else {
        printf("Loaded %d blocks from %s.\n", state.count, CHAIN_FILE);
        if (!validate_chain(state.chain, state.count, public_key, NULL, NULL)) {
            printf("WARNING: the saved blockchain is INVALID - it may have been tampered with!\n");
            report_validation(state.chain, state.count, public_key);
        }
    }

    int user_index = login(librarians, librarian_count);
    if (user_index == -1) {
        printf("Access denied.\n");
        goto cleanup;
    }
    Librarian *user = &librarians[user_index];
    int is_admin = strcmp(user->role, "ADMIN") == 0;
    printf("Welcome, %s (%s).\n", user->full_name, user->role);

    char input[64];

    /* The model is fixed for the whole session, so every token transaction uses the same rules. */
    while (!model_chosen) {
        printf("\nChoose the transaction model for this session:\n");
        printf("1. UTXO model\n2. Account-based model\n");
        if (!prompt_line("Model: ", input, sizeof(input))) {
            goto cleanup;
        }
        model_chosen = parse_model(input, &model);
        if (!model_chosen) printf("Invalid choice.\n");
    }
    state.ledger.model = model;

    if (!setup_ledger(&state, members, member_count) ||
        !ledger_open_account(&state.ledger, user->librarian_id)) {
        printf("ERROR: out of memory.\n");
        goto cleanup;
    }

    srand((unsigned)time(NULL));

    Transaction last_transfer;
    int have_last_transfer = 0;
    int choice = -1;

    do {
        print_menu(&state);

        if (!prompt_line("Choice: ", input, sizeof(input))) {
            printf("\nInput closed. Goodbye.\n");
            break;
        }

        if (!parse_int(input, 0, 16, &choice)) {
            choice = -1;
            printf("Invalid choice.\n");
            continue;
        }

        if (choice == 1) {
            char book_id[64], member_id[64];
            if (!prompt_line("Book ID  : ", book_id, sizeof(book_id)) ||
                !prompt_line("Member ID: ", member_id, sizeof(member_id))) {
                break;
            }

            int book_index = find_book(books, book_count, book_id);
            int member_index = find_member(members, member_count, member_id);

            if (book_index == -1 || member_index == -1) {
                printf("ERROR: Book or Member not found\n");
                continue;
            }

            if (find_active_borrow(state.chain, state.count, state.pending.blocks,
                                   state.pending.count, book_id) != NULL) {
                printf("ERROR: This book is already on loan (confirmed or pending).\n");
                continue;
            }

            Block details = {0};
            strcpy(details.book_id, books[book_index].book_id);
            strcpy(details.book_title, books[book_index].title);
            strcpy(details.member_id, members[member_index].member_id);
            strcpy(details.member_name, members[member_index].full_name);

            /* Borrowing earns nothing, so no transaction is created. */
            if (queue_block(&state, "BORROWED", &details, REWARD_NONE, user->librarian_id, key_pair)) {
                printf("Borrow of '%s' for %s recorded.\n", details.book_title, details.member_name);
            }

        } else if (choice == 2) {
            char book_id[64], member_id[64];
            if (!prompt_line("Book ID  : ", book_id, sizeof(book_id)) ||
                !prompt_line("Member ID: ", member_id, sizeof(member_id))) {
                break;
            }

            if (find_book(books, book_count, book_id) == -1 ||
                find_member(members, member_count, member_id) == -1) {
                printf("ERROR: Book or Member not found\n");
                continue;
            }

            const Block *loan = find_active_borrow(state.chain, state.count, state.pending.blocks,
                                                   state.pending.count, book_id);
            if (loan == NULL) {
                printf("ERROR: This book is not currently on loan.\n");
                continue;
            }

            /* Copy now: queue_block may grow the pending pool and move the block loan points to. */
            Block details = *loan;
            if (strcmp(details.member_id, member_id) != 0) {
                printf("ERROR: This book is on loan to %s (%s), not %s.\n",
                       details.member_name, details.member_id, member_id);
                continue;
            }

            /* Late = past the loan period, or already flagged OVERDUE. */
            const Block *latest = find_latest_record(state.chain, state.count, state.pending.blocks,
                                                     state.pending.count, book_id);
            long held = (long)(time(NULL) - details.timestamp);
            int late = held > loan_period_seconds() || strcmp(latest->action, "OVERDUE") == 0;
            int reward = late ? REWARD_LATE : REWARD_ON_TIME;

            printf("Returned %s after %ld second%s: %s.\n", late ? "LATE" : "ON TIME",
                   held, held == 1 ? "" : "s", late ? "5-coin reward" : "10-coin reward");

            if (queue_block(&state, "RETURNED", &details, reward, user->librarian_id, key_pair)) {
                printf("Return of '%s' recorded.\n", details.book_title);
            }

        } else if (choice == 3) {
            long period = loan_period_seconds();
            time_t now = time(NULL);
            int marked = 0;

            for (int b = 0; b < book_count; b++) {
                const Block *latest = find_latest_record(state.chain, state.count, state.pending.blocks,
                                                         state.pending.count, books[b].book_id);
                if (latest == NULL || strcmp(latest->action, "BORROWED") != 0 ||
                    now - latest->timestamp < period) {
                    continue;
                }

                /* Not returned yet, so no reward transaction - just a record of the overdue loan. */
                Block details = *latest;
                if (!queue_block(&state, "OVERDUE", &details, REWARD_NONE, user->librarian_id, key_pair)) {
                    break;
                }
                printf("OVERDUE: '%s' borrowed by %s (%s) - no token transaction.\n",
                       details.book_title, details.member_name, details.member_id);
                marked++;
            }

            printf("%d loan(s) marked overdue (loan period: %ld seconds).\n", marked, period);

        } else if (choice == 4) {
            print_pending_pool(&state);

        } else if (choice == 5) {
            mine_solo(&state, user->librarian_id);

        } else if (choice == 6) {
            int miners;
            char prompt[64];
            snprintf(prompt, sizeof(prompt), "Number of pool miners (%d-%d) [%d]: ",
                     MIN_POOL_MINERS, MAX_POOL_MINERS, DEFAULT_POOL_MINERS);
            int ok = prompt_int(prompt, MIN_POOL_MINERS, MAX_POOL_MINERS, DEFAULT_POOL_MINERS, &miners);
            if (ok < 0) break;
            if (ok) mine_pool(&state, miners);

        } else if (choice == 7) {
            char renter[64], prompt[64];
            int rounds, fee;

            snprintf(prompt, sizeof(prompt), "Renter account [%s]: ", user->librarian_id);
            if (!prompt_line(prompt, renter, sizeof(renter))) break;
            if (renter[0] == '\0') strcpy(renter, user->librarian_id);
            if (strlen(renter) >= ACCOUNT_ID_SIZE || !ledger_open_account(&state.ledger, renter)) {
                printf("ERROR: invalid renter account.\n");
                continue;
            }

            int ok = prompt_int("Rental duration in rounds (1-5): ", 1, MAX_RENTAL_ROUNDS, 0, &rounds);
            if (ok < 0) break;
            if (!ok || rounds == 0) {
                if (ok) printf("ERROR: Enter a whole number from 1 to %d.\n", MAX_RENTAL_ROUNDS);
                continue;
            }

            snprintf(prompt, sizeof(prompt), "Rental fee per round in coins [%d]: ", DEFAULT_RENTAL_FEE);
            ok = prompt_int(prompt, 0, 10000, DEFAULT_RENTAL_FEE, &fee);
            if (ok < 0) break;
            if (ok) mine_cloud(&state, renter, rounds, fee);

        } else if (choice == 8) {
            printf("\nConfirmed chain (%d blocks):", state.count);
            for (int i = 0; i < state.count; i++) {
                print_block(&state.chain[i], public_key);
            }

        } else if (choice == 9) {
            ledger_print_balances(&state.ledger);
            if (state.ledger.model == MODEL_UTXO) {
                ledger_print_utxo_set(&state.ledger);
            }

        } else if (choice == 10) {
            char sender[64], recipient[64], amount_text[64], err[200];
            long amount;
            unsigned long nonce = 0;

            if (!prompt_line("From account: ", sender, sizeof(sender)) ||
                !prompt_line("To account  : ", recipient, sizeof(recipient)) ||
                !prompt_line("Amount (coins, fee is 1 extra): ", amount_text, sizeof(amount_text))) {
                break;
            }
            if (!parse_coins(amount_text, &amount)) {
                printf("ERROR: Enter a positive amount such as 3 or 2.50.\n");
                continue;
            }

            if (state.ledger.model == MODEL_ACCOUNT) {
                Account *account = ledger_find_account(&state.ledger, sender);
                char nonce_text[64], prompt[64];
                snprintf(prompt, sizeof(prompt), "Nonce (next valid: %lu): ",
                         account != NULL ? account->nonce : 0UL);
                if (!prompt_line(prompt, nonce_text, sizeof(nonce_text))) break;

                char *end;
                nonce = strtoul(nonce_text, &end, 10);
                if (end == nonce_text || *end != '\0' || nonce_text[0] == '-') {
                    printf("ERROR: The nonce must be a whole number.\n");
                    continue;
                }
            }

            Transaction tx;
            if (!ledger_build_transfer(&state.ledger, sender, recipient, amount, nonce,
                                       &tx, err, sizeof(err))) {
                printf("REJECTED: %s.\n", err);
                continue;
            }
            ledger_print_transaction(&tx);

            if (!ledger_submit(&state.ledger, &tx, err, sizeof(err))) {
                printf("REJECTED: %s.\n", err);
                continue;
            }

            char balance[32];
            printf("ACCEPTED. %s now has %s, ", sender,
                   format_coins(ledger_balance(&state.ledger, sender), balance));
            printf("%s now has %s.\n", recipient,
                   format_coins(ledger_balance(&state.ledger, recipient), balance));
            if (state.ledger.model == MODEL_UTXO) {
                ledger_print_utxo_set(&state.ledger);
            }
            last_transfer = tx;
            have_last_transfer = 1;

        } else if (choice == 11) {
            char err[200];
            if (!have_last_transfer) {
                printf("Make a successful transfer (option 10) first.\n");
                continue;
            }
            printf("Re-submitting the exact same signed-off transaction:\n");
            ledger_print_transaction(&last_transfer);
            if (ledger_submit(&state.ledger, &last_transfer, err, sizeof(err))) {
                printf("ACCEPTED (this should never happen).\n");
            } else {
                printf("REJECTED: %s.\n", err);
            }

        } else if (choice == 12) {
            char id[64];
            if (!prompt_line("Account ID: ", id, sizeof(id))) break;
            if (!ledger_print_history(&state.ledger, id)) {
                printf("ERROR: No account '%s'.\n", id);
            }

        } else if (choice == 13) {
            int new_difficulty;
            char prompt[64];
            snprintf(prompt, sizeof(prompt), "Difficulty (1-4) [%d]: ", state.difficulty);
            int ok = prompt_int(prompt, MIN_DIFFICULTY, MAX_DIFFICULTY, state.difficulty, &new_difficulty);
            if (ok < 0) break;
            if (ok) {
                state.difficulty = new_difficulty;
                printf("New blocks must now start with %d zero%s.\n", new_difficulty,
                       new_difficulty == 1 ? "" : "s");
            }

        } else if (choice == 14) {
            mining_benchmark();

        } else if (choice == 15) {
            report_validation(state.chain, state.count, public_key);

        } else if (choice == 16) {
            if (!is_admin) {
                printf("ERROR: Only an ADMIN can run the tamper-detection demo.\n");
                continue;
            }
            if (state.count < 2) {
                printf("Mine at least one lending block first so there's a block to tamper with.\n");
                continue;
            }

            printf("Changing Block #1's book title in memory only, without re-hashing or re-signing.\n");
            strcpy(state.chain[1].book_title, "TAMPERED TITLE");
            report_validation(state.chain, state.count, public_key);
            printf("(Restart the program to reload the clean chain from disk.)\n");

        } else if (choice == 0) {
            if (state.pending.count > 0) {
                printf("WARNING: %d unconfirmed block%s in the pending pool will be discarded.\n",
                       state.pending.count, state.pending.count == 1 ? "" : "s");
                if (prompt_line("Exit anyway? (y/n): ", input, sizeof(input)) &&
                    strcmp(input, "y") != 0 && strcmp(input, "Y") != 0) {
                    choice = -1;
                    continue;
                }
            }
            printf("Goodbye.\n");
        }

    } while (choice != 0);

    exit_code = 0;

cleanup:
    ledger_free(&state.ledger);
    pending_free(&state.pending);
    free(state.chain);
    EVP_PKEY_free(public_key);
    EVP_PKEY_free(key_pair);
    return exit_code;
}
