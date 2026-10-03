# Blockchain-Based Library Book Lending Tracker

## Formative 2: Token Rewards, Transaction Models and Mining

This version extends the Formative 1 tracker. Returning a book now creates a **token reward transaction**. Lending blocks are no longer added to the chain straight away: they wait in a **pending pool** until a miner confirms them with **proof of work**. Member balances are kept with either the **UTXO model** or the **account-based model**, and the pool can be mined three ways: **solo**, **pool** or **cloud rental**.

### What changed from Formative 1

| Before (Formative 1) | Now (Formative 2) |
| :---- | :---- |
| Borrow/return appended a block immediately | Borrow/return/overdue creates a signed block in the **pending pool** |
| No tokens | On-time return = **10 coins**, late return = **5 coins**, not returned (OVERDUE) = **no transaction** |
| Block hash had no work behind it | Each block is **mined**: the nonce is incremented until the hash starts with *difficulty* zeros (1-4, default 2) |
| Validation checked hash, link and signature | Validation also checks **proof of work** and that the **reward / transaction ID** match the block |

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

---

## Formative 1 report

The sections below are the original Formative 1 documentation. Where the build command, file list or menu differ, the Formative 2 section above is current.

**DemoVideo**: _https://youtu.be/YUA85d7KCy0_
| File Name | Link | Purpose of the file |
| :---- | :---- | :---- |
| DemoVideo formative | [**DemoVideo**](https://youtu.be/YUA85d7KCy0)  | complete explanation of the entire project , and complete guard on how to run it | 
| Blockchain.c | [**blockchain.c**](https://github.com/josep-prog/formative_introduction_blockchain/blob/main/src/blockchain.c)  | I created this file to manage the blockchain. It creates the genesis, borrowing and returning blocks, links them using hashes, checks active loans, validates the blockchain (hashes, links and signatures), and saves and loads the chain file. |
| Blockchain.h | [**blockchain.h**](https://github.com/josep-prog/formative_introduction_blockchain/blob/main/src/blockchain.h)  | I created this file to define the Block structure and declare the blockchain functions so that other files, especially main.c, can use them. |
| Crypto.c | [**crypto.c**](https://github.com/josep-prog/formative_introduction_blockchain/blob/main/src/crypto.c)  | I created this file to handle the security part of the system. It generates keys, saves and loads the passphrase-encrypted key file, creates and verifies digital signatures, and hashes librarian PINs using OpenSSL. |
| Crypto.h | [**crypto.h**](https://github.com/josep-prog/formative_introduction_blockchain/blob/main/src/crypto.h)  | I created this file to declare the cryptographic functions so that main.c and blockchain.c can use the security functions from crypto.c |
| Main.c | [**main.c**](https://github.com/josep-prog/formative_introduction_blockchain/blob/main/src/main.c)  | I created this file to control the whole program. It handles the menu and calls the other files when the program needs to load records, create blockchain transactions, or perform security checks. |
| Registry.h | [**registry.h**](https://github.com/josep-prog/formative_introduction_blockchain/blob/main/src/registry.h)   | I created this file to define the Book, Member and Librarian structures and declare the registry functions used by main.c. |
| Registry.c | [**registry.c**](https://github.com/josep-prog/formative_introduction_blockchain/blob/main/src/registry.c)  | I created this file to load the books, members and librarians from the data files, report any bad lines, and find a record by ID. |
| Books.txt | [**books.txt**](https://github.com/josep-prog/formative_introduction_blockchain/blob/main/data/books.txt)  | This file is for storing the registered books, including their IDs, titles, and authors. |
| Members.txt | [**members.txt**](https://github.com/josep-prog/formative_introduction_blockchain/blob/main/data/members.txt)  | this file to store the registered library members and their basic information. |
| Librarians.txt | [**librarians.txt**](https://github.com/josep-prog/formative_introduction_blockchain/blob/main/data/librarians.txt) | this file stores the staff who can log in: their ID, name, role (ADMIN or LIBRARIAN) and a PBKDF2 hash of their PIN. |
| Makefile | [**Makefile**](https://github.com/josep-prog/formative_introduction_blockchain/blob/main/Makefile) | this file to make compiling the whole project easier by providing the commands needed to build the program and link OpenSSL. |

**Technical Report: Individual Assignment 1**
**Student:** Joseph Nishimwe · African Leadership University
**Demo video:** https://youtu.be/YUA85d7KCy0
**Repository:** https://github.com/josep-prog/formative_introduction_blockchain

## Contents

1. [Introduction](#1-introduction)
2. [Project files](#2-project-files)
3. [Build and run](#3-build-and-run)
4. [Description of the blockchain implementation](#4-description-of-the-blockchain-implementation)
5. [Security mechanisms](#5-security-mechanisms)
6. [Data persistence](#6-data-persistence)
7. [Error handling strategy](#7-error-handling-strategy)
8. [Screenshots of application execution](#8-screenshots-of-application-execution)
9. [Testing](#9-testing)
10. [Challenges encountered and solutions](#10-challenges-encountered-and-solutions)
11. [System design diagram](#11-system-design-diagram)
12. [Limitations](#12-limitations)
13. [Conclusion](#13-conclusion)

<img width="916" height="606" alt="structure" src="https://github.com/user-attachments/assets/e85e3837-be68-4c46-9ee4-873c61da14c0" />

I divided the program into different parts so that each part has a clear responsibility. The main.c file controls what the user sees and connects the other parts of the program. The registry.c file is responsible for loading and searching for books and members. The blockchain.c file contains the main blockchain logic, such as creating blocks, connecting them, checking borrowing status, calculating hashes, and validating the chain. The crypto.c file handles the digital signatures and the creation of the cryptographic key. This separation makes the program easier to understand because the code that deals with books and members is kept separate from the code that deals with the blockchain and cryptography.
---

## 1. Introduction

A library needs to know which books are out, who has them, and when they come back. Usually this is kept in a paper log or a normal database. The problem is that these records are easy to change. A librarian could quietly mark a lost book as "returned", or a borrower could say the log is wrong, and nobody could prove what really happened.

In this project I built a small library lending system in the C language that solves this problem with a blockchain. Every time a book is borrowed or returned, the program writes a new record called a block. Each block is locked to the block before it with a SHA-256 hash, and each block is signed with a digital signature. Because of this, if anyone changes an old record, the program can detect it.

The program runs in the terminal. A librarian logs in, and then can borrow a book, return a book, view all records, check that the chain is still valid, mark overdue loans, and (as an admin) run a demonstration of tamper detection.

## 2. Project files

| File | Purpose |
| :---- | :---- |
| [src/main.c](src/main.c) | Controls the whole program. It shows the menu, handles the login, and calls the other files to load records, create blocks and run security checks. |
| [src/blockchain.c](src/blockchain.c) | Manages the blockchain. It creates the genesis, borrow, return and overdue blocks, links them with hashes, finds active loans, validates the chain (hashes, links and signatures), and saves and loads the chain file. |
| [src/blockchain.h](src/blockchain.h) | Defines the `Block` structure and declares the blockchain functions. |
| [src/crypto.c](src/crypto.c) | Handles security with OpenSSL. It generates the key pair, saves and loads the encrypted private key and the public key, signs and verifies blocks, and hashes librarian PINs. |
| [src/crypto.h](src/crypto.h) | Declares the cryptographic functions. |
| [src/registry.c](src/registry.c) | Loads books, members and librarians from the data files, reports bad lines, and finds a record by ID. |
| [src/registry.h](src/registry.h) | Defines the `Book`, `Member` and `Librarian` structures. |
| [src/pending.c](src/pending.c) | (Formative 2) The pending pool of blocks waiting to be mined. |
| [src/ledger.c](src/ledger.c) | (Formative 2) UTXO and account-based token ledgers. |
| [src/mining.c](src/mining.c) | (Formative 2) Solo, pool and cloud mining. |
| [data/books.txt](data/books.txt) | The book registry: ID, title, author. |
| [data/members.txt](data/members.txt) | The member registry: ID, full name, course code. |
| [data/librarians.txt](data/librarians.txt) | Staff who can log in: ID, name, role (ADMIN or LIBRARIAN) and a PBKDF2 hash of their PIN. |
| [Makefile](Makefile) | Builds the program and links OpenSSL with one command. |

These files are created when the program runs. They are listed in `.gitignore` and are not committed:

| File | Purpose |
| :---- | :---- |
| `data/chain.txt` | The saved blockchain, one block per line. |
| `data/key.pem` | The private signing key, encrypted with a passphrase. |
| `data/pub.pem` | The public key, used to check signatures. |

## 3. Build and run

### Required libraries and dependencies

| Dependency | Why it is needed |
| :---- | :---- |
| `gcc` | C compiler (the code is C11). |
| `make` | Runs the build steps in the Makefile. |
| OpenSSL 3 (`libssl-dev`) | SHA-256 hashing, ECDSA signatures, AES key encryption and PBKDF2 PIN hashing. |

On Ubuntu or Debian, install all of them with:

```bash
sudo apt install gcc make libssl-dev
```

### Compilation

```bash
git clone https://github.com/josep-prog/formative_introduction_blockchain.git
cd formative_introduction_blockchain
make clean && make
```

This compiles all the source files into one program called `library`. The Makefile runs:

```bash
gcc -Wall -Wextra -std=c11 -o library src/main.c src/blockchain.c src/crypto.c src/registry.c src/pending.c src/ledger.c src/mining.c -lssl -lcrypto
```

### Running

| Command | What it does |
| :---- | :---- |
| `./library` | Starts the program. It asks for the key passphrase and a librarian login, then shows the menu. |
| `./library --verify` | Checks `data/chain.txt` using only the public key in `data/pub.pem`. It needs no passphrase and no login, so anyone can check the records. |
| `./library --hash-pin LIB003 4321` | Prints the PIN hash to put in a new line of `data/librarians.txt`. |

The signing key in `data/key.pem` is encrypted with a passphrase of at least 4 characters. The program asks for it at start-up, or reads it from the `LIBRARY_KEY_PASSPHRASE` environment variable. The first run creates the key with whatever passphrase you give, and later runs must use the same one.

If you ran an older version of this program, delete the old files first, because the chain format changed:

```bash
rm -f data/chain.txt data/key.pem data/pub.pem
```

### Test data

**Librarians (logins):**

| ID | Name | Role | PIN |
| :---- | :---- | :---- | :---- |
| LIB001 | Joseph Nishimwe | ADMIN | 1234 |
| LIB002 | Librarian | LIBRARIAN | 5678 |
<img width="1272" height="704" alt="librarian-admin" src="https://github.com/user-attachments/assets/f98a7e52-d0d0-489c-934f-130c141d665e" />


**Books:**

| ID | Title | Author |
| :---- | :---- | :---- |
| BK001 | The Money Trap: Lost Illusions Inside the Tech Bubble | Alok Sama |
| BK002 | Wars Guns & Votes: Democracy in Dangerous Places | Paul Collier |
| BK003 | Sustainable Leadership | Clarke Murphy |
| BK004 | Ancient Philosophy: The Fundamentals | Daniel W. Graham |
| BK005 | Gulliver's Travels and Other Writings | Jonathan Swift |

**Members:**

| ID | Name | Course |
| :---- | :---- | :---- |
| ALU001 | Irakoze Jean | BSE |
| ALU002 | Joseph Habimana | BSE |
| ALU003 | Uwase Diane | BEL |
| ALU004 | Manzi Eric | BSE |
| ALU005 | Mukamana Alice | BEL |

## 4. Description of the blockchain implementation

### 4.1 How the code is organised

I split the program into four parts so that each part has one job. `main.c` shows the menu and connects everything. `registry.c` loads the books, members and librarians and finds a record by its ID. `blockchain.c` creates blocks, links them together, checks the chain and saves it to a file. `crypto.c` does all the cryptography: making keys, signing, checking signatures and hashing PINs. Keeping these apart makes the program easier to read, because the code for books and members is separate from the code for the blockchain and cryptography.

### 4.2 The book and member registries

When the program starts, it reads `books.txt` and `members.txt` into arrays of `Book` and `Member` structs. Each line has three fields separated by commas. If a file is missing or has no usable lines, the program prints an error and stops. A bad line (a missing field, a field that is too long, a duplicate ID or a `|` character) is reported with its line number and skipped. Windows line endings are handled too.

These registries are the first check. Before any block is created, the program looks up the book ID and the member ID. If either one is not in the registry, it prints `ERROR: Book or Member not found` and nothing is added to the chain. The match is exact, so an ID like `BK001X` is not accepted as `BK001`.

### 4.3 The block

Each lending event is stored in one block:

| Field | Type | What it holds |
| :---- | :---- | :---- |
| `index` | `int` | Position of the block in the chain. The first block is 0. |
| `timestamp` | `time_t` | The time the block was created. |
| `book_id`, `book_title` | `char[20]`, `char[80]` | The book. The title is copied from the registry at that moment. |
| `member_id`, `member_name` | `char[20]`, `char[50]` | The member. The name is copied from the registry at that moment. |
| `librarian_id` | `char[20]` | The librarian who recorded the action (my own addition). |
| `action` | `char[10]` | `BORROWED`, `RETURNED` or `OVERDUE` (`GENESIS` for block 0). |
| `previous_hash` | `char[65]` | The hash of the block before this one. |
| `signature` | `unsigned char[72]` | ECDSA digital signature of the block data, stored with its length. |
| `hash` | `char[65]` | SHA-256 hash of all the fields above, including the signature. |

I copy the book title and member name into the block instead of only keeping the IDs. This way the block remembers exactly what was true at the time, even if the registry file changes later.

### 4.4 The genesis block and the chain

The chain always starts with a genesis block. It has index 0, the action `GENESIS`, and a previous hash made of 64 zeros, because there is no block before it. Every block after that stores the hash of the block before it in its `previous_hash` field. This is what makes it a chain:

```
Genesis → Borrow → Return → Borrow → Return
```

If one old block changes, its hash changes, and the next block no longer points to it correctly.

### 4.5 Borrowing a book

The librarian types a book ID and a member ID. The program checks both IDs in the registries. Then it checks if the book is already on loan, using `find_active_borrow()`. This function reads the chain from the newest block backwards and finds the last record for that book. If the last record is `BORROWED` (or `OVERDUE`), the book is still out and the program refuses. The blockchain itself is used to know the current state of each book, so no separate list is needed. If the book is free, the program calls `create_lending_block()`, which builds a new `BORROWED` block, signs it and calculates its hash. The block is added to the end of the chain and the chain is saved.

### 4.6 Returning a book

For a return, the program again checks both IDs. It then looks for the book's active `BORROWED` block. If there is none, because the book was never borrowed or was already returned, it prints an error. I also added one more rule: the member returning the book must be the member who borrowed it. If everything is correct, a `RETURNED` block is created with the member details from the original borrow block, then signed, hashed, added and saved.

### 4.7 Overdue loans

Menu option 3 looks at every book. If its last record is `BORROWED` and it is older than the loan period (120 seconds by default), the program adds an `OVERDUE` block. The book stays on loan until it is returned. A loan is only marked overdue once. The loan period can be changed with the `LOAN_PERIOD_SECONDS` environment variable.

### 4.8 Validating the chain

The function `validate_chain()` answers one question: **has anything been changed?** For every block it checks that:

1. the index matches the block's position,
2. the stored hash is the same as a freshly calculated hash,
3. the `previous_hash` matches the hash of the block before, and
4. the digital signature is valid (checked with the public key).

It also checks that block 0 is a real genesis block. If any check fails, the program prints which block failed and why. The hash is recalculated on a copy of the block, so validation never changes the chain it is checking.

### 4.9 Tamper detection

Option 6 (ADMIN only) shows tamper detection. It changes the book title in block 1 to `TAMPERED TITLE` in memory, without updating the hash or signature, and then validates the chain. The validation fails and reports block 1. The change is only made in memory, and the program never saves a chain that fails validation, so the file on disk stays clean. Restarting the program reloads the clean chain.

Tampering can also be shown by editing `data/chain.txt` in a text editor. The next time the program starts, or when `./library --verify` is run, it reports that the chain is invalid.

## 5. Security mechanisms

### 5.1 SHA-256 hashing

A hash is like a fingerprint for data. SHA-256 turns any data into a 64-character code. If even one letter of the data changes, the code becomes completely different. `create_transaction_data()` joins all the fields of a block together in a fixed order, and `calculate_hash()` runs SHA-256 over them and the signature. This hash is stored in the block, and the next block stores it as its previous hash. This is how the blocks are linked and how changes are detected.

### 5.2 Digital signatures (ECDSA)

Hashing shows that data has changed, but someone who changes a block could also calculate a new hash. Digital signatures stop this. The first time the program runs, `generate_key_pair()` creates a key pair on the P-256 curve using OpenSSL. The private key signs each lending block with `sign_data()`. The public key checks the signature with `verify_signature()`. Without the private key, nobody can make a valid signature for a changed block, so a rewritten block is caught even if its hash was updated.

The private key is saved in `data/key.pem`. It is encrypted with AES-256 using a passphrase, and the file can only be read by its owner. So a copy of the file alone is not enough to sign blocks.

The public key is saved separately, without encryption, in `data/pub.pem`. The program uses **only** this public key when it checks signatures, so checking the records never needs the secret key. Anyone can run `./library --verify` to check the whole chain without a passphrase or login. When the program starts, it also checks that `pub.pem` really belongs to the signing key, and stops if it does not.

### 5.3 Librarian login and roles

Before the menu appears, a librarian must log in with an ID and a PIN. The PINs are never stored as plain text. `librarians.txt` stores a PBKDF2-HMAC-SHA256 hash of each PIN, calculated 100,000 times and salted with the librarian ID, which makes guessing slow. The PIN is hidden while it is typed, and the comparison is done in constant time. After three wrong tries, the program prints "Access denied." and closes.

Each librarian has a role. A LIBRARIAN can borrow, return, view, validate and mark overdue loans. Only an ADMIN can run the tamper demonstration. The ID of the logged-in librarian is written into every block and is covered by the signature, so each block shows who recorded it.

## 6. Data persistence

All data is stored in plain text files in the `data` folder. The registries (`books.txt`, `members.txt`, `librarians.txt`) are read at start-up. The blockchain is stored in `chain.txt`, with one block per line and the fields separated by `|`:

```
index|timestamp|book_id|book_title|member_id|member_name|librarian_id|action|previous_hash|signature_hex|hash
```

The signature is written as hexadecimal text. The chain is saved after every borrow, return or overdue action, so no records are lost when the program closes. To avoid a broken file if the program crashes while saving, `save_chain()` first writes to a temporary file and then renames it over the real file. Renaming is a single step, so the file is either the old version or the new version, never half written.

When the program starts, `load_chain()` reads `chain.txt` and the chain is validated straight away. If the file does not exist, a new chain with a genesis block is created. The keys are also kept between runs, so old signatures can still be checked.

## 7. Error handling strategy

My approach is simple: **check everything before changing anything, and never leave the chain in a bad state.**

| Situation | What the program does |
| :---- | :---- |
| `books.txt` or `members.txt` missing or empty | Prints an error and stops. |
| A bad line in a registry file | Prints a warning with the line number and skips the line. |
| Unknown book ID or member ID | Prints `ERROR: Book or Member not found`. No block is added. |
| Borrowing a book that is already out | Prints an error. No block is added. |
| Returning a book that is not on loan, or by the wrong member | Prints an error. No block is added. |
| Wrong passphrase or wrong PIN | Prints an error. After three wrong PINs, access is denied. |
| `pub.pem` does not match the signing key | Prints an error and stops. |
| `chain.txt` cannot be read | Prints an error and stops, so a damaged file is not overwritten. |
| `chain.txt` was tampered with | Prints a warning with the bad block and the reason. An invalid chain is never saved. |
| Letters typed where a number is expected | Prints "Invalid choice." and shows the menu again. |
| Input closed (for example Ctrl+D) | Exits cleanly instead of looping. |
| Chain reaches its maximum size (1000 blocks) | Prints an error and refuses new blocks. |

Input is always read one full line at a time, and every text field is checked against its maximum size before it is copied. This stops long input from overflowing a buffer or spilling into the next question.

## 8. Screenshots of application execution

**1. Start-up.** The program loads 5 books, 5 members and 2 librarians, unlocks (or creates) the signing key, loads the saved blockchain (or starts a new one with the genesis block), and asks the librarian to log in.

<img width="1918" height="561" alt="Start-up" src="https://github.com/user-attachments/assets/e52db112-11af-441c-9ab0-adbfbd9ab9b4" />

**2. The menu.** Seven options: borrow, return, view records, validate, mark overdue loans, tamper demo and exit.

<img width="1920" height="1080" alt="Menu" src="https://github.com/user-attachments/assets/84364e74-133d-4b7c-9c63-df924bc6caec" />

**3. Borrowing and returning.** Every successful borrow or return is added as a signed block and saved to `data/chain.txt` straight away. Unknown IDs, a book that is already on loan, a return for a book that is not on loan, or a return by the wrong member print an ERROR and add nothing.

<img width="1920" height="1080" alt="Borrow and return" src="https://github.com/user-attachments/assets/1a028929-8354-4b43-b17a-e216dcc9ab8f" />

**4. Validation and tamper detection.** Option 4 prints "Blockchain is VALID" for an untouched chain. After option 6 changes Block #1, validation prints "Blockchain is INVALID - tampering detected!" with the block number and the reason.

<img width="1920" height="1080" alt="Validation and tamper detection" src="https://github.com/user-attachments/assets/275da27b-51ee-41cd-8b2f-796498d14335" />

**5. Checking the chain with only the public key (`./library --verify`).**

<!-- Add screenshot here -->

**6. Error when `books.txt` or `members.txt` is missing or empty.**

<!-- Add screenshot here -->

## 9. Testing

The blockchain is saved between runs, so run `rm -f data/chain.txt` before each command below to start from a clean chain. First set the passphrase once with `export LIBRARY_KEY_PASSPHRASE=demo-pass`. Every command starts by logging in as `LIB001` with PIN `1234`.

| Command | What it checks |
| :---- | :---- |
| `printf 'LIB001\n0000\nLIB001\n0000\nLIB001\n0000\n' \| ./library` | A wrong PIN three times prints "Access denied." and exits. |
| `printf 'LIB001\n1234\n1\nBK001\nALU001\n7\n' \| ./library` | A normal borrow works. |
| `printf 'LIB001\n1234\n1\nBK001\nALU001\n1\nBK001\nALU002\n7\n' \| ./library` | A book that is already on loan cannot be borrowed again. |
| `printf 'LIB001\n1234\n1\nBK999\nALU001\n1\nBK001\nALU999\n7\n' \| ./library` | An unknown book and an unknown member are both rejected with "ERROR: Book or Member not found". |
| `printf 'LIB001\n1234\n1\nBK001\nALU001\n2\nBK001\nALU002\n2\nBK001\nALU001\n7\n' \| ./library` | Only the member who borrowed a book can return it. |
| `printf 'LIB001\n1234\n2\nBK001\nALU001\n7\n' \| ./library` | Returning a book that was never borrowed prints an error. |
| `printf 'LIB001\n1234\n1\nBK001\nALU001\n5\n3\n7\n' \| LOAN_PERIOD_SECONDS=0 ./library` | An OVERDUE block is added and shown with a VALID signature. |
| `printf 'LIB001\n1234\n1\nBK001\nALU001\n6\n4\n7\n' \| ./library` | Tampering is detected and stays detected. |
| `printf 'LIB002\n5678\n6\n7\n' \| ./library` | A LIBRARIAN cannot run the tamper demo; only an ADMIN can. |
| `printf 'LIB001\n1234\nabc\n7\n' \| ./library` | Letters in the menu are ignored and the program does not crash. |
| `printf '' \| ./library` | The program exits cleanly when there is no input. |
| `./library --verify` | The saved chain is checked with only the public key. |

**Test results:**

<img width="1920" height="1080" alt="Test results 1" src="https://github.com/user-attachments/assets/525eacc9-b608-4172-a64e-fad79f377514" />

<img width="1920" height="1080" alt="Test results 2" src="https://github.com/user-attachments/assets/e8649ab8-aa9b-4167-88a8-a8a881c32cd4" />

<img width="1210" height="495" alt="Test results 3" src="https://github.com/user-attachments/assets/922f0c14-7ff0-43dd-ad07-5de445488964" />

## 10. Challenges encountered and solutions

**Validation was changing the data it checked.** At first, to check a block, I recalculated its hash directly on the block. This overwrote the stored hash, so a changed block could look correct. I fixed this by recalculating the hash on a copy of the block and comparing the copy with the original. Now validation only reads the chain and never changes it.

**Signatures do not always have the same length.** An ECDSA signature is usually 70 to 72 bytes, not a fixed size. If the program always read 72 bytes, it would check extra bytes that are not part of the signature, and verification would fail. I solved this by storing the real length of each signature next to it, and by refusing any signature longer than 72 bytes.

**Records and keys were lost between runs.** In my first version, the chain was only kept in memory and a new key was made every time the program started. When the program closed, all records were gone, and old signatures could not be checked with the new key. I now save the chain to `chain.txt` after every action, and I save the key to a file and load it again on the next run. To protect it, the key file is encrypted with a passphrase and only the owner can read it.

**Checking the chain needed the secret key.** The program first took the public key from the encrypted private key file, so nobody could check the records without the passphrase. I fixed this by saving the public key in its own file, `pub.pem`, and using only that file for checking. I also added `./library --verify` so anyone can check the chain.

**A crash while saving could break the chain file.** If the program stopped in the middle of writing `chain.txt`, the file would be half written. I solved this by writing to a temporary file first and then renaming it, which happens in one step.

**The tamper demo could damage the real records.** Once I added saving, there was a new risk: after the demo changed a block, the next save would have written the tampered chain to disk. I solved this by making the program validate the chain before every save and refuse to save an invalid chain.

**Special characters in the data files.** The chain file uses `|` to separate fields, so a title containing `|` would break it. The registry loader now rejects such lines. It also removes extra spaces and handles Windows line endings.

## 11. System design diagram

The diagram shows three things. First, how the registries are loaded at start-up and used to check every book ID and member ID before a block is made. Second, how each block points to the block before it through its `previous_hash`, starting from the genesis block. Third, what is inside one block. It also shows the steps of the lending flow (check IDs, check the book's history, build, sign, hash, add, save) and the three checks done when the chain is validated.

```mermaid
flowchart TD
    START(["Program starts"]) --> REG

    subgraph REG["1. Registries loaded at start-up"]
        direction LR
        BK["books.txt<br/>book_id, title, author"]
        MB["members.txt<br/>member_id, full_name, course_code"]
        LB["librarians.txt<br/>librarian_id, name, role, PIN hash"]
    end

    REG --> KEY["Unlock signing key<br/>key.pem + passphrase<br/>check pub.pem matches"]
    KEY --> LOAD["Load chain.txt<br/>or create the Genesis Block"]
    LOAD --> LOGIN{"Librarian login<br/>ID and PIN correct?"}
    LOGIN -- "No, after 3 tries" --> DENY(["Access denied"])
    LOGIN -- "Yes" --> MENU["Menu<br/>Borrow, Return, View, Validate, Overdue, Tamper demo"]

    MENU -- "Borrow or Return" --> ASK["Enter Book ID and Member ID"]
    ASK --> CHECK{"Both IDs found<br/>in the registries?"}
    CHECK -- "No" --> ERR1["ERROR: Book or Member not found<br/>nothing is added"]
    CHECK -- "Yes" --> STATE{"Does the book's history<br/>on the chain allow it?"}
    STATE -- "No" --> ERR2["ERROR: already on loan<br/>or not on loan"]
    STATE -- "Yes" --> NEW["Build a new block<br/>copy book title and member name"]
    NEW --> SIGN["Sign the block data<br/>ECDSA with the private key"]
    SIGN --> HASH["SHA-256 hash of all fields<br/>and the signature"]
    HASH --> APPEND["Add the block to the end of the chain"]
    APPEND --> SAVE["Validate, then save to chain.txt"]

    APPEND -.-> CHAIN
    subgraph CHAIN["2. The blockchain: each block points to the one before it"]
        direction LR
        G["Block 0: GENESIS<br/>previous_hash = 64 zeros<br/>hash = H0"]
        B1["Block 1: BORROWED<br/>previous_hash = H0<br/>hash = H1"]
        B2["Block 2: RETURNED<br/>previous_hash = H1<br/>hash = H2"]
        G --> B1 --> B2
    end

    B1 -.-> FIELDS
    subgraph BLOCK["3. Inside one block"]
        FIELDS["index, timestamp<br/>book_id, book_title<br/>member_id, member_name<br/>librarian_id, action<br/>previous_hash<br/>signature: ECDSA<br/>hash: SHA-256"]
    end

    MENU -- "Validate" --> VAL["Check every block<br/>1. recompute its hash<br/>2. compare previous_hash links<br/>3. verify signature with pub.pem"]
    VAL --> OK(["VALID"])
    VAL --> BAD(["INVALID: block number and reason"])
```

## 12. Limitations

The whole chain file is rewritten after every action. This is fine for a small library but would be slow for a very large one.

There is one signing key for the whole system. The key proves that a block came from this program, and the signed librarian ID shows who made it, but separate keys for each librarian would be stronger. The genesis block is not signed, because it holds no lending data.

The PIN login protects the menu, but anyone who can edit `data/librarians.txt` could add an account. A real system would keep these files on a server that only administrators can change.

## 13. Conclusion

This project shows how a blockchain can make library records trustworthy. The registries decide which books and members are valid. The blockchain keeps a full history that is never overwritten, only added to. SHA-256 links the blocks so that any change is visible, and digital signatures prove that each block was made by the system and by a logged-in librarian. The validation step can check the whole history at any time, using only the public key.

In short: **the registry tells the system what is valid, the blockchain keeps the history, cryptography protects the records, and validation checks whether the history has been changed.**
