#include "utils/utils.hpp"
#include <cassert>
#include <stdexcept>
#include <utility>
#include <openssl/aes.h>
#include <openssl/bio.h>
#include <openssl/core_names.h>
#include <openssl/encoder.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/params.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
namespace anyblob {
namespace utils {
//---------------------------------------------------------------------------
string base64Encode(const uint8_t* input, uint64_t length)
// Encodes a string as a base64 string
{
    assert(in_range<int>(length));
    auto baseLength = 4 * ((length + 2) / 3);
    auto buffer = make_unique<char[]>(baseLength + 1);
    auto encodeLength = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(buffer.get()), input, static_cast<int>(length));
    if (encodeLength < 0 || static_cast<unsigned>(encodeLength) != baseLength)
        throw runtime_error("OpenSSL Error!");
    return string(buffer.get(), static_cast<unsigned>(encodeLength));
}
//---------------------------------------------------------------------------
pair<unique_ptr<uint8_t[]>, uint64_t> base64Decode(const uint8_t* input, uint64_t length)
// Decodes from base64 to raw string
{
    assert(in_range<int>(length));
    auto baseLength = 3 * length / 4;
    auto buffer = make_unique<uint8_t[]>(baseLength + 1);
    if (!length) {
        return {move(buffer), 0};
    }
    auto decodeLength = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(buffer.get()), input, static_cast<int>(length));
    if (decodeLength < 0 || static_cast<unsigned>(decodeLength) != baseLength)
        throw runtime_error("OpenSSL Error!");
    while (input[--length] == '=') {
        --decodeLength;
        if (static_cast<unsigned>(decodeLength) + 2 < baseLength)
            throw runtime_error("OpenSSL Error!");
    }
    return {move(buffer), decodeLength};
}
//---------------------------------------------------------------------------
string hexEncode(const uint8_t* input, uint64_t length, bool upper)
// Encodes a string as a hex string
{
    const char hex[] = "0123456789abcdef";
    string output;
    output.reserve(length << 1);
    for (auto i = 0u; i < length; i++) {
        output.push_back(upper ? static_cast<char>(toupper(hex[input[i] >> 4])) : hex[input[i] >> 4]);
        output.push_back(upper ? static_cast<char>(toupper(hex[input[i] & 15])) : hex[input[i] & 15]);
    }
    return output;
}
//---------------------------------------------------------------------------
string encodeUrlParameters(const string& encode)
// Encodes a string for url
{
    string result;
    for (auto c : encode) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            result += c;
        else {
            result += "%";
            result += hexEncode(reinterpret_cast<uint8_t*>(&c), 1, true);
        }
    }
    return result;
}
//---------------------------------------------------------------------------
string sha256Encode(const uint8_t* data, uint64_t length)
// Encodes the data as sha256 hex string
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    EVP_MD_CTX* mdctx;
    if ((mdctx = EVP_MD_CTX_new()) == nullptr)
        throw runtime_error("OpenSSL Error!");

    if (EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr) <= 0)
        throw runtime_error("OpenSSL Error!");

    if (EVP_DigestUpdate(mdctx, data, length) <= 0)
        throw runtime_error("OpenSSL Error!");

    unsigned digestLength = SHA256_DIGEST_LENGTH;
    if (EVP_DigestFinal_ex(mdctx, reinterpret_cast<unsigned char*>(hash), &digestLength) <= 0)
        throw runtime_error("OpenSSL Error!");

    EVP_MD_CTX_free(mdctx);
    return hexEncode(hash, SHA256_DIGEST_LENGTH);
}
//---------------------------------------------------------------------------
string md5Encode(const uint8_t* data, uint64_t length)
// Encodes the data as md5 string
{
    EVP_MD_CTX* mdctx;
    unsigned char hash[MD5_DIGEST_LENGTH];

    if ((mdctx = EVP_MD_CTX_new()) == nullptr)
        throw runtime_error("OpenSSL Error!");

    if (EVP_DigestInit_ex(mdctx, EVP_md5(), nullptr) <= 0)
        throw runtime_error("OpenSSL Error!");

    if (EVP_DigestUpdate(mdctx, data, length) <= 0)
        throw runtime_error("OpenSSL Error!");

    unsigned digestLength = MD5_DIGEST_LENGTH;
    if (EVP_DigestFinal_ex(mdctx, reinterpret_cast<unsigned char*>(hash), &digestLength) <= 0)
        throw runtime_error("OpenSSL Error!");

    EVP_MD_CTX_free(mdctx);
    return string(reinterpret_cast<char*>(hash), digestLength);
}
//---------------------------------------------------------------------------
pair<unique_ptr<uint8_t[]>, uint64_t> hmacSign(const uint8_t* keyData, uint64_t keyLength, const uint8_t* msgData, uint64_t msgLength)
// Encodes the msg with the key with hmac-sha256
{
    auto mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    if (!mac)
        throw runtime_error("OpenSSL Error!");

    OSSL_PARAM params[4];
    auto* p = params;
    string digest = "SHA2-256";
    *p++ = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, digest.data(), digest.size());
    *p = OSSL_PARAM_construct_end();

    auto mctx = EVP_MAC_CTX_new(mac);
    if (!mctx)
        throw runtime_error("OpenSSL Error!");

    if (EVP_MAC_init(mctx, keyData, keyLength, params) <= 0)
        throw runtime_error("OpenSSL Error!");

    if (EVP_MAC_update(mctx, msgData, msgLength) <= 0)
        throw runtime_error("OpenSSL Error!");

    size_t len;
    if (EVP_MAC_final(mctx, NULL, &len, 0) <= 0)
        throw runtime_error("OpenSSL Error!");

    auto hash = make_unique<uint8_t[]>(len);

    if (EVP_MAC_final(mctx, hash.get(), &len, len) <= 0)
        throw runtime_error("OpenSSL len!");

    EVP_MAC_CTX_free(mctx);
    EVP_MAC_free(mac);

    return {move(hash), SHA256_DIGEST_LENGTH};
}
//---------------------------------------------------------------------------
pair<unique_ptr<uint8_t[]>, uint64_t> rsaSign(const uint8_t* keyData, uint64_t keyLength, const uint8_t* msgData, uint64_t msgLength)
// Encodes the msg with the key with rsa
{
    assert(in_range<int>(keyLength));
    auto* keybio = BIO_new_mem_buf(reinterpret_cast<const void*>(keyData), static_cast<int>(keyLength));
    if (!keybio)
        throw runtime_error("OpenSSL Error - No Buffer Mem!");

    EVP_PKEY* priKey = nullptr;
    if (!PEM_read_bio_PrivateKey(keybio, &priKey, nullptr, nullptr))
        throw runtime_error("OpenSSL Error - Read Private Key!");

    auto* rsaSign = EVP_MD_CTX_new();
    if (EVP_DigestSignInit(rsaSign, nullptr, EVP_sha256(), nullptr, priKey) <= 0)
        throw runtime_error("OpenSSL Error - Sign Init!");

    if (EVP_DigestSignUpdate(rsaSign, msgData, msgLength) <= 0)
        throw runtime_error("OpenSSL Error - Sign Update!");

    size_t msgLenghtEnc;
    if (EVP_DigestSignFinal(rsaSign, nullptr, &msgLenghtEnc) <= 0)
        throw runtime_error("OpenSSL Error - Sign Final!");

    auto hash = make_unique<uint8_t[]>(msgLenghtEnc);
    if (EVP_DigestSignFinal(rsaSign, hash.get(), &msgLenghtEnc) <= 0)
        throw runtime_error("OpenSSL Error - Sign Final!");

    BIO_free_all(keybio);
    EVP_MD_CTX_free(rsaSign);
    EVP_PKEY_free(priKey);
    return {move(hash), msgLenghtEnc};
}
//---------------------------------------------------------------------------
uint64_t aesDecrypt(const unsigned char* key, const unsigned char* iv, const uint8_t* encData, uint64_t encLength, uint8_t* plainData)
// Decrypt with AES
{
    assert(in_range<int>(encLength));
    int len;
    uint64_t plainLength;

    unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx.get())
        throw runtime_error("OpenSSL Cipher Error!");

    if (EVP_DecryptInit(ctx.get(), EVP_aes_256_cbc(), key, iv) <= 0)
        throw runtime_error("OpenSSL Decrypt Init Error!");

    if (EVP_DecryptUpdate(ctx.get(), plainData, &len, reinterpret_cast<const unsigned char*>(encData), static_cast<int>(encLength)) <= 0)
        throw runtime_error("OpenSSL Decrypt Error!");
    assert(in_range<unsigned>(len));
    plainLength = static_cast<unsigned>(len);

    if (EVP_DecryptFinal(ctx.get(), plainData + len, &len) <= 0)
        throw runtime_error("OpenSSL Decrypt Final Error!");
    assert(in_range<unsigned>(len));
    plainLength += static_cast<unsigned>(len);

    return plainLength;
}
//---------------------------------------------------------------------------
uint64_t aesEncrypt(const unsigned char* key, const unsigned char* iv, const uint8_t* plainData, uint64_t plainLength, uint8_t* encData)
// Encrypt with AES
{
    assert(in_range<int>(plainLength));
    int len;
    uint64_t encLength;

    unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx.get())
        throw runtime_error("OpenSSL Cipher Error!");

    if (EVP_EncryptInit(ctx.get(), EVP_aes_256_cbc(), key, iv) <= 0)
        throw runtime_error("OpenSSL Encrypt Init Error!");

    if (EVP_EncryptUpdate(ctx.get(), encData, &len, reinterpret_cast<const unsigned char*>(plainData), static_cast<int>(plainLength)) <= 0)
        throw runtime_error("OpenSSL Encrypt Error!");
    assert(in_range<unsigned>(len));
    encLength = static_cast<unsigned>(len);

    if (EVP_EncryptFinal(ctx.get(), encData + len, &len) <= 0)
        throw runtime_error("OpenSSL Encrypt Final Error!");
    assert(in_range<unsigned>(len));
    encLength += static_cast<unsigned>(len);

    return encLength;
}
//---------------------------------------------------------------------------
}; // namespace utils
}; // namespace anyblob
