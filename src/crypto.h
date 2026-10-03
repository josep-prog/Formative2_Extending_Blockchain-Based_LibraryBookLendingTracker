#ifndef CRYPTO_H
#define CRYPTO_H

#include <openssl/evp.h>
#include <stddef.h>

/* Result of load_key(). */
#define KEY_LOADED     0   /* encrypted key loaded with the given passphrase   */
#define KEY_MISSING    1   /* no key file yet                                  */
#define KEY_BAD        2   /* wrong passphrase, or the file is not a valid key */
#define KEY_PLAINTEXT  3   /* loaded, but the file was not encrypted           */

#define MIN_PASSPHRASE_LENGTH 4

EVP_PKEY *generate_key_pair(void);

int save_key(EVP_PKEY *key_pair, const char *filename, const char *passphrase);
int load_key(const char *filename, const char *passphrase, EVP_PKEY **key_out);

/* The public half is stored unencrypted so anyone can verify signatures. */
int save_public_key(EVP_PKEY *key, const char *filename);
EVP_PKEY *load_public_key(const char *filename);

int sign_data(
    EVP_PKEY *private_key,
    const unsigned char *data,
    size_t data_len,
    unsigned char *signature,
    size_t *signature_len
);

int verify_signature(
    EVP_PKEY *public_key,
    const unsigned char *data,
    size_t data_len,
    const unsigned char *signature,
    size_t signature_len
);

/* PBKDF2-HMAC-SHA256 of the PIN, salted with the librarian ID. */
int hash_pin(const char *librarian_id, const char *pin, char out_hex[65]);
int verify_pin(const char *librarian_id, const char *pin, const char *stored_hex);

/* SHA-256 of any buffer as 64 lowercase hex characters. */
void sha256_hex(const void *data, size_t data_len, char out_hex[65]);

#endif
