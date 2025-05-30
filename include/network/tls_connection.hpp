#pragma once
#include <memory>
#include <openssl/types.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2023
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network {
//---------------------------------------------------------------------------
struct HTTPSMessage;
class TLSContext;
class ConnectionManager;
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
        size_t internalBioWrite;
        /// Bytes read from network bio (used for send)
        int64_t networkBioRead;
        /// Bytes written to socket (used for send)
        size_t socketWrite;
        /// Bytes wanted to read from internal bio (used for recv)
        size_t internalBioRead;
        /// Bytes written to network bio (used for recv)
        int64_t networkBioWrite;
        /// Bytes read fromsocket (used for recv)
        size_t socketRead;
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
    HTTPSMessage* _message;
    /// The SSL context
    TLSContext& _context;
    /// The SSL connection
    SSL* _ssl;
    /// The internal buffer used for communicating with SSL
    BIO* _internalBio;
    /// The external buffer used for communicating with the socket
    BIO* _networkBio;
    /// The buffer
    std::unique_ptr<char[]> _buffer;
    /// The state
    State _state;
    /// Already conencted
    bool _connected;

    public:
    /// The constructor
    TLSConnection(TLSContext& context);

    /// The destructor
    ~TLSConnection();

    // Initialze SSL
    [[nodiscard]] bool init(HTTPSMessage* message);
    // Initialze SSL
    void destroy();
    /// Get the SSL/TLS context
    [[nodiscard]] inline TLSContext& getContext() const { return _context; }

    /// Recv a TLS encrypted message
    [[nodiscard]] Progress recv(ConnectionManager& connectionManager, char* buffer, int64_t bufferLength, int64_t& resultLength);
    /// Send a TLS encrypted message
    [[nodiscard]] Progress send(ConnectionManager& connectionManager, const char* buffer, int64_t bufferLength, int64_t& resultLength);
    /// SSL/TLS connect
    [[nodiscard]] Progress connect(ConnectionManager& connectionManager);
    /// SSL/TLS shutdown
    [[nodiscard]] Progress shutdown(ConnectionManager& connectionManager, bool failedOnce = false);

    private:
    /// Helper function that handles the SSL_op calls
    template <typename F>
    Progress operationHelper(ConnectionManager& connectionManager, F&& func, int64_t& result);
    /// The processing of the shadow tls layer
    Progress process(ConnectionManager& connectionManager);
};
//---------------------------------------------------------------------------
} // namespace anyblob::network
