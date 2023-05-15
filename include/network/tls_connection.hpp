#pragma once
#include <memory>
#include <mutex>
#include <openssl/types.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2023
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace network {
//---------------------------------------------------------------------------
struct HTTPSMessage;
class IOUringSocket;
class TLSContext;
//---------------------------------------------------------------------------
/// The TLS Interface
//---------------------------------------------------------------------------
/* anyblob |   OpenSSL
 *   |     |
 *    -------> SSL_read / SSL_write / SSL_connect
 *         |     /\    ||
 *         |     ||    \/
 *         |    internalBio
 *         |    networkBio
 *         |     ||     /\
 *         |     \/     ||
 *    -------<  recv / send
 *   |     |
 *   |     |
 *  socket
 * Adopted from https://www.openssl.org/docs/man3.1/man3/BIO_new_bio_pair.html
*/
//---------------------------------------------------------------------------
class TLSConnection {
    public:
    /// The progress of the TLS
    enum class Progress : uint16_t {
        Init,
        SendingInit,
        Sending,
        ReceivingInit,
        Receiving,
        Progress,
        Finished,
        Aborted
    };

    private:
    /// The state
    struct State {
        /// Bytes wanted to write from internal bio (used for send)
        int internalBioWrite;
        /// Bytes read from network bio (used for send)
        int networkBioRead;
        /// Bytes written to socket (used for send)
        int socketWrite;
        /// Bytes wanted to read from internal bio (used for recv)
        int internalBioRead;
        // Bytes written to network bio (used for recv)
        int networkBioWrite;
        /// Bytes read fromsocket (used for recv)
        int socketRead;
        /// The progress
        Progress progress;

        /// Resets the statistics
        inline void reset() {
            internalBioWrite = 0;
            networkBioRead = 0;
            socketWrite = 0;
            internalBioRead = 0;
            networkBioWrite = 0;
            socketRead = 0;
        }
    };
    /// The corresponding
    HTTPSMessage& message;
    /// The SSL context
    TLSContext& context;
    /// The SSL connection
    SSL* ssl;
    /// The internal buffer used for communicating with SSL
    BIO* internalBio;
    /// The external buffer used for communicating with the socket
    BIO* networkBio;
    /// The buffer
    std::unique_ptr<char[]> buffer;
    /// The state
    State state;

    public:
    /// The constructor
    TLSConnection(HTTPSMessage& message, TLSContext& context);

    /// The destructor
    ~TLSConnection();

    // Initialze SSL
    [[nodiscard]] bool init();
    // Initialze SSL
    void destroy();
    /// Get the SSL/TLS context
    [[nodiscard]] inline TLSContext& getContext() const { return context; }

    /// Recv a TLS encrypted message
    [[nodiscard]] Progress recv(IOUringSocket& socket, char* buffer, int64_t bufferLength, int64_t& resultLength);
    /// Send a TLS encrypted message
    [[nodiscard]] Progress send(IOUringSocket& socket, const char* buffer, int64_t bufferLength, int64_t& resultLength);
    /// SSL/TLS connect
    [[nodiscard]] Progress connect(IOUringSocket& socket);

    private:
    /// Helper function that handles the SSL_op calls
    template <typename F>
    Progress operationHelper(IOUringSocket& socket, F&& func, int64_t& result);
    /// The processing of the shadow tls layer
    Progress process(IOUringSocket& socket);
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
