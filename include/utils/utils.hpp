#pragma once
#include <memory>
#include <string>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::utils {
//---------------------------------------------------------------------------
#ifndef NDEBUG
#define verify(expression) assert(expression)
#else
#define verify(expression) ((void) (expression))
#endif
//---------------------------------------------------------------------------
/// Encode url special characters in %HEX
std::string encodeUrlParameters(const std::string& encode);
/// Encode everything from binary representation to hex
std::string hexEncode(const uint8_t* input, uint64_t length, bool upper = false);
/// Encode everything from binary representation to base64
std::string base64Encode(const uint8_t* input, uint64_t length);
/// Decodes from base64 to raw string
std::pair<std::unique_ptr<uint8_t[]>, uint64_t> base64Decode(const uint8_t* input, uint64_t length);
/// Build sha256 of the data encoded as hex
std::string sha256Encode(const uint8_t* data, uint64_t length);
/// Build md5 of the data
std::string md5Encode(const uint8_t* data, uint64_t length);
/// Sing with hmac and return sha256 encoded signature
std::pair<std::unique_ptr<uint8_t[]>, uint64_t> hmacSign(const uint8_t* keyData, uint64_t keyLength, const uint8_t* msgData, uint64_t msgLength);
/// Sing with rsa and return sha256 encoded signature
std::pair<std::unique_ptr<uint8_t[]>, uint64_t> rsaSign(const uint8_t* keyData, uint64_t keyLength, const uint8_t* msgData, uint64_t msgLength);
/// Decrypt with AES-256-CBC
uint64_t aesDecrypt(const unsigned char* key, const unsigned char* iv, const uint8_t* encData, uint64_t encLength, uint8_t* plainData);
/// Encrypt with AES-256-CBC
uint64_t aesEncrypt(const unsigned char* key, const unsigned char* iv, const uint8_t* plainData, uint64_t plainLength, uint8_t* encData);
//---------------------------------------------------------------------------
} // namespace anyblob::utils
