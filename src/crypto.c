#define _POSIX_C_SOURCE 200809L   /* open, fchmod, fdopen */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/pem.h>
#include <openssl/crypto.h>
#include <openssl/sha.h>

#include "crypto.h"

EVP_PKEY *generate_key_pair(void)
{
    EVP_PKEY_CTX *ctx;
    EVP_PKEY *key_pair = NULL;

    ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (ctx == NULL) {
        return NULL;
    }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return NULL;
    }

    if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return NULL;
    }

    if (EVP_PKEY_keygen(ctx, &key_pair) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return NULL;
    }

    EVP_PKEY_CTX_free(ctx);

    return key_pair;
}

int sign_data(
    EVP_PKEY *private_key,
    const unsigned char *data,
    size_t data_len,
    unsigned char *signature,
    size_t *signature_len
)
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        return 0;
    }


    if (EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, private_key) <= 0) {
        EVP_MD_CTX_free(ctx);
        return 0;
    }

    if (EVP_DigestSignUpdate(ctx, data, data_len) <= 0) {
        EVP_MD_CTX_free(ctx);
        return 0;
    }

    if (EVP_DigestSignFinal(ctx, NULL, signature_len) <= 0) {
        EVP_MD_CTX_free(ctx);
        return 0;
    }

    if (*signature_len > 72) {
        EVP_MD_CTX_free(ctx);
        return 0;
    }

    if (EVP_DigestSignFinal(ctx, signature, signature_len) <= 0) {
        EVP_MD_CTX_free(ctx);
        return 0;
    }

    EVP_MD_CTX_free(ctx);
    return 1;
}

int verify_signature(
    EVP_PKEY *public_key,
    const unsigned char *data,
    size_t data_len,
    const unsigned char *signature,
    size_t signature_len
)
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        return 0;
    }

    if (EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, public_key) <= 0) {
        EVP_MD_CTX_free(ctx);
        return 0;
    }

    if (EVP_DigestVerifyUpdate(ctx, data, data_len) <= 0) {
        EVP_MD_CTX_free(ctx);
        return 0;
    }

    int result = EVP_DigestVerifyFinal(ctx, signature, signature_len);

    EVP_MD_CTX_free(ctx);

    return (result == 1) ? 1 : 0;
}


/* The key pair is stored as AES-256 encrypted PEM. */
int save_key(EVP_PKEY *key_pair, const char *filename, const char *passphrase)
{
    /* Owner-only from the start. */
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return 0;
    }
    fchmod(fd, 0600);   /* fix old permissions */

    FILE *file = fdopen(fd, "w");
    if (file == NULL) {
        close(fd);
        return 0;
    }

    int ok = PEM_write_PrivateKey(file, key_pair, EVP_aes_256_cbc(),
                                  (unsigned char *)passphrase, (int)strlen(passphrase),
                                  NULL, NULL);
    if (fclose(file) != 0) {
        ok = 0;
    }
    return ok == 1;
}

/* OpenSSL calls this only for encrypted keys. */
struct passphrase_request {
    const char *passphrase;
    int asked;
};

static int passphrase_callback(char *buf, int size, int rwflag, void *userdata)
{
    (void)rwflag;
    struct passphrase_request *request = userdata;
    request->asked = 1;

    int len = (int)strlen(request->passphrase);
    if (len > size) {
        len = size;
    }
    memcpy(buf, request->passphrase, (size_t)len);
    return len;
}

int load_key(const char *filename, const char *passphrase, EVP_PKEY **key_out)
{
    *key_out = NULL;

    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        return KEY_MISSING;
    }

    struct passphrase_request request = { passphrase, 0 };
    *key_out = PEM_read_PrivateKey(file, NULL, passphrase_callback, &request);
    fclose(file);

    if (*key_out == NULL) {
        return KEY_BAD;
    }
    return request.asked ? KEY_LOADED : KEY_PLAINTEXT;
}

int save_public_key(EVP_PKEY *key, const char *filename)
{
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        return 0;
    }

    int ok = PEM_write_PUBKEY(file, key);
    if (fclose(file) != 0) {
        ok = 0;
    }
    return ok == 1;
}

/* Returns NULL if the file is missing or is not a public key. */
EVP_PKEY *load_public_key(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        return NULL;
    }

    EVP_PKEY *key = PEM_read_PUBKEY(file, NULL, NULL, NULL);
    fclose(file);
    return key;
}

#define PIN_HASH_ITERATIONS 100000

int hash_pin(const char *librarian_id, const char *pin, char out_hex[65])
{
    unsigned char digest[32];

    if (PKCS5_PBKDF2_HMAC(pin, (int)strlen(pin),
                          (const unsigned char *)librarian_id, (int)strlen(librarian_id),
                          PIN_HASH_ITERATIONS, EVP_sha256(),
                          (int)sizeof(digest), digest) != 1) {
        return 0;
    }

    for (size_t i = 0; i < sizeof(digest); i++) {
        sprintf(&out_hex[i * 2], "%02x", digest[i]);
    }
    out_hex[64] = '\0';
    return 1;
}

int verify_pin(const char *librarian_id, const char *pin, const char *stored_hex)
{
    char computed[65];

    if (strlen(stored_hex) != 64 || !hash_pin(librarian_id, pin, computed)) {
        return 0;
    }
    /* Constant-time compare. */
    return CRYPTO_memcmp(computed, stored_hex, 64) == 0;
}

void sha256_hex(const void *data, size_t data_len, char out_hex[65])
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char *)data, data_len, digest);

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(&out_hex[i * 2], "%02x", digest[i]);
    }
    out_hex[64] = '\0';
}
