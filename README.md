# Blockchain-Based Library Book Lending Tracker

## Formative 2: Token Rewards, Transaction Models and Mining

This version extends the Formative 1 tracker. Returning a book now creates a **token reward transaction**. Lending blocks are no longer added to the chain straight away: they wait in a **pending pool** until a miner confirms them with **proof of work**. Member balances are kept with either the **UTXO model** or the **account-based model**, and the pool can be mined three ways: **solo**, **pool** or **cloud rental**.

### What changed from Formative 1

| Before (Formative 1) | Now (Formative 2) |
| :---- | :---- |
| Borrow/return appended a block immediately | Borrow/return/overdue creates a signed block in the **pending pool** |
| No tokens | On-time return = **10 coins**, late return = **5 coins**, not returned (OVERDUE) = **no transaction** |
| Block hash had no work behind it | Each block is **mined**: the nonce is incremented until the hash starts with *difficulty* zeros (1-4, default 2) |
| Validation checked hash, link and signature | Validation also checks **proof of work** and that the **reward / transaction ID** match the block and are never used twice |

New block fields: `token_reward` (10, 5 or 0), `tx_id` (SHA-256 of the reward transaction), plus `difficulty` and `nonce` for proof of work.

### New source files

| File | Purpose |
| :---- | :---- |
| [src/pending.c](src/pending.c) / [.h](src/pending.h) | The pending pool: a heap array (grown with `realloc`) of signed blocks waiting to be mined. |
| [src/ledger.c](src/ledger.c) / [.h](src/ledger.h) | Both transaction models: UTXO set, account balances with nonces, fees, change outputs, double-spend and nonce checks, and a per-account transaction history stored as a linked list. |
| [src/mining.c](src/mining.c) / [.h](src/mining.h) | Solo, pool and cloud mining, confirming pending blocks onto the chain, and a difficulty benchmark. |

`blockchain.c` gained the proof-of-work loop (`mine_attempts`), the reward transaction ID, and the extra validation checks. `crypto.c` gained a `sha256_hex` helper.

### Compilation

Dependencies are unchanged: `gcc`, `make` and OpenSSL 3 (`sudo apt install gcc make libssl-dev`). No other libraries are needed.

```bash
make clean && make
```

which runs

```bash
gcc -Wall -Wextra -std=c11 -o library src/main.c src/blockchain.c src/crypto.c src/registry.c src/pending.c src/ledger.c src/mining.c -lssl -lcrypto
```

The chain file format has new fields, so a `data/chain.txt` saved by Formative 1 cannot be loaded. Delete it once before the first run: `rm -f data/chain.txt`. The keys (`key.pem`, `pub.pem`) can stay.

### Switching between transaction models

The model is chosen once and used for every token transaction in that session.

| How | Example |
| :---- | :---- |
| Command-line option | `./library --model utxo` or `./library --model account` |
| Start-up menu | Run `./library` with no `--model`. After login it asks: `1. UTXO model  2. Account-based model` |

The menu header always shows the active model, for example `[model: UTXO | difficulty: 2 | pending: 3 | chain: 5 blocks]`.

### Setting the mining difficulty

Difficulty is the number of leading `0` characters a block hash must have (range **1 to 4**, default **2**).

| How | Example |
| :---- | :---- |
| Command-line option | `./library --difficulty 3` |
| Menu option 13 | `Set mining difficulty (1-4)` changes it for the next blocks mined |
| Menu option 14 | Benchmark: mines a test block at 1, 2, 3 and 4 and prints the attempts and time for each |

Each block stores the difficulty it was mined at, so validation still works after the difficulty changes.

### Menu

```
 -- Lending --                       -- Tokens --
 1. Borrow a book                     8. View confirmed lending records
 2. Return a book                     9. View token balances (and UTXO set)
 3. Mark overdue loans               10. Transfer tokens
 4. View pending pool                11. Replay last transfer (double-spend / nonce test)
 -- Mining --                        12. Transaction history for a member
 5. Mine pending blocks: solo         -- Chain --
 6. Mine pending blocks: pool        13. Set mining difficulty (1-4)
 7. Mine pending blocks: cloud       14. Proof-of-work difficulty benchmark
                                     15. Validate the blockchain
                                     16. Tamper-detection demo (ADMIN)
                                      0. Exit
```

### How the system works

**Lending events trigger token transactions.** When a book is returned, the program checks whether it is late: either the loan is older than the loan period (120 seconds by default, or the value of `LOAN_PERIOD_SECONDS`) or the loan was already marked OVERDUE. An on-time return carries a 10-coin reward and a late one carries 5 coins. The reward is written into the block's `token_reward` field, and the block's `tx_id` is the SHA-256 of the reward transaction (timestamp, book, member, librarian, amount). Borrowing and OVERDUE blocks carry 0 coins and no transaction. The librarian's ECDSA signature covers the reward and the `tx_id`, so nobody can change a reward later.

**The pending pool and mining.** A new block is signed and placed in the pending pool, with no position on the chain yet. Option 4 lists the pool with each block's reward and signature status. Mining takes the blocks in order. For each one, it checks the signature and reward again, links it to the tip of the chain (`index`, `previous_hash`), then increments the `nonce` and re-hashes it with SHA-256 until the hash starts with the required number of zeros. Only then is the block appended and saved, and only then is the reward credited to the member. The block hash covers the index, previous hash, difficulty, signed data, signature and nonce, so changing anything in a mined block breaks its proof of work and every link after it.

**UTXO model.** Tokens exist only as unspent transaction outputs `(tx_id, output index, owner, amount)`. A member's balance is the sum of the outputs they own. A reward is a coinbase transaction with no inputs: the 1-coin fee is deducted, so the member gets an output of 9 (or 4) and the library treasury (`LIBRARY`) gets a 1-coin fee output. A transfer selects the sender's oldest outputs as inputs until they cover *amount + fee*. Any excess comes back to the sender as a **change output**, and if the inputs cannot cover *amount + fee* the transfer is rejected. When a transaction is applied, its inputs are removed from the UTXO set, so spending the same output again (a **double spend**) is rejected because that output no longer exists. The full UTXO set is printed after every confirmed block and after every transfer.

**Account model.** Each member is an account with a balance and a **nonce**. A confirmed reward credits the balance directly (amount minus the 1-coin fee). A transfer debits the sender by *amount + fee* and credits the recipient. It is rejected if the balance is too low, or if the nonce is not exactly the sender's next nonce, which stops reused (replayed) or out-of-order transactions. The nonce increases by one with every outgoing transaction. Every account keeps a **linked list** of its transactions (sender, recipient, amount, fee, nonce), printed with option 12.

**Comparison.** UTXO stores coins rather than balances, so it can check each input on its own and prevents double spends by construction. The cost is that it needs change outputs and a balance is a sum over the set. The account model is simpler to read and update, and a balance is one number, but it needs nonces to stop replays.

**Solo mining** (option 5). The logged-in librarian mines every pending block alone. The program prints the hash attempts, nonce and time for each block. The miner earns 50 coins per confirmed block, on top of the member rewards.

**Pool mining** (option 6). 2-8 miners (default 4), each with a random hash rate of 50-500 attempts per round. In each round all miners hash in parallel on separate nonce ranges, and the miner who hits the target earliest (relative to its speed) wins the block. The total reward is 50 coins per block. A 2% pool fee goes to `POOL-OP`, and the rest is shared as `(miner_attempts / total_attempts) × reward` in a table showing miner ID, hash rate, attempts, blocks found, share % and reward.

**Cloud mining** (option 7). The user rents 600 hashes per round for 1-5 rounds and pays a fixed rental fee each round (default 25 coins). Each block the rented rig confirms pays 50 coins, of which the provider keeps 10% as a maintenance fee. Rewards are credited round by round, and each round's rental fee is then deducted from the renter's balance and paid to `CLOUD-OP`. A balance cannot go negative, so if the renter runs out of tokens the unpaid part is reported as still owed. The table shows gross earnings, rental, maintenance and net profit per round and cumulatively. If cumulative fees ever exceed cumulative rewards, the program prints **WARNING: this rental is UNPROFITABLE**. Higher difficulty means fewer blocks per round, so the same rental becomes unprofitable.

### Design choices and assumptions

- **Amounts are stored in hundredths of a coin** (`long`), so the 2% pool fee and pool shares are exact without floating-point rounding errors. Any remainder from rounding goes to the miner with the most attempts.
- **The fixed 1-coin fee** applies to member rewards and member transfers, and goes to the `LIBRARY` treasury account. Mining rewards are new coins (coinbase) and pay no fee.
- **The signature covers the lending event, not its chain position**, because a block is signed when the librarian records it, before a miner decides where it goes. Its position (index, previous hash) and the proof of work are protected by the block hash.
- **Pending blocks are kept in memory.** If you exit with blocks still pending, the program warns you and asks you to confirm.
- **Balances are rebuilt from the chain at start-up**: every confirmed RETURNED block's reward is replayed, so members never lose confirmed rewards. If the saved chain fails validation, nothing is replayed and all balances start at 0, so a tampered reward is never paid out. Mining rewards and manual transfers are not recorded on the lending chain, so they last only for the session.
- **Transfers are submitted by the librarian** on a member's behalf (members have no keys of their own), and are applied as soon as they pass validation.
- Book status (on loan or not) looks at both the confirmed chain and the pending pool, so a book cannot be borrowed twice while the first borrow is still waiting to be mined.

### How to test the mining simulations and edge cases

Set the passphrase once with `export LIBRARY_KEY_PASSPHRASE=demo-pass`, and run `rm -f data/chain.txt` before each command to start clean. Each command logs in as `LIB001` / `1234`.

| Test | Command | Expected |
| :---- | :---- | :---- |
| On-time return + solo mining | `printf 'LIB001\n1234\n1\nBK001\nALU001\n2\nBK001\nALU001\n4\n5\n9\n0\n' \| ./library --model utxo` | Pending pool shows 2 blocks; solo mining prints attempts per block; ALU001 gets a 9.00 UTXO (10 − 1 fee) |
| Late return (5 coins) | `printf 'LIB001\n1234\n1\nBK001\nALU001\n3\n2\nBK001\nALU001\n5\n9\n0\n' \| LOAN_PERIOD_SECONDS=0 ./library --model utxo` | "Returned LATE"; ALU001 ends with 4.00 |
| Overdue, not returned: no transaction | `printf 'LIB001\n1234\n1\nBK002\nALU002\n3\n4\n5\n9\n0\n' \| LOAN_PERIOD_SECONDS=0 ./library --model account` | OVERDUE block has reward 0 and "(none)" TX; ALU002 stays 0.00 |
| UTXO change + double spend | `printf 'LIB001\n1234\n1\nBK001\nALU001\n2\nBK001\nALU001\n5\n10\nALU001\nALU002\n3\n11\n0\n' \| ./library --model utxo` | Transfer has a 5.00 change output; the replay is rejected as a DOUBLE SPEND |
| Insufficient balance (UTXO) | `printf 'LIB001\n1234\n10\nALU001\nALU002\n100\n0\n' \| ./library --model utxo` | "inputs insufficient" |
| Nonce check + history (account) | `printf 'LIB001\n1234\n1\nBK001\nALU001\n2\nBK001\nALU001\n5\n10\nALU001\nALU002\n3\n0\n11\n10\nALU001\nALU002\n1\n7\n12\nALU001\n0\n' \| ./library --model account` | First transfer accepted with nonce 0; replay rejected (reused nonce); nonce 7 rejected; history lists both transactions |
| Insufficient balance (account) | `printf 'LIB001\n1234\n10\nALU003\nALU001\n5\n0\n0\n' \| ./library --model account` | "insufficient balance" |
| Pool mining | `printf 'LIB001\n1234\n1\nBK001\nALU001\n1\nBK002\nALU002\n2\nBK001\nALU001\n13\n3\n6\n\n0\n' \| ./library --model utxo` | Reward table with 4 miners, 2% pool fee, shares adding up to 100% |
| Unprofitable cloud rental | `printf 'LIB001\n1234\n1\nBK003\nALU003\n7\n\n3\n25\n0\n' \| ./library --model utxo` | 1 block in round 1, nothing after; WARNING UNPROFITABLE from round 2 |
| Difficulty 4 | `printf 'LIB001\n1234\n1\nBK004\nALU004\n5\n14\n0\n' \| ./library --model utxo --difficulty 4` | Tens of thousands of attempts for one block; benchmark shows ~16× more work per extra zero |
| Verify with public key only | `./library --verify` | VALID; edit a `token_reward` in `data/chain.txt` and it reports the block as tampered |

### Running

| Command | What it does |
| :---- | :---- |
| `./library` | Starts the program. It asks for the key passphrase and a librarian login, then the transaction model, then shows the menu. |
| `./library --model account --difficulty 3` | Starts with the model and difficulty already chosen. |
| `./library --verify` | Checks `data/chain.txt` using only the public key in `data/pub.pem`. It needs no passphrase and no login. |
| `./library --hash-pin LIB003 4321` | Prints the PIN hash to put in a new line of `data/librarians.txt`. |

The signing key is encrypted with a passphrase of at least 4 characters. The program asks for it at start-up, or reads it from the `LIBRARY_KEY_PASSPHRASE` environment variable. The first run creates the key with whatever passphrase you give, and later runs must use the same one.

These files are created when the program runs. They are listed in `.gitignore` and are not committed:

| File | Purpose |
| :---- | :---- |
| `library` | The compiled program. |
| `data/chain.txt` | The saved blockchain, one block per line. |
| `data/key.pem` | The private signing key, encrypted with the passphrase. |
| `data/pub.pem` | The public key, used to check signatures. |

### Test data

| Librarian ID | Name | Role | PIN |
| :---- | :---- | :---- | :---- |
| LIB001 | Joseph Nishimwe | ADMIN | 1234 |
| LIB002 | Librarian | LIBRARIAN | 5678 |

Books are `BK001` to `BK005` ([data/books.txt](data/books.txt)) and members are `ALU001` to `ALU005` ([data/members.txt](data/members.txt)).

### Demo video and report

- **Demo video:** https://youtu.be/cwhcAC3NmaE
- **Technical report:** [Formative2_Technical-Report_Book Lending_Tracker.pdf](Formative2_Technical-Report_Book%20Lending_Tracker.pdf)
- **Repository:** https://github.com/josep-prog/Formative2_Extending_Blockchain-Based_LibraryBookLendingTracker
