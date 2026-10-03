#ifndef LEDGER_H
#define LEDGER_H

#include <stddef.h>

/*
 * Member token balances under one of two models, chosen once per session:
 *   UTXO    - balances are the sum of unspent transaction outputs a member owns
 *   ACCOUNT - each member has a stored balance and a nonce
 *
 * Amounts are kept in hundredths of a coin (COIN = 100), so pool shares and the
 * 2% pool fee stay exact without floating point.
 */

typedef enum { MODEL_UTXO, MODEL_ACCOUNT } LedgerModel;

#define COIN            100L
#define TX_FEE          (1 * COIN)     /* fixed fee on every reward and transfer */
#define FEE_ACCOUNT     "LIBRARY"      /* the library treasury collects all fees */
#define COINBASE_SENDER "COINBASE"     /* sender of newly created tokens          */

#define ACCOUNT_ID_SIZE 20
#define MAX_TX_INPUTS   32
#define MAX_TX_OUTPUTS  3              /* recipient, change, fee */

typedef struct {
    char tx_id[65];       /* transaction that created the output being spent */
    int output_index;
} TxInput;

typedef struct {
    char owner[ACCOUNT_ID_SIZE];
    long amount;
} TxOutput;

typedef struct {
    char tx_id[65];                    /* SHA-256 of every field below */
    char sender[ACCOUNT_ID_SIZE];
    char recipient[ACCOUNT_ID_SIZE];
    long amount;                       /* what the recipient receives */
    long fee;
    unsigned long nonce;               /* account model only */

    int input_count;                   /* UTXO model only */
    TxInput inputs[MAX_TX_INPUTS];
    int output_count;
    TxOutput outputs[MAX_TX_OUTPUTS];
} Transaction;

/* One entry in the UTXO set. */
typedef struct {
    char tx_id[65];
    int output_index;
    char owner[ACCOUNT_ID_SIZE];
    long amount;
} Utxo;

/* Per-account transaction history, kept as a singly linked list. */
typedef struct HistoryNode {
    char tx_id[65];
    char sender[ACCOUNT_ID_SIZE];
    char recipient[ACCOUNT_ID_SIZE];
    long amount;
    long fee;
    unsigned long nonce;
    int has_nonce;                     /* 0 for minted tokens and UTXO transfers */
    struct HistoryNode *next;
} HistoryNode;

typedef struct {
    char id[ACCOUNT_ID_SIZE];
    long balance;                      /* account model only */
    unsigned long nonce;               /* next nonce this account must use */
    HistoryNode *history_head;
    HistoryNode *history_tail;
} Account;

typedef struct {
    LedgerModel model;

    Account *accounts;                 /* heap arrays, grown with realloc */
    int account_count;
    int account_capacity;

    Utxo *utxos;
    int utxo_count;
    int utxo_capacity;

    unsigned long mint_sequence;       /* makes every coinbase tx_id unique */
} Ledger;

void ledger_init(Ledger *ledger, LedgerModel model);
void ledger_free(Ledger *ledger);
const char *ledger_model_name(LedgerModel model);

/* Creates the account if it does not exist. Returns 0 if memory ran out. */
int ledger_open_account(Ledger *ledger, const char *id);
Account *ledger_find_account(Ledger *ledger, const char *id);
long ledger_balance(const Ledger *ledger, const char *id);

/*
 * Creates new tokens (a lending reward or a mining reward). fee is taken out of
 * gross before the recipient is credited. tx_id may be NULL to generate one.
 */
int ledger_mint(Ledger *ledger, const char *tx_id, const char *recipient,
                long gross, long fee, char *err, size_t err_size);

/*
 * Builds a member-to-member transfer without applying it.
 * UTXO: picks the sender's outputs and adds a change output.
 * ACCOUNT: records the nonce the sender supplied.
 */
int ledger_build_transfer(Ledger *ledger, const char *sender, const char *recipient,
                          long amount, unsigned long nonce, Transaction *tx,
                          char *err, size_t err_size);

/* Validates and applies a transfer. Rejects double spends, bad nonces and low balances. */
int ledger_submit(Ledger *ledger, const Transaction *tx, char *err, size_t err_size);

/*
 * Fee-less service charge (the cloud rental): moves up to amount from payer to payee.
 * A balance can never go negative, so it returns what was actually paid.
 */
long ledger_charge(Ledger *ledger, const char *payer, const char *payee, long amount);

/* "12.50" style text for an amount in hundredths of a coin. */
const char *format_coins(long amount, char buf[32]);

void ledger_print_balances(const Ledger *ledger);
void ledger_print_utxo_set(const Ledger *ledger);
void ledger_print_transaction(const Transaction *tx);
int ledger_print_history(Ledger *ledger, const char *id);

#endif
