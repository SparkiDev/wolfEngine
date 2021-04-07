/* we_rsa.c
 *
 * Copyright (C) 2019-2021 wolfSSL Inc.
 *
 * This file is part of wolfengine.
 *
 * wolfengine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfengine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#include <wolfengine/we_internal.h>

#ifdef WE_HAVE_RSA

/* Maximum DER digest size, taken from wolfSSL. Sum of the maximum size of the
   encoded digest, algorithm tag, and sequence tag. */
#define MAX_DER_DIGEST_SZ 98
/* The default RSA key/modulus size in bits. */
#define DEFAULT_KEY_BITS 2048
/* The default RSA public exponent, e. */
#define DEFAULT_PUB_EXP WC_RSA_EXPONENT

/**
 * Data required to complete an RSA operation.
 */
typedef struct we_Rsa
{
    /* wolfSSL structure for holding RSA key data. */
    RsaKey key;
    /* Stored by control command EVP_PKEY_CTRL_MD. */
    const EVP_MD *md;
    /* Stored by string control command "rsa_mgf1_md". */
    const EVP_MD *mdMGF1;
    /* Padding mode */
    int padMode;
    /* The public exponent ("e"). */
    long pubExp;
    /* The key/modulus size in bits. */
    int bits;
    /* Length of salt to use with PSS. */
    int saltLen;
    /* Indicates private key has been set into wolfSSL structure. */
    int privKeySet:1;
    /* Indicates public key has been set into wolfSSL structure. */
    int pubKeySet:1;
} we_Rsa;

/** RSA direct method - RSA using wolfSSL for the implementation. */
RSA_METHOD *we_rsa_method = NULL;

/**
 * Set the public key in a we_Rsa structure.
 *
 * @param  ctx     [in]  Public key context of operation.
 * @param  rsa     [in]  RSA structure to hold public key.
 * @returns  1 on success and 0 on failure.
 */
static int we_set_public_key(RSA *rsaKey, we_Rsa *engineRsa)
{
    int ret = 1;
    int rc = 0;
    unsigned char *pubDer = NULL;
    int pubDerLen = 0;
    word32 idx = 0;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_set_public_key");

    pubDerLen = i2d_RSAPublicKey(rsaKey, &pubDer);
    if (pubDerLen == 0) {
        WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "i2d_RSAPublicKey", pubDerLen);
        ret = 0;
    }

    if (ret == 1) {
        rc = wc_RsaPublicKeyDecode(pubDer, &idx, &engineRsa->key,
                                   pubDerLen);
        if (rc != 0) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaPublicKeyDecode", rc);
            ret = 0;
        }
    }

    if (ret == 1) {
        engineRsa->pubKeySet = 1;
    }

    if (pubDer != NULL) {
        OPENSSL_free(pubDer);
    }

    WOLFENGINE_LEAVE(WE_LOG_PK, "we_set_public_key", ret);

    return ret;
}

/**
 * Set the private key in a we_Rsa structure.
 *
 * @param  ctx     [in]  Public key context of operation.
 * @param  rsa     [in]  RSA structure to hold private key.
 * @returns  1 on success and 0 on failure.
 */
static int we_set_private_key(RSA *rsaKey, we_Rsa *engineRsa)
{
    int ret = 1;
    int rc = 0;
    unsigned char *privDer = NULL;
    int privDerLen = 0;
    word32 idx = 0;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_set_private_key");

    privDerLen = i2d_RSAPrivateKey(rsaKey, &privDer);
    if (privDerLen == 0) {
        WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "i2d_RSAPrivateKey", privDerLen);
        ret = 0;
    }

    if (ret == 1) {
        rc = wc_RsaPrivateKeyDecode(privDer, &idx, &engineRsa->key,
                                    privDerLen);
        if (rc != 0) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaPrivateKeyDecode", rc);
            ret = 0;
        }
    }

    if (ret == 1) {
        engineRsa->privKeySet = 1;
    }

    if (privDer != NULL) {
        OPENSSL_clear_free(privDer, privDerLen);
    }

    WOLFENGINE_LEAVE(WE_LOG_PK, "we_set_private_key", ret);

    return ret;
}

/**
 * Initialize and set the data required to complete an RSA operation.
 *
 * @param  rsa  [in]  RSA context of operation.
 * @returns  1 on success and 0 on failure.
 */
static int we_rsa_init(RSA *rsa)
{
    int ret = 1;
    int rc = 0;
    we_Rsa *engineRsa;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_init");

    engineRsa = (we_Rsa *)OPENSSL_zalloc(sizeof(we_Rsa));
    if (engineRsa == NULL) {
        WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "OPENSSL_zalloc", engineRsa);
        ret = 0;
    }

    if (ret == 1) {
        rc = wc_InitRsaKey(&engineRsa->key, NULL);
        if (rc != 0) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_InitRsaKey", rc);
            ret = 0;
        }
    }

#ifdef WC_RSA_BLINDING
    if (ret == 1) {
        rc = wc_RsaSetRNG(&engineRsa->key, we_rng);
        if (rc != 0) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaSetRNG", rc);
            ret = 0;
        }
    }
#endif /* WC_RSA_BLINDING */

    if (ret == 1) {
        rc = RSA_set_ex_data(rsa, WE_RSA_EX_DATA_IDX, engineRsa);
        if (rc != 1) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "RSA_set_ex_data", rc);
            ret = 0;
        }
    }

    if (ret == 0 && engineRsa != NULL) {
        wc_FreeRsaKey(&engineRsa->key);
        OPENSSL_free(engineRsa);
    }

    return ret;
}

/**
 * Clean up the RSA operation data.
 *
 * @param  rsa  [in]  RSA context of operation.
 * @returns  1 on success and 0 on failure.
 */
static int we_rsa_finish(RSA *rsa)
{
    we_Rsa *engineRsa;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_finish");

    engineRsa = (we_Rsa *)RSA_get_ex_data(rsa, WE_RSA_EX_DATA_IDX);
    if (engineRsa != NULL) {
        wc_FreeRsaKey(&engineRsa->key);
        OPENSSL_free(engineRsa);
        RSA_set_ex_data(rsa, WE_RSA_EX_DATA_IDX, NULL);
    }

    WOLFENGINE_LEAVE(WE_LOG_PK, "we_rsa_finish", 1);

    return 1;
}

/**
 * Perform an RSA public encryption operation.
 *
 * @param  fromLen  [in]   Length of buffer to encrypt.
 * @param  from     [in]   Buffer to encrypt.
 * @param  to       [out]  Buffer to place ciphertext in.
 * @param  rsa      [in]   RSA context of operation.
 * @param  padding  [in]   Type of padding to use.
 * @returns  Length of ciphertext on success and -1 on failure.
 */
static int we_rsa_pub_enc(int fromLen, const unsigned char *from,
                          unsigned char *to, RSA *rsa, int padding)
{
    int ret = 1;
    int rc = 0;
    we_Rsa *engineRsa = NULL;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_pub_enc");

    engineRsa = (we_Rsa *)RSA_get_ex_data(rsa, WE_RSA_EX_DATA_IDX);
    if (engineRsa == NULL) {
        WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "RSA_get_ex_data", engineRsa);
        ret = -1;
    }

    if (ret == 1 && !engineRsa->pubKeySet) {
        rc = we_set_public_key(rsa, engineRsa);
        if (rc == 0) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "we_set_public_key", rc);
            ret = -1;
        }
    }

    if (ret == 1) {
        switch (padding) {
            case RSA_PKCS1_PADDING:
                /* PKCS 1 v1.5 padding using block type 2. */
                rc = wc_RsaPublicEncrypt(from, fromLen, to, RSA_size(rsa),
                                         &engineRsa->key, we_rng);
                if (rc < 0) {
                    WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaPublicEncrypt", rc);
                    ret = -1;
                }
                else {
                    ret = rc;
                }
                break;
            case RSA_PKCS1_OAEP_PADDING:
                /* OAEP padding using SHA-1, MGF1. */
                rc = wc_RsaPublicEncrypt_ex(from, fromLen, to, RSA_size(rsa),
                                            &engineRsa->key, we_rng,
                                            WC_RSA_OAEP_PAD, WC_HASH_TYPE_SHA,
                                            WC_MGF1SHA1, NULL, 0);
                if (rc < 0) {
                    WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaPublicEncrypt_ex",
                                          rc);
                    ret = -1;
                }
                else {
                    ret = rc;
                }
                break;
            case RSA_NO_PADDING:
                rc = wc_RsaPublicEncrypt_ex(from, fromLen, to, RSA_size(rsa),
                                            &engineRsa->key, we_rng,
                                            WC_RSA_NO_PAD, WC_HASH_TYPE_NONE, 0,
                                            NULL, 0);
                if (rc < 0) {
                    WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaPublicEncrypt_ex",
                                          rc);
                    ret = -1;
                }
                else {
                    ret = rc;
                }
                break;
            default:
                WOLFENGINE_ERROR_MSG(WE_LOG_PK,
                                     "we_rsa_pub_enc: unknown padding");
                ret = -1;
        }
    }

    WOLFENGINE_LEAVE(WE_LOG_PK, "we_rsa_pub_enc", ret);

    return ret;
}

/**
 * Perform an RSA private decryption operation.
 *
 * @param  fromLen  [in]   Length of buffer to decrypt.
 * @param  from     [in]   Buffer to decrypt.
 * @param  to       [out]  Buffer to place plaintext in.
 * @param  rsa      [in]   RSA context of operation.
 * @param  padding  [in]   Type of padding to use.
 * @returns  Length of plaintext on success and -1 on failure.
 */
static int we_rsa_priv_dec(int fromLen, const unsigned char *from,
                           unsigned char *to, RSA *rsa, int padding)
{
    int ret = 1;
    int rc = 0;
    we_Rsa *engineRsa = NULL;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_priv_dec");

    engineRsa = (we_Rsa *)RSA_get_ex_data(rsa, WE_RSA_EX_DATA_IDX);
    if (engineRsa == NULL) {
        WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "RSA_get_ex_data", engineRsa);
        ret = -1;
    }

    if (ret == 1 && !engineRsa->privKeySet) {
        rc = we_set_private_key(rsa, engineRsa);
        if (rc == 0) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "we_set_private_key", rc);
            ret = -1;
        }
    }

    if (ret == 1) {
        switch (padding) {
            case RSA_PKCS1_PADDING:
                /* PKCS 1 v1.5 padding using block type 2. */
                rc = wc_RsaPrivateDecrypt(from, fromLen, to, RSA_size(rsa),
                                          &engineRsa->key);
                if (rc < 0) {
                    WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaPrivateDecrypt",
                                          rc);
                    ret = -1;
                }
                else {
                    ret = rc;
                }
                break;
            case RSA_PKCS1_OAEP_PADDING:
                /* OAEP padding using SHA-1, MGF1. */
                rc = wc_RsaPrivateDecrypt_ex(from, fromLen, to, RSA_size(rsa),
                                             &engineRsa->key,  WC_RSA_OAEP_PAD,
                                             WC_HASH_TYPE_SHA, WC_MGF1SHA1,
                                             NULL, 0);
                if (rc < 0) {
                    WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaPrivateDecrypt_ex",
                                          rc);
                    ret = -1;
                }
                else {
                    ret = rc;
                }
                break;
            case RSA_NO_PADDING:
                rc = wc_RsaPrivateDecrypt_ex(from, fromLen, to, RSA_size(rsa),
                                             &engineRsa->key, WC_RSA_NO_PAD,
                                             WC_HASH_TYPE_NONE, 0, NULL, 0);
                if (rc < 0) {
                    WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaPrivateDecrypt_ex",
                                          rc);
                    ret = -1;
                }
                else {
                    ret = rc;
                }
                break;
            default:
                WOLFENGINE_ERROR_MSG(WE_LOG_PK,
                                     "we_rsa_priv_dec: unknown padding");
                ret = -1;
        }
    }

    WOLFENGINE_LEAVE(WE_LOG_PK, "we_rsa_priv_dec", ret);

    return ret;
}

/**
 * Convert an OpenSSL hash NID to a wolfSSL MGF1 algorithm.
 *
 * @param  nid  [in]  OpenSSL hash NID to convert.
 * @returns  wolfSSL MGF1 algorithm or WC_MGF1NONE on failure.
 */
static int we_mgf_from_hash(int nid)
{
    int mgf;

    switch (nid) {
        case NID_sha1:
            mgf = WC_MGF1SHA1;
            break;
        case NID_sha224:
            mgf = WC_MGF1SHA224;
            break;
        case NID_sha256:
            mgf = WC_MGF1SHA256;
            break;
        case NID_sha384:
            mgf = WC_MGF1SHA384;
            break;
        case NID_sha512:
            mgf = WC_MGF1SHA512;
            break;
        default:
            mgf = WC_MGF1NONE;
            break;
    }

    return mgf;
}

/**
 * Internal function for performing an RSA private encryption operation.
 *
 * @param  fromLen  [in]   Length of buffer to encrypt.
 * @param  from     [in]   Buffer to encrypt.
 * @param  toLen    [in]   Size of ciphertext buffer.
 * @param  to       [out]  Buffer to place ciphertext in.
 * @param  key      [in]   RSA key to use.
 * @param  padding  [in]   Type of padding to use.
 * @returns  Length of ciphertext on success and -1 on failure.
 */
static int we_rsa_priv_enc_int(size_t fromLen, const unsigned char *from,
                               size_t toLen, unsigned char *to, we_Rsa *rsa)
{
    int ret = 1;
    int rc = 0;
    unsigned int tLen = (unsigned int)toLen;
    const EVP_MD *mdMGF1;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_priv_enc_int");

    switch (rsa->padMode) {
        case RSA_PKCS1_PADDING:
            /* PKCS 1 v1.5 padding using block type 1. */
            rc = wc_RsaSSL_Sign(from, (word32)fromLen, to, (word32)toLen,
                    &rsa->key, we_rng);
            if (rc < 0) {
                WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaSSL_Sign", rc);
                ret = -1;
            }
            else {
                ret = rc;
            }
            break;
        case RSA_NO_PADDING:
            rc = wc_RsaDirect((byte*)from, (unsigned int)fromLen, to, &tLen,
                              &rsa->key, RSA_PRIVATE_ENCRYPT, we_rng);
            if (rc < 0) {
                WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaDirect", rc);
                ret = -1;
            }
            else {
                ret = rc;
            }
            break;
        case RSA_PKCS1_PSS_PADDING:
            mdMGF1 = rsa->mdMGF1 != NULL ? rsa->mdMGF1 : rsa->md;
            rc = wc_RsaPSS_Sign_ex(from, (word32)fromLen, to,
                (word32)toLen,
                we_nid_to_wc_hash_type(EVP_MD_type(rsa->md)),
                we_mgf_from_hash(EVP_MD_type(mdMGF1)), rsa->saltLen,
                &rsa->key, we_rng);
            if (rc < 0) {
                WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaPSS_Sign_ex", rc);
                ret = -1;
            }
            else {
                ret = rc;
            }
            break;
        default:
            WOLFENGINE_ERROR_MSG(WE_LOG_PK,
                                 "we_rsa_priv_enc_int: unknown padding");
            ret = -1;
    }

    WOLFENGINE_LEAVE(WE_LOG_PK, "we_rsa_priv_enc_int", ret);

    return ret;
}

/**
 * Perform an RSA private encryption operation.
 *
 * @param  fromLen  [in]   Length of buffer to encrypt.
 * @param  from     [in]   Buffer to encrypt.
 * @param  to       [out]  Buffer to place ciphertext in.
 * @param  rsa      [in]   RSA context of operation.
 * @param  padding  [in]   Type of padding to use.
 * @returns  Length of ciphertext on success and -1 on failure.
 */
static int we_rsa_priv_enc(int fromLen, const unsigned char *from,
                           unsigned char *to, RSA *rsa, int padding)
{
    int ret = 1;
    int rc = 0;
    we_Rsa *engineRsa = NULL;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_priv_enc");

    engineRsa = (we_Rsa *)RSA_get_ex_data(rsa, WE_RSA_EX_DATA_IDX);
    if (engineRsa == NULL) {
        WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "RSA_get_ex_data", engineRsa);
        ret = -1;
    }

    if (ret == 1 && !engineRsa->privKeySet) {
        rc = we_set_private_key(rsa, engineRsa);
        if (rc == 0) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "we_set_private_key", rc);
            ret = -1;
        }
    }

    if (ret == 1) {
        engineRsa->padMode = padding;
        ret = we_rsa_priv_enc_int(fromLen, from, RSA_size(rsa), to, engineRsa);
        if (ret == -1) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "we_rsa_priv_enc_int", ret);
        }
    }

    WOLFENGINE_LEAVE(WE_LOG_PK, "we_rsa_priv_enc", ret);

    return ret;
}

/**
 * Internal function for performing an RSA public decryption operation.
 *
 * @param  fromLen  [in]   Length of buffer to decrypt.
 * @param  from     [in]   Buffer to decrypt.
 * @param  toLen    [in]   Size of plaintext buffer.
 * @param  to       [out]  Buffer to place plaintext in.
 * @param  key      [in]   RSA key to use.
 * @param  padding  [in]   Type of padding to use.
 * @returns  Length of plaintext on success and -1 on failure.
 */
static int we_rsa_pub_dec_int(size_t fromLen, const unsigned char *from,
                              size_t toLen, unsigned char *to, we_Rsa *rsa)
{
    int ret = 1;
    int rc = 0;
    unsigned int tLen = (unsigned int)toLen;
    const EVP_MD *mdMGF1;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_pub_dec_int");

    switch (rsa->padMode) {
        case RSA_PKCS1_PADDING:
            /* PKCS #1 v1.5 padding using block type 1. */
            rc = wc_RsaSSL_Verify(from, (word32)fromLen, to, (word32)toLen,
                    &rsa->key);
            if (rc < 0) {
                WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaSSL_Verify", rc);
                ret = -1;
            }
            else {
                ret = rc;
            }
            break;
        case RSA_NO_PADDING:
            rc = wc_RsaDirect((byte*)from, (unsigned int)fromLen, to, &tLen,
                              &rsa->key, RSA_PUBLIC_DECRYPT, we_rng);
            if (rc < 0) {
                WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaDirect", rc);
                ret = -1;
            }
            else {
                ret = rc;
            }
            break;
        case RSA_PKCS1_PSS_PADDING:
            /* PKCS #1 PSS padding. */
            mdMGF1 = rsa->mdMGF1 != NULL ? rsa->mdMGF1 : rsa->md;
            rc = wc_RsaPSS_Verify_ex((byte*)from, (word32)fromLen, to,
                (word32)toLen, we_nid_to_wc_hash_type(EVP_MD_type(rsa->md)),
                we_mgf_from_hash(EVP_MD_type(mdMGF1)), rsa->saltLen,
                &rsa->key);
            if (rc < 0) {
                WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaPSS_Verify_ex", rc);
                ret = -1;
            } else {
                ret = rc;
            }
            break;
        default:
            WOLFENGINE_ERROR_MSG(WE_LOG_PK,
                                 "we_rsa_pub_dec_int: unknown padding");
            ret = -1;
    }

    WOLFENGINE_LEAVE(WE_LOG_PK, "we_rsa_pub_dec_int", ret);

    return ret;
}

/**
 * Perform an RSA public decryption operation.
 *
 * @param  fromLen  [in]   Length of buffer to decrypt.
 * @param  from     [in]   Buffer to decrypt.
 * @param  to       [out]  Buffer to place plaintext in.
 * @param  rsa      [in]   RSA context of operation.
 * @param  padding  [in]   Type of padding to use.
 * @returns  Length of plaintext on success and -1 on failure.
 */
static int we_rsa_pub_dec(int fromLen, const unsigned char *from,
                          unsigned char *to, RSA *rsa, int padding)
{
    int ret = 1;
    int rc = 0;
    we_Rsa *engineRsa = NULL;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_pub_dec");

    engineRsa = (we_Rsa *)RSA_get_ex_data(rsa, WE_RSA_EX_DATA_IDX);
    if (engineRsa == NULL) {
        WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "RSA_get_ex_data", engineRsa);
        ret = -1;
    }

    if (ret == 1 && !engineRsa->pubKeySet) {
        rc = we_set_public_key(rsa, engineRsa);
        if (rc == 0) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "we_set_public_key", rc);
            ret = -1;
        }
    }

    if (ret == 1) {
        engineRsa->padMode = padding;
        ret = we_rsa_pub_dec_int(fromLen, from, RSA_size(rsa), to, engineRsa);
        if (ret == -1) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "we_rsa_pub_dec_int", ret);
        }
    }

    WOLFENGINE_LEAVE(WE_LOG_PK, "we_rsa_pub_dec", ret);

    return ret;
}

static int we_convert_rsa(RsaKey *wolfKey, RSA *osslKey)
{
    int ret = 1;
    int rc = 0;
    int derLen = 0;
    unsigned char *der = NULL;
    const unsigned char *derPtr = NULL;
    RSA *decodedRsa = NULL;
    const BIGNUM *e = NULL, *n = NULL, *d = NULL, *p = NULL, *q = NULL,
                 *dmp1 = NULL, *dmq1 = NULL, *iqmp = NULL;
    BIGNUM *eDup = NULL, *nDup = NULL, *dDup = NULL, *pDup = NULL, *qDup = NULL,
           *dmp1Dup = NULL, *dmq1Dup = NULL, *iqmpDup = NULL;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_convert_rsa");

    derLen = wc_RsaKeyToDer(wolfKey, NULL, 0);
    if (derLen <= 0) {
        WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaKeyToDer", derLen);
        ret = 0;
    }

    if (ret == 1) {
        der = (unsigned char *)OPENSSL_malloc(derLen);
        if (der == NULL) {
            WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "OPENSSL_malloc", der);
            ret = 0;
        }
    }

    if (ret == 1) {
        derLen = wc_RsaKeyToDer(wolfKey, der, derLen);
        if (derLen <= 0) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaKeyToDer", derLen);
            ret = 0;
        }
    }

    if (ret == 1) {
        /*
         * The pointer passed to d2i_RSAPrivateKey will get advanced to the
         * end of the buffer, so we save the original pointer in order to free
         * the buffer later.
         */
        derPtr = (const unsigned char *)der;
        decodedRsa = d2i_RSAPrivateKey(NULL, &derPtr, derLen);
        if (decodedRsa == NULL) {
            WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "d2i_RSAPrivateKey",
                                       decodedRsa);
            ret = 0;
        }
    }

    if (ret == 1) {
        RSA_get0_key(decodedRsa, &n, &e, &d);
        RSA_get0_factors(decodedRsa, &p, &q);
        RSA_get0_crt_params(decodedRsa, &dmp1, &dmq1, &iqmp);
    }

    if (ret == 1) {
        eDup = BN_dup(e);
        if (eDup == NULL) {
            WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "BN_dup", eDup);
            ret = 0;
        }
    }
    if (ret == 1) {
        nDup = BN_dup(n);
        if (nDup == NULL) {
            WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "BN_dup", nDup);
            ret = 0;
        }
    }
    if (ret == 1) {
        dDup = BN_dup(d);
        if (dDup == NULL) {
            WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "BN_dup", dDup);
            ret = 0;
        }
    }
    if (ret == 1) {
        pDup = BN_dup(p);
        if (pDup == NULL) {
            WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "BN_dup", pDup);
            ret = 0;
        }
    }
    if (ret == 1) {
        qDup = BN_dup(q);
        if (qDup == NULL) {
            WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "BN_dup", qDup);
            ret = 0;
        }
    }
    if (ret == 1) {
        dmp1Dup = BN_dup(dmp1);
        if (dmp1Dup == NULL) {
            WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "BN_dup", dmp1Dup);
            ret = 0;
        }
    }
    if (ret == 1) {
        dmq1Dup = BN_dup(dmq1);
        if (dmq1Dup == NULL) {
            WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "BN_dup", dmq1Dup);
            ret = 0;
        }
    }
    if (ret == 1) {
        iqmpDup = BN_dup(iqmp);
        if (iqmpDup == NULL) {
            WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "BN_dup", iqmpDup);
            ret = 0;
        }
    }

    if (ret == 1) {
        rc = RSA_set0_key(osslKey, nDup, eDup, dDup);
        if (rc != 1) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "RSA_set0_key", rc);
            ret = 0;
        }
    }
    if (ret == 1) {
        rc = RSA_set0_factors(osslKey, pDup, qDup);
        if (rc != 1) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "RSA_set0_factors", rc);
            ret = 0;
        }
    }
    if (ret == 1) {
        rc = RSA_set0_crt_params(osslKey, dmp1Dup, dmq1Dup, iqmpDup);
        if (rc != 1) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "RSA_set0_crt_params", rc);
            ret = 0;
        }
    }

    if (der != NULL) {
        OPENSSL_free(der);
    }
    if (decodedRsa != NULL) {
        RSA_free(decodedRsa);
    }

    WOLFENGINE_LEAVE(WE_LOG_PK, "we_convert_rsa", ret);

    return ret;
}

static int we_rsa_keygen_int(RsaKey *wolfKey, RSA **osslKey, int bits, long e)
{
    int ret = 1;
    int rc = 0;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_pkey_keygen");

    if (wolfKey == NULL) {
        WOLFENGINE_ERROR_MSG(WE_LOG_PK, "we_rsa_keygen_int: wolfKey NULL");
        ret = 0;
    }
    if (osslKey == NULL) {
        WOLFENGINE_ERROR_MSG(WE_LOG_PK, "we_rsa_keygen_int: osslKey NULL");
        ret = 0;
    }

    if (ret == 1) {
        rc = wc_MakeRsaKey(wolfKey, bits, e, we_rng);
        if (rc != 0) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_MakeRsaKey", rc);
            ret = 0;
        }
    }

    if (ret == 1 && *osslKey == NULL) {
        *osslKey = RSA_new();
        if (*osslKey == NULL) {
            WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "RSA_new", *osslKey);
            ret = 0;
        }
    }

    if (ret == 1) {
        rc = we_convert_rsa(wolfKey, *osslKey);
        if (rc != 1) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "we_convert_rsa", rc);
            ret = 0;
        }
    }

    WOLFENGINE_LEAVE(WE_LOG_PK, "we_rsa_keygen_int", ret);

    return ret;
}

static int we_rsa_keygen(RSA *osslKey, int bits, BIGNUM *eBn, BN_GENCB *cb)
{
    int ret = 1;
    int rc = 0;
    we_Rsa *engineRsa = NULL;
    long e = 0;

    (void)cb; /* Callback not supported, yet. */

    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_keygen");

    engineRsa = (we_Rsa *)RSA_get_ex_data(osslKey, WE_RSA_EX_DATA_IDX);
    if (engineRsa == NULL) {
        WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "RSA_get_ex_data", engineRsa);
        ret = 0;
    }

    if (ret == 1) {
        e = (long)BN_get_word(eBn);
        if (e == -1) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "BN_get_word", (int)e);
            ret = 0;
        }
    }

    if (ret == 1) {
        rc = we_rsa_keygen_int(&engineRsa->key, &osslKey, bits, e);
        if (rc != 1) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "we_rsa_keygen_int", rc);
            ret = 0;
        }
    }

    WOLFENGINE_LEAVE(WE_LOG_PK, "we_rsa_keygen", ret);

    return ret;
}

/**
 * Initialize the RSA method.
 *
 * @return  1 on success and 0 on failure.
 */
int we_init_rsa_meth(void)
{
    int ret = 1;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_init_rsa_meth");

    we_rsa_method = RSA_meth_new("wolfengine_rsa", 0);
    if (we_rsa_method == NULL) {
        WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "RSA_meth_new", we_rsa_method);
        ret = 0;
    }

    if (ret == 1) {
        RSA_meth_set_init(we_rsa_method, we_rsa_init);
        RSA_meth_set_finish(we_rsa_method, we_rsa_finish);
        RSA_meth_set_pub_enc(we_rsa_method, we_rsa_pub_enc);
        RSA_meth_set_pub_dec(we_rsa_method, we_rsa_pub_dec);
        RSA_meth_set_priv_enc(we_rsa_method, we_rsa_priv_enc);
        RSA_meth_set_priv_dec(we_rsa_method, we_rsa_priv_dec);
        RSA_meth_set_keygen(we_rsa_method, we_rsa_keygen);
    }

    if (ret == 0 && we_rsa_method != NULL) {
        RSA_meth_free(we_rsa_method);
        we_rsa_method = NULL;
    }

    WOLFENGINE_LEAVE(WE_LOG_PK, "we_init_rsa_meth", ret);

    return ret;
}

#ifdef WE_HAVE_EVP_PKEY

/** EVP public key method - RSA using wolfSSL for the implementation. */
EVP_PKEY_METHOD *we_rsa_pkey_method = NULL;

/**
 * Initialize and set the data required to complete an RSA operation.
 *
 * @param  ctx  [in]  Public key context of operation.
 * @returns  1 on success and 0 on failure.
 */
static int we_rsa_pkey_init(EVP_PKEY_CTX *ctx)
{
    int ret = 1;
    int rc = 0;
    we_Rsa *rsa;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_pkey_init");

    rsa = (we_Rsa *)OPENSSL_zalloc(sizeof(we_Rsa));
    if (rsa == NULL) {
        WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "OPENSSL_zalloc", rsa);
        ret = 0;
    }

    if (ret == 1) {
        rc = wc_InitRsaKey(&rsa->key, NULL);
        if (rc != 0) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_InitRsaKey", rc);
            ret = 0;
        }
    }

    if (ret == 1) {
        EVP_PKEY_CTX_set_data(ctx, rsa);
        rsa->padMode = RSA_PKCS1_PADDING;
        rsa->pubExp = DEFAULT_PUB_EXP;
        rsa->bits = DEFAULT_KEY_BITS;
    }

    if (ret == 0 && rsa != NULL) {
        OPENSSL_free(rsa);
    }

    WOLFENGINE_LEAVE(WE_LOG_PK, "we_rsa_pkey_init", ret);

    return ret;
}

/**
 * Clean up the RSA operation data.
 *
 * @param  ctx  [in]  Public key context of operation.
 */
static void we_rsa_pkey_cleanup(EVP_PKEY_CTX *ctx)
{
    we_Rsa *rsa = (we_Rsa *)EVP_PKEY_CTX_get_data(ctx);

    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_pkey_cleanup");

    if (rsa != NULL) {
        wc_FreeRsaKey(&rsa->key);
        OPENSSL_free(rsa);
        EVP_PKEY_CTX_set_data(ctx, NULL);
    }
}

/**
 * Copy the EVP public key method from/to EVP public key contexts.
 *
 * @param  dst  [in]  Destination public key context.
 * @param  src  [in]  Source public key context.
 * @returns  1 on success and 0 on failure.
 */
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
static int we_rsa_pkey_copy(EVP_PKEY_CTX *dst, const EVP_PKEY_CTX *src)
#else
static int we_rsa_pkey_copy(EVP_PKEY_CTX *dst, EVP_PKEY_CTX *src)
#endif
{
    int ret = 1;

    (void)dst;
    (void)src;
 
    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_pkey_copy");
    WOLFENGINE_LEAVE(WE_LOG_PK, "we_rsa_pkey_copy", ret);

    return ret;
}

/**
 * Generate an RSA key.
 *
 * @param  ctx   [in]   Public key context of operation.
 * @param  pkey  [out]  EVP public key to hold result.
 * @returns  1 on success and 0 on failure.
 */
static int we_rsa_pkey_keygen(EVP_PKEY_CTX *ctx, EVP_PKEY *pkey)
{
    int ret = 1;
    int rc = 0;
    we_Rsa *engineRsa = NULL;
    RSA *rsa = NULL;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_pkey_keygen");

    engineRsa = (we_Rsa *)EVP_PKEY_CTX_get_data(ctx);
    if (engineRsa == NULL) {
        WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "EVP_PKEY_CTX_get_data",
                                   engineRsa);
        ret = 0;
    }

    if (ret == 1) {
        rc = we_rsa_keygen_int(&engineRsa->key, &rsa, engineRsa->bits,
                               engineRsa->pubExp);
        if (rc == 0) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "we_rsa_keygen_int", rc);
            ret = 0;
        }
    }

    if (ret == 1) {
        ret = EVP_PKEY_assign_RSA(pkey, rsa);
        if (ret == 0) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "EVP_PKEY_assign_RSA", ret);
        }
    }

    WOLFENGINE_LEAVE(WE_LOG_PK, "we_rsa_pkey_keygen", ret);

    return ret;
}

/**
 * Extra operations for working with RSA.
 * Supported operations include:
 *  - EVP_PKEY_CTRL_RSA_PADDING: set the padding mode.
 *  - EVP_PKEY_CTRL_GET_RSA_PADDING: get the padding mode.
 *  - EVP_PKEY_CTRL_MD: set the digest method for sign/verify.
 *  - EVP_PKEY_CTRL_GET_MD: get the digets method for sign/verify.
 *  - EVP_PKEY_CTRL_RSA_KEYGEN_BITS: set the key size in bits.
 *  - EVP_PKEY_CTRL_RSA_KEYGEN_PUBEXP: set the public exponent, "e."
 *
 * @param  ctx   [in]  Public key context of operation.
 * @param  type  [in]  Type of operation to perform.
 * @param  num   [in]  Integer parameter.
 * @param  ptr   [in]  Pointer parameter.
 * @returns  1 on success and 0 on failure.
 */
static int we_rsa_pkey_ctrl(EVP_PKEY_CTX *ctx, int type, int num, void *ptr)
{
    int ret = 1;
    we_Rsa *rsa = NULL;
    BIGNUM* bn = NULL;
    long e;
    char errBuff[WOLFENGINE_MAX_ERROR_SZ];

    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_pkey_ctrl");

    rsa = (we_Rsa *)EVP_PKEY_CTX_get_data(ctx);
    if (rsa == NULL) {
        WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "EVP_PKEY_CTX_get_data", rsa);
        ret = 0;
    }

    if (ret == 1) {
        switch (type) {
            case EVP_PKEY_CTRL_RSA_PADDING:
                if (num != RSA_PKCS1_PADDING &&
                    num != RSA_PKCS1_PSS_PADDING &&
                    num != RSA_PKCS1_OAEP_PADDING &&
                    num != RSA_NO_PADDING)
                {
                    WOLFENGINE_ERROR_MSG(WE_LOG_PK,
                                         "Unsupported RSA padding mode.");
                    ret = 0;
                }
                else {
                    rsa->padMode = num;
                }
                break;
            case EVP_PKEY_CTRL_GET_RSA_PADDING:
                *(int *)ptr = rsa->padMode;
                break;
            case EVP_PKEY_CTRL_MD:
                rsa->md = (EVP_MD*)ptr;
                break;
            case EVP_PKEY_CTRL_GET_MD:
                *(const EVP_MD **)ptr = rsa->md;
                break;
            case EVP_PKEY_CTRL_DIGESTINIT:
                break;
#if OPENSSL_VERSION_NUMBER >= 0x1010100fL
            case EVP_PKEY_CTRL_RSA_KEYGEN_PRIMES:
                /* wolfCrypt can only do key generation with 2 primes. */
                WOLFENGINE_ERROR_MSG(WE_LOG_PK,
                                     "wolfCrypt does not support multi-prime"
                                     "RSA.");
                ret = 0;
                break;
#endif
            case EVP_PKEY_CTRL_RSA_KEYGEN_BITS:
                if (num < RSA_MIN_SIZE || num > RSA_MAX_SIZE) {
                    WOLFENGINE_ERROR_MSG(WE_LOG_PK,
                                         "RSA key size not in range.");
                    ret = 0;
                }
                else {
                    rsa->bits = num;
                    ret = 1;
                }
                break;
            case EVP_PKEY_CTRL_RSA_KEYGEN_PUBEXP:
                bn = (BIGNUM*)ptr;
                e = (long)BN_get_word(bn);
                if (e == -1) {
                    WOLFENGINE_ERROR_MSG(WE_LOG_PK,
                                         "RSA public exponent too large.");
                    ret = 0;
                }
                if (ret == 1 && e == 0) {
                    WOLFENGINE_ERROR_MSG(WE_LOG_PK,
                                         "RSA public exponent is 0.");
                    ret = 0;
                }
                if (ret == 1) {
                    rsa->pubExp = (int)e;
                }
                break;
            case EVP_PKEY_CTRL_RSA_PSS_SALTLEN:
                /* Store salt length to use with RSA-PSS. */
                if (rsa->padMode != RSA_PKCS1_PSS_PADDING) {
                    ret = 0;
                }
                rsa->saltLen = num;
                break;
            case EVP_PKEY_CTRL_GET_RSA_PSS_SALTLEN:
                /* Get the salt length to use with RSA-PSS. */
                if (rsa->padMode != RSA_PKCS1_PSS_PADDING) {
                    ret = 0;
                }
                if (ret == 1) {
                    *(int *)ptr = rsa->saltLen;
                }
                break;
            default:
                XSNPRINTF(errBuff, sizeof(errBuff), "Unsupported ctrl type %d",
                          type);
                WOLFENGINE_ERROR_MSG(WE_LOG_PK, errBuff);
                ret = 0;
                break;
        }
    }
    
    WOLFENGINE_LEAVE(WE_LOG_PK, "we_rsa_pkey_ctrl", ret);

    return ret;
}

/**
 * Extra operations for working with RSA.
 * Supported operations include:
 *  - "rsa_padding_mode": set the padding mode
 *  - "rsa_pss_saltlen": set RSA-PSS salt length to use
 *  - "rsa_keygen_bits": set size of RSA keys to generate in bits
 *  - "rsa_mgf1_md": set the RSA-PSS MGF1 hash to use
 *
 * @param  ctx   [in]  Public key context of operation.
 * @param  type  [in]  Type of operation to perform.
 * @param  num   [in]  Integer parameter.
 * @param  ptr   [in]  Pointer parameter.
 * @returns  1 on success and 0 on failure.
 */
static int we_rsa_pkey_ctrl_str(EVP_PKEY_CTX *ctx, const char *type,
                                const char *value)
{
    int ret = 1;
    we_Rsa *rsa = NULL;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_pkey_ctrl_str");

    rsa = (we_Rsa *)EVP_PKEY_CTX_get_data(ctx);
    if (rsa == NULL) {
        WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "EVP_PKEY_CTX_get_data", rsa);
        ret = 0;
    }

    if ((ret == 1) && (XSTRNCMP(type, "rsa_padding_mode", 17) == 0)) {
        /* Padding mode. */
        ret = 2;
        if (XSTRNCMP(value, "none", 5) == 0) {
            rsa->padMode = RSA_NO_PADDING;
        }
        else if (XSTRNCMP(value, "pkcs1", 6) == 0) {
            rsa->padMode = RSA_PKCS1_PADDING;
        }
        else if (XSTRNCMP(value, "oaep", 5) == 0) {
            rsa->padMode = RSA_PKCS1_OAEP_PADDING;
        }
        else if (XSTRNCMP(value, "pss", 4) == 0) {
            rsa->padMode = RSA_PKCS1_PSS_PADDING;
        }
        else {
            ret = 0;
        }
    }

    if ((ret == 1) && (XSTRNCMP(type, "rsa_pss_saltlen", 16) == 0)) {
        /* RSA-PSS salt length. */
        ret = 2;
        if (rsa->padMode != RSA_PKCS1_PSS_PADDING) {
            ret = 0;
        }
        else {
            rsa->saltLen = XATOI(value);
        }
    }

    if ((ret == 1) && (XSTRNCMP(type, "rsa_keygen_bits", 16) == 0)) {
        /* Size, in bits, of RSA key to generate. */
        ret = 2;
        rsa->bits = XATOI(value);
    }

    if ((ret == 1) && (XSTRNCMP(type, "rsa_mgf1_md", 12) == 0)) {
        /* Digest to use with MGF in RSA-PSS. */
        ret = 2;
        rsa->mdMGF1 = EVP_get_digestbyname(value);
        if (rsa->mdMGF1 == NULL) {
            ret = 0;
        }
    }

    if (ret == 1) {
        ret = 0;
    }
    else if (ret == 2) {
        ret = 1;
    }

    return ret;
}

/**
 * Encode a digest in DER format.
 *
 * @param  md            [in]   EVP_MD structure containing the hash type.
 * @param  digest        [in]   Buffer holding the digest.
 * @param  digestLen     [in]   Length of digest buffer.
 * @param  encodedDigest [out]  Buffer containing encoded digest. If NULL,
                                memory will be allocated and caller must free.
 * @returns Length of encoded digest on success and 0 on failure.
 */
static int we_der_encode_digest(const EVP_MD *md, const unsigned char *digest,
                                size_t digestLen, unsigned char **encodedDigest)
{
    int ret = 1;
    int hashOID = 0;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_der_encode_digest");

    if (*encodedDigest == NULL) {
        *encodedDigest = (unsigned char *)OPENSSL_malloc(MAX_DER_DIGEST_SZ);
        if (*encodedDigest == NULL) {
            WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "OPENSSL_malloc",
                                       *encodedDigest);
            ret = 0;
        }
    }

    if (ret == 1) {
        hashOID = we_nid_to_wc_hash_oid(EVP_MD_type(md));
        if (hashOID <= 0) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "we_nid_to_wc_hash_oid", hashOID);
            ret = 0;
        }
    }

    if (ret == 1) {
        ret = wc_EncodeSignature(*encodedDigest, digest, (word32)digestLen,
                                 hashOID);
        if (ret == 0) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_EncodeSignature", ret);
        }
    }

    WOLFENGINE_LEAVE(WE_LOG_PK, "we_der_encode_digest", ret);

    return ret;
}

/**
 * Sign data with a private RSA key.
 *
 * @param  ctx     [in]      Public key context of operation.
 * @param  sig     [in]      Buffer to hold signature data.
 *                           NULL indicates length of signature requested.
 * @param  sigLen  [in/out]  Length of signature buffer.
 * @param  tbs     [in]      To Be Signed data.
 * @param  tbsLen  [in]      Length of To Be Signed data.
 * @returns  1 on success and 0 on failure.
 */
static int we_rsa_pkey_sign(EVP_PKEY_CTX *ctx, unsigned char *sig,
                            size_t *sigLen, const unsigned char *tbs,
                            size_t tbsLen)
{
    int ret = 1;
    we_Rsa *rsa = NULL;
    EVP_PKEY *pkey = NULL;
    RSA *rsaKey = NULL;
    unsigned char *encodedDigest = NULL;
    int encodedDigestLen = 0;
    int len;
    int actualSigLen = 0;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_pkey_sign");

    rsa = (we_Rsa *)EVP_PKEY_CTX_get_data(ctx);
    if (rsa == NULL) {
        WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "EVP_PKEY_CTX_get_data", rsa);
        ret = 0;
    }

    /* Set up private key */
    if (ret == 1 && !rsa->privKeySet) {
        pkey = EVP_PKEY_CTX_get0_pkey(ctx);
        if (pkey == NULL) {
            WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "EVP_PKEY_CTX_get0_pkey",
                                       pkey);
            ret = 0;
        }
        if (ret == 1) {
            rsaKey = (RSA*)EVP_PKEY_get0_RSA(pkey);
            if (rsaKey == NULL) {
                WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "EVP_PKEY_get0_RSA",
                                           rsaKey);
                ret = 0;
            }
        }
        if (ret == 1) {
            ret = we_set_private_key(rsaKey, rsa);
            if (ret == 0) {
                WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "we_set_private_key", ret);
            }
        }
    }

    if (ret == 1 && sig == NULL) {
        len = wc_SignatureGetSize(WC_SIGNATURE_TYPE_RSA, &rsa->key,
                                  sizeof(rsa->key));
        if (len <= 0) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_SignatureGetSize", (int)len);
            ret = 0;
        }
        else {
            /* Return signature size in bytes. */
            *sigLen = len;
        }
    }

    if (ret == 1 && sig != NULL) {
        if (rsa->md != NULL && rsa->padMode != RSA_PKCS1_PSS_PADDING) {
            /* In this case, OpenSSL expects a proper PKCS #1 v1.5
               signature. */
            encodedDigestLen = we_der_encode_digest(rsa->md, tbs, tbsLen,
                                                    &encodedDigest);
            if (encodedDigestLen == 0) {
                WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "we_der_encode_digest",
                                      encodedDigestLen);
                ret = 0;
            }
            else {
                tbs = encodedDigest;
                tbsLen = encodedDigestLen;
            }
        }
        if (ret == 1) {
            actualSigLen = we_rsa_priv_enc_int(tbsLen, tbs, *sigLen, sig, rsa);
            if (actualSigLen == -1) {
                WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "we_rsa_priv_enc_int",
                                      actualSigLen);
                ret = 0;
            }
            else {
                *sigLen = actualSigLen;
            }
        }
    }

    if (encodedDigest != NULL) {
        OPENSSL_free(encodedDigest);
    }

    WOLFENGINE_LEAVE(WE_LOG_PK, "we_rsa_pkey_sign", ret);

    return ret;
}

/**
 * Verify data with a public RSA key.
 *
 * @param  ctx     [in]  Public key context of operation.
 * @param  sig     [in]  Signature data.
 * @param  sigLen  [in]  Length of signature data.
 * @param  tbs     [in]  To Be Signed data.
 * @param  tbsLen  [in]  Length of To Be Signed data.
 * @returns  1 on success and 0 on failure.
 */
static int we_rsa_pkey_verify(EVP_PKEY_CTX *ctx, const unsigned char *sig,
                         size_t sigLen, const unsigned char *tbs,
                         size_t tbsLen)
{
    int ret = 1;
    int rc = 0;
    we_Rsa *rsa = NULL;
    EVP_PKEY *pkey = NULL;
    RSA *rsaKey = NULL;
    unsigned char *decryptedSig = NULL;
    unsigned char *encodedDigest = NULL;
    int encodedDigestLen = 0;

    WOLFENGINE_ENTER(WE_LOG_PK, "we_rsa_pkey_verify");

    rsa = (we_Rsa *)EVP_PKEY_CTX_get_data(ctx);
    if (rsa == NULL) {
        WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "EVP_PKEY_CTX_get_data", rsa);
        ret = 0;
    }

    /* Set up public key */
    if (ret == 1 && !rsa->pubKeySet) {
        pkey = EVP_PKEY_CTX_get0_pkey(ctx);
        if (pkey == NULL) {
            WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "EVP_PKEY_CTX_get0_pkey",
                                       pkey);
            ret = 0;
        }
        if (ret == 1) {
            rsaKey = (RSA*)EVP_PKEY_get0_RSA(pkey);
            if (rsaKey == NULL) {
                WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "EVP_PKEY_get0_RSA",
                                           rsaKey);
                ret = 0;
            }
        }
        if (ret == 1) {
            ret = we_set_public_key(rsaKey, rsa);
            if (ret == 0) {
                WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "we_set_public_key", ret);
            }
        }
    }

    if (ret == 1) {
        decryptedSig = (unsigned char *)OPENSSL_malloc(sigLen);
        if (decryptedSig == NULL) {
            WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "OPENSSL_malloc",
                                       decryptedSig);
            ret = 0;
        }
    }

    if (ret == 1) {
        rc = we_rsa_pub_dec_int(sigLen, sig, sigLen, decryptedSig, rsa);
        if (rc == -1) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "we_rsa_pub_dec_int", rc);
            ret = 0;
        }
    }

    if (ret == 1 && rsa->padMode == RSA_PKCS1_PSS_PADDING) {
        /* Verify call in we_rsa_pub_dec_int only decrypts - this actually
           checks padding. */
        rc = wc_RsaPSS_CheckPadding_ex(tbs, (word32)tbsLen, decryptedSig, rc,
            we_nid_to_wc_hash_type(EVP_MD_type(rsa->md)), rsa->saltLen, 0);
        if (rc != 0) {
            WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "wc_RsaPSS_CheckPadding_ex", rc);
            ret = 0;
        }
    }
    else {
        if (ret == 1 && rsa->md != NULL) {
            /* In this case, we have a proper DER-encoded signature, not
               just arbitrary signed data, so we must compare with the
               encoded digest. */
            encodedDigestLen = we_der_encode_digest(rsa->md, tbs, tbsLen,
                                                    &encodedDigest);
            if (encodedDigestLen == 0) {
                WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "we_der_encode_digest",
                                      encodedDigestLen);
                ret = 0;
            }
            else {
                tbs = encodedDigest;
                tbsLen = encodedDigestLen;
            }
        }
        if (ret == 1) {
            rc = XMEMCMP(tbs, decryptedSig, tbsLen);
            if (rc != 0) {
                WOLFENGINE_ERROR_FUNC(WE_LOG_PK, "XMEMCMP", rc);
                ret = 0;
            }
        }
    }    

    if (decryptedSig != NULL) {
        OPENSSL_free(decryptedSig);
    }

    if (encodedDigest != NULL) {
        OPENSSL_free(encodedDigest);
    }

    return ret;
}

/**
 * Initialize the RSA method for use with the EVP_PKEY API.
 *
 * @return  1 on success and 0 on failure.
 */
int we_init_rsa_pkey_meth(void)
{
    int ret = 1;

    we_rsa_pkey_method = EVP_PKEY_meth_new(EVP_PKEY_RSA, 0);
    if (we_rsa_pkey_method == NULL) {
        WOLFENGINE_ERROR_FUNC_NULL(WE_LOG_PK, "EVP_PKEY_meth_new",
                                   we_rsa_pkey_method);
        ret = 0;
    }

    if (ret == 1) {
        EVP_PKEY_meth_set_init(we_rsa_pkey_method, we_rsa_pkey_init);
        EVP_PKEY_meth_set_sign(we_rsa_pkey_method, NULL, we_rsa_pkey_sign);
        EVP_PKEY_meth_set_verify(we_rsa_pkey_method, NULL, we_rsa_pkey_verify);
        EVP_PKEY_meth_set_cleanup(we_rsa_pkey_method, we_rsa_pkey_cleanup);
        EVP_PKEY_meth_set_ctrl(we_rsa_pkey_method, we_rsa_pkey_ctrl,
                               we_rsa_pkey_ctrl_str);
        EVP_PKEY_meth_set_copy(we_rsa_pkey_method, we_rsa_pkey_copy);
        EVP_PKEY_meth_set_keygen(we_rsa_pkey_method, NULL, we_rsa_pkey_keygen);
    }

    if (ret == 0 && we_rsa_pkey_method != NULL) {
        EVP_PKEY_meth_free(we_rsa_pkey_method);
        we_rsa_pkey_method = NULL;
    }

    return ret;
}

#endif /* WE_HAVE_EVP_PKEY */

#endif /* WE_HAVE_RSA */
