#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mining.h"
#include "crypto.h"

#define ALL_ATTEMPTS ((unsigned long)-1)

typedef struct {
    char id[ACCOUNT_ID_SIZE];
    unsigned long hash_rate;      /* attempts per round */
    unsigned long attempts;       /* total attempts */
    int blocks_found;
    long reward;                  /* hundredths of a coin */
} PoolMiner;

static double elapsed_ms(clock_t start)
{
    return (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC;
}

int apply_block_reward(Ledger *ledger, const Block *block)
{
    char err[160];

    if (block->token_reward <= 0) {
        return 1;                 /* no reward, no transaction */
    }
    if (!ledger_mint(ledger, block->tx_id, block->member_id,
                     block->token_reward * COIN, TX_FEE, err, sizeof(err))) {
        printf("WARNING: reward for block #%d not applied: %s.\n", block->index, err);
        return 0;
    }
    return 1;
}

static void print_after_confirmation(LibraryState *state, const Block *block)
{
    char amount[32], credited[32];

    print_block(block, state->public_key);

    if (block->token_reward > 0) {
        printf("  Reward TX confirmed: %d coins - %s fee -> %s credited to %s\n",
               block->token_reward, format_coins(TX_FEE, amount),
               format_coins(block->token_reward * COIN - TX_FEE, credited), block->member_id);
    } else {
        printf("  No reward transaction for this %s block.\n", block->action);
    }

    if (state->ledger.model == MODEL_UTXO) {
        ledger_print_utxo_set(&state->ledger);
    } else {
        printf("  %s balance is now %s\n", block->member_id,
               format_coins(ledger_balance(&state->ledger, block->member_id), amount));
    }
}

/* Prepares the oldest valid pending block; returns 0 if none is left. */
static int next_candidate(LibraryState *state, Block *candidate)
{
    while (state->pending.count > 0) {
        if (state->count >= MAX_BLOCKS) {
            printf("ERROR: Blockchain is full.\n");
            return 0;
        }

        const char *reason;
        *candidate = state->pending.blocks[0];
        if (verify_lending_block(candidate, state->public_key, &reason)) {
            link_block(candidate, &state->chain[state->count - 1], state->difficulty);
            return 1;
        }

        printf("REJECTED pending %s of %s: %s. Dropped from the pool.\n",
               candidate->action, candidate->book_id, reason);
        pending_remove_front(&state->pending, 1);
    }
    return 0;
}

/* Adds a mined block, saves the chain and pays its reward. */
static int confirm_block(LibraryState *state, const Block *mined)
{
    state->chain[state->count++] = *mined;
    pending_remove_front(&state->pending, 1);

    if (!validate_chain(state->chain, state->count, state->public_key, NULL, NULL)) {
        printf("WARNING: chain is invalid, so it was NOT saved to %s.\n", state->chain_file);
    } else if (!save_chain(state->chain_file, state->chain, state->count)) {
        printf("WARNING: could not save the chain to %s.\n", state->chain_file);
    }

    apply_block_reward(&state->ledger, mined);
    print_after_confirmation(state, mined);
    return 1;
}

void print_pending_pool(const LibraryState *state)
{
    printf("\nPending pool: %d unconfirmed block%s waiting to be mined\n",
           state->pending.count, state->pending.count == 1 ? "" : "s");
    if (state->pending.count == 0) {
        return;
    }

    printf("%-3s %-9s %-7s %-22s %-7s %-6s %-20s %-9s\n",
           "#", "Action", "Book", "Member", "Reward", "By", "Reward TX", "Signature");
    printf("--- --------- ------- ---------------------- ------- ------ -------------------- ---------\n");

    for (int i = 0; i < state->pending.count; i++) {
        const Block *b = &state->pending.blocks[i];
        char tx[24] = "(none)";
        if (b->tx_id[0] != '\0') snprintf(tx, sizeof(tx), "%.16s...", b->tx_id);

        printf("%-3d %-9s %-7s %-22.22s %-7d %-6s %-20s %-9s\n", i + 1, b->action, b->book_id,
               b->member_name, b->token_reward, b->librarian_id, tx,
               verify_lending_block(b, state->public_key, NULL) ? "VALID" : "INVALID");
    }
}

/* Solo mining */

int mine_solo(LibraryState *state, const char *miner_id)
{
    Block block;
    int confirmed = 0;
    unsigned long total_attempts = 0;
    char amount[32];

    if (state->pending.count == 0) {
        printf("The pending pool is empty - nothing to mine.\n");
        return 0;
    }

    printf("\n=== SOLO MINING by %s (difficulty %d: hash must start with %d zero%s) ===\n",
           miner_id, state->difficulty, state->difficulty, state->difficulty == 1 ? "" : "s");

    while (next_candidate(state, &block)) {
        unsigned long attempts;
        clock_t start = clock();

        /* Proof of work: change the nonce until the hash is valid. */
        mine_attempts(&block, ALL_ATTEMPTS, &attempts);

        printf("\nMined block #%d after %lu hash attempts (nonce %lu, %.1f ms)\n",
               block.index, attempts, block.nonce, elapsed_ms(start));
        confirm_block(state, &block);
        confirmed++;
        total_attempts += attempts;
    }

    if (confirmed == 0) {
        return 0;
    }

    long reward = (long)confirmed * BLOCK_REWARD * COIN;
    char err[160];
    if (!ledger_mint(&state->ledger, NULL, miner_id, reward, 0, err, sizeof(err))) {
        printf("WARNING: mining reward not paid: %s.\n", err);
    }

    printf("\n--- Solo mining summary ---\n");
    printf("Blocks confirmed      : %d\n", confirmed);
    printf("Total hash attempts   : %lu (average %.0f per block)\n",
           total_attempts, (double)total_attempts / confirmed);
    printf("Mining reward to %-6s: %s coins (%d x %d), on top of the member token rewards\n",
           miner_id, format_coins(reward, amount), confirmed, BLOCK_REWARD);
    return confirmed;
}

/* Pool mining */

int mine_pool(LibraryState *state, int miner_count)
{
    Block block;
    int confirmed = 0;
    char amount[32], err[160];

    if (state->pending.count == 0) {
        printf("The pending pool is empty - nothing to mine.\n");
        return 0;
    }

    PoolMiner *miners = calloc((size_t)miner_count, sizeof(PoolMiner));
    if (miners == NULL) {
        printf("ERROR: out of memory.\n");
        return 0;
    }

    printf("\n=== POOL MINING with %d miners (difficulty %d) ===\n", miner_count, state->difficulty);
    for (int m = 0; m < miner_count; m++) {
        snprintf(miners[m].id, sizeof(miners[m].id), "POOL-M%d", m + 1);
        miners[m].hash_rate = POOL_MIN_HASH_RATE +
                              (unsigned long)rand() % (POOL_MAX_HASH_RATE - POOL_MIN_HASH_RATE + 1);
        printf("  %s hash rate: %lu attempts/round\n", miners[m].id, miners[m].hash_rate);
    }

    while (next_candidate(state, &block)) {
        int finder = -1;
        int rounds = 0;
        unsigned long next_nonce = 0;
        Block winning = block;

        /* All miners hash at the same time on different nonces; the earliest valid hash wins. */
        while (finder == -1) {
            unsigned long used[MAX_POOL_MINERS];
            double win_time = 2.0;     /* later than any attempt in this round */
            rounds++;

            for (int m = 0; m < miner_count; m++) {
                Block attempt = block;
                attempt.nonce = next_nonce;
                next_nonce += miners[m].hash_rate;

                if (mine_attempts(&attempt, miners[m].hash_rate, &used[m])) {
                    double found_at = (double)used[m] / (double)miners[m].hash_rate;
                    if (found_at < win_time) {
                        win_time = found_at;
                        finder = m;
                        winning = attempt;
                    }
                }
            }

            for (int m = 0; m < miner_count; m++) {
                unsigned long by_win = (unsigned long)(win_time * (double)miners[m].hash_rate);
                miners[m].attempts += (m == finder || used[m] < by_win) ? used[m] : by_win;
            }
        }
        block = winning;
        miners[finder].blocks_found++;

        printf("\nBlock #%d found by %s in round %d (nonce %lu)\n",
               block.index, miners[finder].id, rounds, block.nonce);
        confirm_block(state, &block);
        confirmed++;
    }

    if (confirmed == 0) {
        free(miners);
        return 0;
    }

    unsigned long total_attempts = 0;
    for (int m = 0; m < miner_count; m++) total_attempts += miners[m].attempts;

    long gross = (long)confirmed * BLOCK_REWARD * COIN;
    long pool_fee = gross * POOL_FEE_PERCENT / 100;
    long distributable = gross - pool_fee;
    long paid = 0;
    int top = 0;

    /* share = attempts / total attempts * reward, rounded down */
    for (int m = 0; m < miner_count; m++) {
        miners[m].reward = (long)((long long)distributable * (long long)miners[m].attempts /
                                  (long long)total_attempts);
        paid += miners[m].reward;
        if (miners[m].attempts > miners[top].attempts) top = m;
    }
    miners[top].reward += distributable - paid;   /* remainder goes to the top miner */

    ledger_mint(&state->ledger, NULL, POOL_OPERATOR_ACCOUNT, pool_fee, 0, err, sizeof(err));
    for (int m = 0; m < miner_count; m++) {
        if (miners[m].reward > 0 &&
            !ledger_mint(&state->ledger, NULL, miners[m].id, miners[m].reward, 0, err, sizeof(err))) {
            printf("WARNING: reward for %s not paid: %s.\n", miners[m].id, err);
        }
    }

    printf("\n--- Pool reward sharing ---\n");
    printf("Blocks confirmed : %d\n", confirmed);
    printf("Gross reward     : %s coins\n", format_coins(gross, amount));
    printf("Pool fee (%d%%)    : %s coins -> %s\n", POOL_FEE_PERCENT,
           format_coins(pool_fee, amount), POOL_OPERATOR_ACCOUNT);
    printf("To distribute    : %s coins\n\n", format_coins(distributable, amount));

    printf("+----------+-----------+------------+--------+---------+-------------+\n");
    printf("| Miner ID | Hash rate |   Attempts | Blocks | Share %% |      Reward |\n");
    printf("+----------+-----------+------------+--------+---------+-------------+\n");
    for (int m = 0; m < miner_count; m++) {
        printf("| %-8s | %9lu | %10lu | %6d | %6.2f%% | %11s |\n",
               miners[m].id, miners[m].hash_rate, miners[m].attempts, miners[m].blocks_found,
               100.0 * (double)miners[m].attempts / (double)total_attempts,
               format_coins(miners[m].reward, amount));
    }
    printf("+----------+-----------+------------+--------+---------+-------------+\n");
    printf("| TOTAL    |           | %10lu | %6d | 100.00%% | %11s |\n",
           total_attempts, confirmed, format_coins(distributable, amount));
    printf("+----------+-----------+------------+--------+---------+-------------+\n");

    free(miners);
    return confirmed;
}

/* Cloud mining */

int mine_cloud(LibraryState *state, const char *renter_id, int rounds, long rental_fee_coins)
{
    Block block;
    int have_block = 0;            /* block still being mined */
    int confirmed = 0;
    int first_loss_round = 0;
    long rental_fee = rental_fee_coins * COIN;
    long total_gross = 0, total_rental = 0, total_maintenance = 0;
    long rental_paid = 0;          /* what the renter could pay */
    char a[32], b[32], c[32], d[32], e[32];

    printf("\n=== CLOUD MINING: %s rents %lu hashes/round for %d round%s at %s coins/round ===\n",
           renter_id, (unsigned long)CLOUD_HASHES_PER_ROUND, rounds, rounds == 1 ? "" : "s",
           format_coins(rental_fee, a));
    printf("Difficulty %d, %d pending block%s, maintenance %d%% of each round's reward.\n",
           state->difficulty, state->pending.count, state->pending.count == 1 ? "" : "s",
           CLOUD_MAINTENANCE_PCT);

    long round_gross[MAX_RENTAL_ROUNDS], round_maint[MAX_RENTAL_ROUNDS];
    int round_blocks[MAX_RENTAL_ROUNDS];

    for (int r = 0; r < rounds; r++) {
        unsigned long budget = CLOUD_HASHES_PER_ROUND;
        round_blocks[r] = 0;

        /* The rig mines pending blocks until this round's hashes run out. */
        while (budget > 0) {
            if (!have_block) {
                if (!next_candidate(state, &block)) break;
                have_block = 1;
            }
            unsigned long used;
            int found = mine_attempts(&block, budget, &used);
            budget -= used;

            if (found) {
                printf("\n[Round %d] Rented rig mined block #%d (nonce %lu)\n", r + 1, block.index, block.nonce);
                confirm_block(state, &block);
                round_blocks[r]++;
                confirmed++;
                have_block = 0;
            }
        }

        round_gross[r] = (long)round_blocks[r] * BLOCK_REWARD * COIN;
        round_maint[r] = round_gross[r] * CLOUD_MAINTENANCE_PCT / 100;

        /* Pay the round's rewards, minus the provider's cut. */
        if (round_gross[r] - round_maint[r] > 0) {
            char err[160];
            ledger_mint(&state->ledger, NULL, renter_id, round_gross[r] - round_maint[r], 0,
                        err, sizeof(err));
        }

        /* Then the renter pays the rental fee. */
        rental_paid += ledger_charge(&state->ledger, renter_id, CLOUD_PROVIDER_ACCOUNT, rental_fee);
    }

    if (have_block) {
        printf("\nRental ended mid-block: block for %s stays in the pending pool.\n", block.book_id);
    }

    printf("\n--- Cloud mining earnings ---\n");
    printf("+-------+--------+-----------+-----------+-------------+-----------+--------------+\n");
    printf("| Round | Blocks |     Gross |    Rental | Maintenance |       Net | Cumul. net   |\n");
    printf("+-------+--------+-----------+-----------+-------------+-----------+--------------+\n");

    long cumulative_rewards = 0, cumulative_fees = 0;
    for (int r = 0; r < rounds; r++) {
        long fees = rental_fee + round_maint[r];
        cumulative_rewards += round_gross[r];
        cumulative_fees += fees;
        total_gross += round_gross[r];
        total_rental += rental_fee;
        total_maintenance += round_maint[r];

        printf("| %5d | %6d | %9s | %9s | %11s | %9s | %12s |%s\n", r + 1, round_blocks[r],
               format_coins(round_gross[r], a), format_coins(rental_fee, b),
               format_coins(round_maint[r], c), format_coins(round_gross[r] - fees, d),
               format_coins(cumulative_rewards - cumulative_fees, e),
               cumulative_fees > cumulative_rewards ? "  <- fees exceed rewards" : "");

        if (cumulative_fees > cumulative_rewards && first_loss_round == 0) {
            first_loss_round = r + 1;
        }
    }
    printf("+-------+--------+-----------+-----------+-------------+-----------+--------------+\n");

    long total_fees = total_rental + total_maintenance;
    printf("Gross earnings  : %s coins\n", format_coins(total_gross, a));
    printf("Total fees paid : %s coins (rental %s + maintenance %s)\n",
           format_coins(total_fees, b), format_coins(total_rental, c), format_coins(total_maintenance, d));
    printf("Net profit      : %s coins\n", format_coins(total_gross - total_fees, e));
    printf("Credited to %s : %s coins (gross minus maintenance)\n",
           renter_id, format_coins(total_gross - total_maintenance, a));
    printf("Rental deducted from %s : %s coins -> %s\n",
           renter_id, format_coins(rental_paid, b), CLOUD_PROVIDER_ACCOUNT);
    if (rental_paid < total_rental) {
        printf("Rental still owed : %s coins (%s ran out of tokens; a balance cannot go negative)\n",
               format_coins(total_rental - rental_paid, c), renter_id);
    }
    printf("%s balance is now %s coins\n", renter_id,
           format_coins(ledger_balance(&state->ledger, renter_id), d));

    if (total_gross < total_fees) {
        printf("\nWARNING: this rental is UNPROFITABLE - cumulative fees exceeded cumulative "
               "rewards from round %d.\n", first_loss_round);
    } else if (first_loss_round != 0) {
        /* It was losing money at some round, but later blocks covered the fees. */
        printf("\nWARNING: this rental was UNPROFITABLE at round %d - cumulative fees exceeded "
               "cumulative rewards there, although it ended in profit.\n", first_loss_round);
    }
    return confirmed;
}

/* Benchmark */

void mining_benchmark(void)
{
    Block block;

    printf("\nProof-of-work effort by difficulty (same test block each time):\n");
    printf("%-10s %-10s %12s %12s  %s\n", "Difficulty", "Target", "Attempts", "Time (ms)", "Hash");
    printf("---------- ---------- ------------ ------------  ----------------\n");

    for (int d = MIN_DIFFICULTY; d <= MAX_DIFFICULTY; d++) {
        unsigned long attempts;
        char target[12] = "";

        memset(&block, 0, sizeof(block));
        block.index = 1;
        block.timestamp = 1700000000;
        strcpy(block.action, "BENCHMARK");
        memset(block.previous_hash, '0', 64);
        block.difficulty = d;

        for (int i = 0; i < d; i++) target[i] = '0';
        strcat(target, "...");

        clock_t start = clock();
        mine_attempts(&block, ALL_ATTEMPTS, &attempts);
        printf("%-10d %-10s %12lu %12.1f  %.16s...\n", d, target, attempts, elapsed_ms(start), block.hash);
    }
    printf("Each extra zero multiplies the expected work by 16 (one hex digit).\n");
}
